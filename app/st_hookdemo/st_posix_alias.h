/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 只给 makefile 的 -include 用。业务 .c 自己不要 include 这个头。
 * 先拉完系统头，再把 POSIX 名字换成 sys_*，避免宏污染 libc 声明。
 */

#ifndef _ST_POSIX_ALIAS_H_
#define _ST_POSIX_ALIAS_H_

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

int sys_socket(int domain, int type, int protocol);
int sys_connect(int fd, const struct sockaddr *address, socklen_t address_len);
ssize_t sys_read(int fd, void *buf, size_t nbyte);
ssize_t sys_write(int fd, const void *buf, size_t nbyte);
int sys_close(int fd);
int sys_setsockopt(int fd, int level, int option_name, const void *option_value,
                   socklen_t option_len);

#ifdef __cplusplus
}
#endif

#define socket sys_socket
#define connect sys_connect
#define read sys_read
#define write sys_write
#define close sys_close
#define setsockopt sys_setsockopt

#endif
