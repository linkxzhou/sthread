/*
 * Copyright (C) zhoulv2000@163.com
 */

#include <stdarg.h>
#include "st_sys.h"

ST_NAMESPACE_USING

SyscallCallbackTab      g_syscall_tab;
int                     g_hook_flag;

static HookFd           g_hookfd_tab[ST_HOOK_MAX_FD];

HookFd* st_find_fd(int fd)
{
    if ((fd < 0) || (fd >= ST_HOOK_MAX_FD))
    {
        return NULL;
    }

    HookFd* fd_info = &g_hookfd_tab[fd];
    if (fd_info->sock_flag & ST_FD_FLG_INUSE)
    {
        return fd_info;
    }
    else
    {
        return NULL;
    }
}

void st_new_fd(int fd)
{
    if ((fd < 0) || (fd >= ST_HOOK_MAX_FD))
    {
        return;
    }

    HookFd* fd_info = &g_hookfd_tab[fd];
    fd_info->sock_flag = ST_FD_FLG_INUSE;
    fd_info->read_timeout = 500; // 设置等待的ms，默认500ms
    fd_info->write_timeout = 500; // 设置等待的ms，默认500ms
}

void st_free_fd(int fd)
{
    if ((fd < 0) || (fd >= ST_HOOK_MAX_FD))
    {
        return;
    }

    HookFd* fd_info = &g_hookfd_tab[fd];
    fd_info->sock_flag = ST_FD_FLG_NOUSE;
    fd_info->read_timeout = 0;
    fd_info->write_timeout = 0;
}

int st_socket(int domain, int type, int protocol)
{
    HOOK_SYSCALL(socket);
    if (!HOOK_ACTIVE())
    {
        return REAL_FUNC(socket)(domain, type, protocol);
    }

    int fd = REAL_FUNC(socket)(domain, type, protocol);
    if (fd < 0)
    {
        return fd;
    }

    // 设置新的FD
    st_new_fd(fd);

    // 默认都设置为非阻塞
    int flags;
    flags = st_fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    st_fcntl(fd, F_SETFL, flags);

    return fd;
}

int st_close(int fd)
{
    HOOK_SYSCALL(close);
    HookFd* hook_fd = st_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(close)(fd);
    }

    // 释放FD
    st_free_fd(fd);

    return REAL_FUNC(close)(fd);
}

int st_connect(int fd, const struct sockaddr *address, socklen_t address_len)
{
    HOOK_SYSCALL(connect);
    HookFd* hook_fd = st_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(connect)(fd, address, address_len);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(connect)(fd, address, address_len);
    }
    else
    {
        st_new_fd(fd);
        return _connect(fd, address, (int)address_len, hook_fd->write_timeout);
    }
}

ssize_t st_read(int fd, void *buffer, size_t nbyte)
{
    HOOK_SYSCALL(read);
    HookFd* hook_fd = st_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(read)(fd, buffer, nbyte);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(read)(fd, buffer, nbyte);
    }
    else
    {
        st_new_fd(fd);
        return _read(fd, buffer, nbyte, hook_fd->read_timeout);
    }
}

ssize_t st_write(int fd, const void *buffer, size_t nbyte)
{
    HOOK_SYSCALL(write);
    HookFd* hook_fd = st_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(write)(fd, buffer, nbyte);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(write)(fd, buffer, nbyte);
    }
    else
    {
        st_new_fd(fd);
        return _write(fd, buffer, nbyte, hook_fd->write_timeout);
    }
}

ssize_t st_sendto(int fd, const void *buffer, size_t length, int flags,
            const struct sockaddr *dest_addr, socklen_t dest_len)
{
    HOOK_SYSCALL(sendto);
    HookFd* hook_fd = st_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(sendto)(fd, buffer, length, flags, dest_addr, dest_len);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(sendto)(fd, buffer, length, flags, dest_addr, dest_len);
    }
    else
    {
        st_new_fd(fd);
        return _sendto(fd, buffer, (int)length, flags, dest_addr, 
            dest_len, hook_fd->write_timeout);
    }
}

