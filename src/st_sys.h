/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SYS_H_
#define _ST_SYS_H_

#include <dlfcn.h>
#include "stlib/st_util.h"

#define RENAME_SYS_FUNC(name) name##_func

// 重写系统函数
#ifdef  __cplusplus
extern "C" 
{
#endif

// 为空的情况下调用系统函数
#define HOOK_SYSCALL(name) do                       \
    {                                               \
        if (unlikely(!g_syscall.real_##name))       \
        {                                           \
            g_syscall.real_##name = (name##_func)dlsym(RTLD_NEXT, #name);   \
        }                                                                   \
    } while (0)

// 调用实际的系统函数
#define REAL_FUNC(name)      g_syscall.real_##name
#define SET_HOOK_FLAG()      (g_syscall_flag = 1)
#define UNSET_HOOK_FLAG()    (g_syscall_flag = 0)
#define HOOK_ACTIVE()        (g_syscall_flag == 1)

typedef int (*RENAME_SYS_FUNC(socket))(int domain, int type, int protocol);

typedef int (*RENAME_SYS_FUNC(close))(int fd);

typedef int (*RENAME_SYS_FUNC(shutdown))(int fd);

typedef int (*RENAME_SYS_FUNC(connect))(int socket, 
        const struct sockaddr *address, socklen_t address_len);

typedef ssize_t (*RENAME_SYS_FUNC(read))(int fildes, void *buf, size_t nbyte);

typedef ssize_t (*RENAME_SYS_FUNC(write))(int fildes, const void *buf, size_t nbyte);

typedef ssize_t (*RENAME_SYS_FUNC(sendto))(int socket, const void *message, size_t length,
        int flags, const struct sockaddr *dest_addr, socklen_t dest_len);

typedef ssize_t (*RENAME_SYS_FUNC(recvfrom))(int socket, void *buffer, size_t length,
	    int flags, struct sockaddr *address, socklen_t *address_len);

typedef size_t (*RENAME_SYS_FUNC(send))(int socket, const void *buffer, 
        size_t length, int flags);

typedef ssize_t (*RENAME_SYS_FUNC(recv))(int socket, void *buffer, 
        size_t length, int flags);

typedef int (*RENAME_SYS_FUNC(setsockopt))(int socket, int level, int option_name,
		const void *option_value, socklen_t option_len);

typedef int (*RENAME_SYS_FUNC(fcntl))(int fildes, int cmd, ...);

typedef int (*RENAME_SYS_FUNC(ioctl))(int fildes, int request, ... );

typedef int (*RENAME_SYS_FUNC(sleep))(int seconds);

typedef int (*RENAME_SYS_FUNC(accept))(int socket, const struct sockaddr *address, 
        socklen_t *address_len);

typedef struct
{
    RENAME_SYS_FUNC(socket)     real_socket;
    RENAME_SYS_FUNC(close)      real_close;
    RENAME_SYS_FUNC(shutdown)   real_shutdown;
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
    RENAME_SYS_FUNC(accept)     real_accept;
} SyscallCallback;

typedef struct
{
    int sock_flag;
    int read_timeout;
    int write_timeout;
} SyscallFd;

extern SyscallCallback      g_syscall;
extern int                  g_syscall_flag;
extern uint64_t             g_st_threadid;

SyscallFd* st_find_fd(int fd);

void st_new_fd(int fd);

void st_free_fd(int fd);

int st_socket(int domain, int type, int protocol);

int st_close(int fd);

int st_showdown(int fd);

int st_connect(int fd, const struct sockaddr *address, 
    socklen_t address_len);

ssize_t st_read(int fd, void *buf, size_t nbyte);

ssize_t st_write(int fd, const void *buf, size_t nbyte);

ssize_t st_sendto(int fd, const void *message, size_t length, int flags,
    const struct sockaddr *dest_addr, socklen_t dest_len);

ssize_t st_recvfrom(int fd, void *buffer, size_t length, int flags,
    struct sockaddr *address, socklen_t *address_len);

ssize_t st_recv(int fd, void *buffer, size_t length, int flags);

ssize_t st_send(int fd, const void *buf, size_t nbyte, int flags);

int st_setsockopt(int fd, int level, int option_name, 
    const void *option_value, socklen_t option_len);

int st_fcntl(int fd, int cmd, ...);

int st_ioctl(int fd, uint64_t cmd, ...);

int st_accept(int fd, struct sockaddr *address, socklen_t *address_len);

#ifdef  __cplusplus
}
#endif

#endif
