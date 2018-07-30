/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_sys_hook.h"
#include "mt_frame.h"

MTHREAD_NAMESPACE_USING

SyscallFuncTab      g_syscall_tab;
int                 g_hook_flag;

static HookFd       g_hookfd_tab[MT_HOOK_MAX_FD];

HookFd* mt_find_fd(int fd)
{
    // LOG_CHECK_FUNCTION;

    if ((fd < 0) || (fd >= MT_HOOK_MAX_FD))
    {
        return NULL;
    }

    HookFd* fd_info = &g_hookfd_tab[fd];
    if (fd_info->sock_flag_ & MT_FD_FLG_INUSE)
    {
        return fd_info;
    }
    else
    {
        return NULL;
    }
}

void mt_new_fd(int fd)
{
    LOG_CHECK_FUNCTION;

    if ((fd < 0) || (fd >= MT_HOOK_MAX_FD))
    {
        return;
    }

    HookFd* fd_info = &g_hookfd_tab[fd];
    fd_info->sock_flag_ = MT_FD_FLG_INUSE;
    fd_info->read_timeout_ = 500; // 设置等待的ms
    fd_info->write_timeout_ = 500; // 设置等待的ms
}

void mt_free_fd(int fd)
{
    LOG_CHECK_FUNCTION;

    if ((fd < 0) || (fd >= MT_HOOK_MAX_FD))
    {
        return;
    }

    HookFd* fd_info = &g_hookfd_tab[fd];
    fd_info->sock_flag_ = MT_FD_FLG_NOUSE;
    fd_info->read_timeout_ = 0;
    fd_info->write_timeout_ = 0;
}

int mt_socket(int domain, int type, int protocol)
{
    LOG_CHECK_FUNCTION;

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

    mt_new_fd(fd);

    int flags;
    flags = mt_fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    mt_fcntl(fd, F_SETFL, flags);

    return fd;
}

int mt_close(int fd)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(close);
    HookFd* hook_fd = mt_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(close)(fd);
    }

    mt_free_fd(fd);

    return REAL_FUNC(close)(fd);
}

int mt_connect(int fd, const struct sockaddr *address, socklen_t address_len)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(connect);
    HookFd* hook_fd = mt_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(connect)(fd, address, address_len);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(connect)(fd, address, address_len);
    }
    else
    {
        mt_new_fd(fd);
        int flags;
        flags = mt_fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        mt_fcntl(fd, F_SETFL, flags);
        return Frame::connect(fd, address, (int)address_len, hook_fd->write_timeout_);
    }
}

ssize_t mt_read(int fd, void *buffer, size_t nbyte)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(read);
    HookFd* hook_fd = mt_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(read)(fd, buffer, nbyte);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(read)(fd, buffer, nbyte);
    }
    else
    {
        mt_new_fd(fd);
        int flags;
        flags = mt_fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        mt_fcntl(fd, F_SETFL, flags);
        return Frame::read(fd, buffer, nbyte, hook_fd->read_timeout_);
    }
}

ssize_t mt_write(int fd, const void *buffer, size_t nbyte)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(write);
    HookFd* hook_fd = mt_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(write)(fd, buffer, nbyte);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(write)(fd, buffer, nbyte);
    }
    else
    {
        mt_new_fd(fd);
        int flags;
        flags = mt_fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        mt_fcntl(fd, F_SETFL, flags);
        return Frame::write(fd, buffer, nbyte, hook_fd->write_timeout_);
    }
}

ssize_t mt_sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *dest_addr, socklen_t dest_len)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(sendto);
    HookFd* hook_fd = mt_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(sendto)(fd, buffer, length, flags, dest_addr, dest_len);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(sendto)(fd, buffer, length, flags, dest_addr, dest_len);
    }
    else
    {
        mt_new_fd(fd);
        int flags;
        flags = mt_fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        mt_fcntl(fd, F_SETFL, flags);
        return Frame::sendto(fd, buffer, (int)length, flags, dest_addr, dest_len, hook_fd->write_timeout_);
    }
}

ssize_t mt_recvfrom(int fd, void *buffer, size_t length, int flags,
                  struct sockaddr *address, socklen_t *address_len)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(recvfrom);
    HookFd* hook_fd = mt_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
    }
    else
    {
        mt_new_fd(fd);
        int flags;
        flags = mt_fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        mt_fcntl(fd, F_SETFL, flags);
        return Frame::recvfrom(fd, buffer, length, flags, address, address_len, hook_fd->read_timeout_);
    }
}

