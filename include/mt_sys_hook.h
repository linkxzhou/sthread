/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_SYS_HOOK_H_INCLUDED_
#define _MT_SYS_HOOK_H_INCLUDED_

#include <dlfcn.h>
#include "mt_utils.h"

#define RENAME_SYS_FUNC(name) name##_func

// 重写系统函数
#ifdef  __cplusplus
extern "C" {
#endif

// 为空的情况下调用系统函数
#define HOOK_SYSCALL(name)                                                  \
do  {                                                                       \
    if (!g_syscall_tab.real_##name) {                                       \
        LOG_TRACE("system func : %s", #name);                               \
        g_syscall_tab.real_##name = (name##_func)dlsym(RTLD_NEXT, #name);   \
    }                                                                       \
} while (0)

// 调用实际的系统函数
#define REAL_FUNC(name)      g_syscall_tab.real_##name
#define SET_HOOK_FLAG()      (g_hook_flag = 1)
#define UNSET_HOOK_FLAG()    (g_hook_flag = 0)
#define HOOK_ACTIVE()        (g_hook_flag == 1)

typedef int (*RENAME_SYS_FUNC(socket))(int domain, int type, int protocol);
typedef int (*RENAME_SYS_FUNC(close))(int fd);
typedef int (*RENAME_SYS_FUNC(connect))(int socket, const struct sockaddr *address, socklen_t address_len);
typedef ssize_t (*RENAME_SYS_FUNC(read))(int fildes, void *buf, size_t nbyte);
typedef ssize_t (*RENAME_SYS_FUNC(write))(int fildes, const void *buf, size_t nbyte);
typedef ssize_t (*RENAME_SYS_FUNC(sendto))(int socket, const void *message, size_t length,
                        int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
typedef ssize_t (*RENAME_SYS_FUNC(recvfrom))(int socket, void *buffer, size_t length,
	                    int flags, struct sockaddr *address, socklen_t *address_len);
typedef size_t (*RENAME_SYS_FUNC(send))(int socket, const void *buffer, size_t length, int flags);
typedef ssize_t (*RENAME_SYS_FUNC(recv))(int socket, void *buffer, size_t length, int flags);
typedef int (*RENAME_SYS_FUNC(setsockopt))(int socket, int level, int option_name,
			            const void *option_value, socklen_t option_len);
typedef int (*RENAME_SYS_FUNC(fcntl))(int fildes, int cmd, ...);
typedef int (*RENAME_SYS_FUNC(ioctl))(int fildes, int request, ... );
typedef unsigned int (*RENAME_SYS_FUNC(sleep))(unsigned int seconds);

// TODO : 暂时实现
// typedef int (*RENAME_SYS_FUNC(poll))(struct pollfd fds[], nfds_t nfds, int timeout);

typedef struct syscall_func_tab
{
    RENAME_SYS_FUNC(socket)     real_socket;
    RENAME_SYS_FUNC(close)      real_close;
    RENAME_SYS_FUNC(connect)    real_connect;
    RENAME_SYS_FUNC(read)       real_read;
    RENAME_SYS_FUNC(write)      real_write;
    RENAME_SYS_FUNC(sendto)     real_sendto;
    RENAME_SYS_FUNC(recvfrom)   real_recvfrom;
    RENAME_SYS_FUNC(send)       real_send;
    RENAME_SYS_FUNC(recv)       real_recv;
    RENAME_SYS_FUNC(setsockopt) real_setsockopt;
    RENAME_SYS_FUNC(fcntl)      real_fcntl;
    RENAME_SYS_FUNC(ioctl)      real_ioctl;
    RENAME_SYS_FUNC(sleep)      real_sleep;

    // TODO : 暂时实现
    // RENAME_SYS_FUNC(poll)       real_poll;
}SyscallFuncTab;

typedef struct hook_fd
{
    int     sock_flag_;
    int     read_timeout_;
    int     write_timeout_;
}HookFd;

unsigned long mt_get_threadid(void);

void mt_set_threadid(unsigned long threadid);

HookFd* mt_find_fd(int fd);

void mt_new_fd(int fd);

void mt_free_fd(int fd);

int mt_socket(int domain, int type, int protocol);

int mt_close(int fd);

int mt_connect(int fd, const struct sockaddr *address, socklen_t address_len);

ssize_t mt_read(int fd, void *buf, size_t nbyte);

ssize_t mt_write(int fd, const void *buf, size_t nbyte);

ssize_t mt_sendto(int fd, const void *message, size_t length, int flags,
               const struct sockaddr *dest_addr, socklen_t dest_len);

ssize_t mt_recvfrom(int fd, void *buffer, size_t length, int flags,
                  struct sockaddr *address, socklen_t *address_len);

ssize_t mt_recv(int fd, void *buffer, size_t length, int flags);

ssize_t mt_send(int fd, const void *buf, size_t nbyte, int flags);

int mt_setsockopt(int fd, int level, int option_name, const void *option_value, socklen_t option_len);

int mt_fcntl(int fd, int cmd, ...);

int mt_ioctl(int fd, uint64_t cmd, ...);

extern SyscallFuncTab   g_syscall_tab;
extern int              g_hook_flag;
extern unsigned long    g_mt_threadid;

#ifdef  __cplusplus
}
#endif

#endif