ssize_t st_recvfrom(int fd, void *buffer, size_t length, int flags,
            struct sockaddr *address, socklen_t *address_len)
{
    HOOK_SYSCALL(recvfrom);
    HookFd* hook_fd = st_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
    }
    else
    {
        st_new_fd(fd);
        return _recvfrom(fd, buffer, length, flags, address, address_len, hook_fd->read_timeout);
    }
}

ssize_t st_recv(int fd, void *buffer, size_t length, int flags)
{
    HOOK_SYSCALL(recv);
    HookFd* hook_fd = st_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(recv)(fd, buffer, length, flags);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(recv)(fd, buffer, length, flags);
    }
    else
    {
        st_new_fd(fd);
        return _recv(fd, buffer, length, flags, hook_fd->read_timeout);
    }
}

ssize_t st_send(int fd, const void *buffer, size_t nbyte, int flags)
{
    HOOK_SYSCALL(send);
    HookFd* hook_fd = st_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(send)(fd, buffer, nbyte, flags);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(send)(fd, buffer, nbyte, flags);
    }
    else
    {
        st_new_fd(fd);
        return _send(fd, buffer, nbyte, flags, hook_fd->write_timeout);
    }
}

int st_setsockopt(int fd, int level, int option_name, 
        const void *option_value, socklen_t option_len)
{
    HOOK_SYSCALL(setsockopt);
    HookFd* hook_fd = st_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(setsockopt)(fd, level, option_name, option_value, option_len);
    }

    if (SOL_SOCKET == level)
    {
        struct timeval *val = (struct timeval*)option_value;
        if (SO_RCVTIMEO == option_name)
        {
            hook_fd->read_timeout = val->tv_sec * 1000 + val->tv_usec / 1000;
        }
        else if (SO_SNDTIMEO == option_name)
        {
            hook_fd->write_timeout = val->tv_sec * 1000 + val->tv_usec / 1000;
        }
    }

    return REAL_FUNC(setsockopt)(fd, level, option_name, option_value, option_len);
}

int st_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    ::va_start(ap, cmd);
    void* arg = va_arg(ap, void *);
    ::va_end(ap);

    HOOK_SYSCALL(fcntl);
    HookFd* hook_fd = st_find_fd(fd);
    
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(fcntl)(fd, cmd, arg);
    }

    if (cmd == F_SETFL)
    {
        ::va_start(ap, cmd);
        int flags = va_arg(ap, int);
        ::va_end(ap);

        if (flags & O_NONBLOCK)
        {
            hook_fd->sock_flag |= ST_FD_FLG_UNBLOCK;
        }
    }

    return REAL_FUNC(fcntl)(fd, cmd, arg);
}

int st_ioctl(int fd, uint64_t cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    void* arg = va_arg(ap, void *);
    va_end(ap);

    HOOK_SYSCALL(ioctl);
    HookFd* hook_fd = st_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(ioctl)(fd, cmd, arg);
    }

    if (cmd == FIONBIO)
    {
        int flags = (arg != NULL) ? *((int*)arg) : 0;
        if (flags != 0)
        {
            hook_fd->sock_flag |= ST_FD_FLG_UNBLOCK;
        }
    }

    return REAL_FUNC(ioctl)(fd, cmd, arg);
}

int st_accept(int fd, const struct sockaddr *address, socklen_t *address_len)
{
    HOOK_SYSCALL(accept);
    HookFd* hook_fd = st_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(accept)(fd, address, address_len);
    }

    if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(accept)(fd, address, address_len);
    }

    int accept_fd = REAL_FUNC(accept)(fd, address, address_len);
    st_new_fd(accept_fd);

    // 设置为非阻塞
    if (accept_fd > 0)
    {
        int flags;
        flags = st_fcntl(accept_fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        st_fcntl(accept_fd, F_SETFL, flags);
    }
    
    return accept_fd;
}