ssize_t mt_recv(int fd, void *buffer, size_t length, int flags)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(recv);
    HookFd* hook_fd = mt_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(recv)(fd, buffer, length, flags);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(recv)(fd, buffer, length, flags);
    }
    else
    {
        mt_new_fd(fd);
        int flags;
        flags = mt_fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        mt_fcntl(fd, F_SETFL, flags);
        return Frame::recv(fd, buffer, length, flags, hook_fd->read_timeout_);
    }
}

ssize_t mt_send(int fd, const void *buffer, size_t nbyte, int flags)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(send);
    HookFd* hook_fd = mt_find_fd(fd);

    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(send)(fd, buffer, nbyte, flags);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(send)(fd, buffer, nbyte, flags);
    }
    else
    {
        mt_new_fd(fd);
        int flags;
        flags = mt_fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        mt_fcntl(fd, F_SETFL, flags);
        return Frame::send(fd, buffer, nbyte, flags, hook_fd->write_timeout_);
    }
}

int mt_setsockopt(int fd, int level, int option_name, const void *option_value, 
    socklen_t option_len)
{
    LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(setsockopt);
    HookFd* hook_fd = mt_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(setsockopt)(fd, level, option_name, option_value, option_len);
    }

    if (SOL_SOCKET == level)
    {
        struct timeval *val = (struct timeval*)option_value;
        if (SO_RCVTIMEO == option_name)
        {
            hook_fd->read_timeout_ = val->tv_sec * 1000 + val->tv_usec / 1000;
        }
        else if (SO_SNDTIMEO == option_name)
        {
            hook_fd->write_timeout_ = val->tv_sec * 1000 + val->tv_usec / 1000;
        }
    }

    return REAL_FUNC(setsockopt)(fd, level, option_name, option_value, option_len);
}

int mt_fcntl(int fd, int cmd, ...)
{
    LOG_CHECK_FUNCTION;

    va_list ap;
    va_start(ap, cmd);
    void* arg = va_arg(ap, void *);
    va_end(ap);

    HOOK_SYSCALL(fcntl);
    HookFd* hook_fd = mt_find_fd(fd);
    
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(fcntl)(fd, cmd, arg);
    }

    if (cmd == F_SETFL)
    {
        va_start(ap, cmd);
        int flags = va_arg(ap, int);
        va_end(ap);

        if (flags & O_NONBLOCK)
        {
            LOG_TRACE("socket MT_FD_FLG_UNBLOCK");
            hook_fd->sock_flag_ |= MT_FD_FLG_UNBLOCK;
        }
    }

    return REAL_FUNC(fcntl)(fd, cmd, arg);
}

int mt_ioctl(int fd, uint64_t cmd, ...)
{
    LOG_CHECK_FUNCTION;

    va_list ap;
    va_start(ap, cmd);
    void* arg = va_arg(ap, void *);
    va_end(ap);

    HOOK_SYSCALL(ioctl);
    HookFd* hook_fd = mt_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(ioctl)(fd, cmd, arg);
    }

    if (cmd == FIONBIO)
    {
        int flags = (arg != NULL) ? *((int*)arg) : 0;
        if (flags != 0)
        {
            hook_fd->sock_flag_ |= MT_FD_FLG_UNBLOCK;
        }
    }

    return REAL_FUNC(ioctl)(fd, cmd, arg);
}

int mt_accept(int fd, const struct sockaddr *address, socklen_t *address_len)
{
    // LOG_CHECK_FUNCTION;

    HOOK_SYSCALL(accept);
    HookFd* hook_fd = mt_find_fd(fd);
    if (!HOOK_ACTIVE() || !hook_fd)
    {
        return REAL_FUNC(accept)(fd, address, address_len);
    }

    if (hook_fd->sock_flag_ & MT_FD_FLG_UNBLOCK)
    {
        return REAL_FUNC(accept)(fd, address, address_len);
    }

    int accept_fd = REAL_FUNC(accept)(fd, address, address_len);
    mt_new_fd(accept_fd);
    
    return accept_fd;
}