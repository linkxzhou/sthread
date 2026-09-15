/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SYS_H_
#define _ST_SYS_H_

#include "stlib/st_util.h"
#include <dlfcn.h>

// 重写系统函数
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t read_timeout : 4;
  uint32_t write_timeout : 4;
} sys_fd;

sys_fd *__find_fd(int fd);

void __new_fd(int fd);

void __free_fd(int fd);

int __socket(int domain, int type, int protocol);

int __close(int fd);

int __showdown(int fd);

int __connect(int fd, const struct sockaddr *address, socklen_t address_len);

ssize_t __read(int fd, void *buf, size_t nbyte);

ssize_t __write(int fd, const void *buf, size_t nbyte);

ssize_t __sendto(int fd, const void *message, size_t length, int flags,
                 const struct sockaddr *de__addr, socklen_t de__len);

ssize_t __recvfrom(int fd, void *buffer, size_t length, int flags,
                   struct sockaddr *address, socklen_t *address_len);

ssize_t __recv(int fd, void *buffer, size_t length, int flags);

ssize_t __send(int fd, const void *buf, size_t nbyte, int flags);

int __setsockopt(int fd, int level, int option_name, const void *option_value,
                 socklen_t option_len);

int __fcntl(int fd, int cmd, ...);

int __ioctl(int fd, uint64_t cmd, ...);

int __accept(int fd, struct sockaddr *address, socklen_t *address_len);

#ifdef __cplusplus
}
#endif

#endif
