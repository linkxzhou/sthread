/*
 * Copyright (C) zhoulv2000@163.com
 *
 * Hook-layer (POSIX-shaped) syscalls. Timed framework APIs live in src/st_sys.h
 * as st_* (D4).
 */

#ifndef _ST_APP_SYS_HOOK_H_
#define _ST_APP_SYS_HOOK_H_

#include "stlib/st_util.h"
#include "src/st_public.h"
#include <dlfcn.h>

#define RENAME_SYS_FUNC(name) name##_func

#ifdef __cplusplus
extern "C" {
#endif

#define HOOK_SYSCALL(name)                                                     \
  do {                                                                         \
    if (!g_syscall_tab.real_##name) {                                          \
      g_syscall_tab.real_##name = (name##_func)dlsym(RTLD_NEXT, #name);        \
    }                                                                          \
  } while (0)

#define REAL_FUNC(name) g_syscall_tab.real_##name
#define SET_HOOK_FLAG() (g_hook_flag = 1)
#define UNSET_HOOK_FLAG() (g_hook_flag = 0)
#define HOOK_ACTIVE() (g_hook_flag == 1)

#ifndef ST_FD_FLG_NOUSE
#define ST_FD_FLG_NOUSE 0x0
#define ST_FD_FLG_INUSE 0x1
#define ST_FD_FLG_UNBLOCK 0x2
#endif

typedef int (*RENAME_SYS_FUNC(socket))(int domain, int type, int protocol);
typedef int (*RENAME_SYS_FUNC(close))(int fd);
typedef int (*RENAME_SYS_FUNC(connect))(int socket,
                                        const struct sockaddr *address,
                                        socklen_t address_len);
typedef ssize_t (*RENAME_SYS_FUNC(read))(int fildes, void *buf, size_t nbyte);
typedef ssize_t (*RENAME_SYS_FUNC(write))(int fildes, const void *buf,
                                          size_t nbyte);
typedef ssize_t (*RENAME_SYS_FUNC(sendto))(int socket, const void *message,
                                           size_t length, int flags,
                                           const struct sockaddr *dest_addr,
                                           socklen_t dest_len);
typedef ssize_t (*RENAME_SYS_FUNC(recvfrom))(int socket, void *buffer,
                                             size_t length, int flags,
                                             struct sockaddr *address,
                                             socklen_t *address_len);
typedef ssize_t (*RENAME_SYS_FUNC(send))(int socket, const void *buffer,
                                         size_t length, int flags);
typedef ssize_t (*RENAME_SYS_FUNC(recv))(int socket, void *buffer,
                                         size_t length, int flags);
typedef int (*RENAME_SYS_FUNC(setsockopt))(int socket, int level,
                                           int option_name,
                                           const void *option_value,
                                           socklen_t option_len);
typedef int (*RENAME_SYS_FUNC(fcntl))(int fildes, int cmd, ...);
typedef int (*RENAME_SYS_FUNC(ioctl))(int fildes, int request, ...);
typedef unsigned int (*RENAME_SYS_FUNC(sleep))(unsigned int seconds);
typedef int (*RENAME_SYS_FUNC(accept))(int socket,
                                       const struct sockaddr *address,
                                       socklen_t *address_len);

typedef struct {
  RENAME_SYS_FUNC(socket) real_socket;
  RENAME_SYS_FUNC(close) real_close;
  RENAME_SYS_FUNC(connect) real_connect;
  RENAME_SYS_FUNC(read) real_read;
  RENAME_SYS_FUNC(write) real_write;
  RENAME_SYS_FUNC(sendto) real_sendto;
  RENAME_SYS_FUNC(recvfrom) real_recvfrom;
  RENAME_SYS_FUNC(send) real_send;
  RENAME_SYS_FUNC(recv) real_recv;
  RENAME_SYS_FUNC(setsockopt) real_setsockopt;
  RENAME_SYS_FUNC(fcntl) real_fcntl;
  RENAME_SYS_FUNC(ioctl) real_ioctl;
  RENAME_SYS_FUNC(sleep) real_sleep;
  RENAME_SYS_FUNC(accept) real_accept;
} SyscallCallbackTab;

typedef struct {
  int sock_flag;
  int read_timeout;  /* log2 ms scale historically; see sys_new_fd */
  int write_timeout;
} sys_fd;

extern SyscallCallbackTab g_syscall_tab;
extern int g_hook_flag;

sys_fd *sys_find_fd(int fd);
void sys_new_fd(int fd);
void sys_free_fd(int fd);

int sys_socket(int domain, int type, int protocol);
int sys_close(int fd);
int sys_shutdown(int fd);
int sys_connect(int fd, const struct sockaddr *address, socklen_t address_len);
ssize_t sys_read(int fd, void *buf, size_t nbyte);
ssize_t sys_write(int fd, const void *buf, size_t nbyte);
ssize_t sys_sendto(int fd, const void *message, size_t length, int flags,
                   const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t sys_recvfrom(int fd, void *buffer, size_t length, int flags,
                     struct sockaddr *address, socklen_t *address_len);
ssize_t sys_recv(int fd, void *buffer, size_t length, int flags);
ssize_t sys_send(int fd, const void *buf, size_t nbyte, int flags);
int sys_setsockopt(int fd, int level, int option_name, const void *option_value,
                   socklen_t option_len);
int sys_fcntl(int fd, int cmd, ...);
int sys_ioctl(int fd, uint64_t cmd, ...);
int sys_accept(int fd, struct sockaddr *address, socklen_t *address_len);

#ifdef __cplusplus
}
#endif

#endif /* _ST_APP_SYS_HOOK_H_ */
