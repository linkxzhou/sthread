/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_sys.h"
#include "st_manager.h"
#include <stdarg.h>

using namespace sthread;

SyscallCallback g_syscall;
int g_syscall_flag;

static SyscallFd g_syscallfd_tab[ST_HOOK_MAX_FD];

SyscallFd *st_find_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_HOOK_MAX_FD))) {
    return NULL;
  }
  SyscallFd *fd_info = &g_syscallfd_tab[fd];
  if (fd_info->sock_flag & ST_FD_FLG_INUSE) {
    return fd_info;
  } else {
    return NULL;
  }
}

void st_new_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_HOOK_MAX_FD))) {
    return;
  }
  SyscallFd *fd_info = &g_syscallfd_tab[fd];
  fd_info->sock_flag = ST_FD_FLG_INUSE;
  fd_info->read_timeout = 500;  // 设置等待的ms，默认500ms
  fd_info->write_timeout = 500; // 设置等待的ms，默认500ms
}

void st_free_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_HOOK_MAX_FD))) {
    return;
  }
  SyscallFd *fd_info = &g_syscallfd_tab[fd];
  fd_info->sock_flag = ST_FD_FLG_NOUSE;
  fd_info->read_timeout = 0;
  fd_info->write_timeout = 0;
}

int st_socket(int domain, int type, int protocol) {
  HOOK_SYSCALL(socket);
  int fd = REAL_FUNC(socket)(domain, type, protocol);
  if (fd < 0) {
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

int st_close(int fd) {
  HOOK_SYSCALL(close);
  SyscallFd *_fd = st_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(close)(fd);
  }
  st_free_fd(fd);
  return REAL_FUNC(close)(fd);
}

int st_showdown(int fd) {
  HOOK_SYSCALL(shutdown);
  SyscallFd *_fd = st_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(shutdown)(fd);
  }
  st_free_fd(fd);
  return REAL_FUNC(shutdown)(fd);
}

int st_connect(int fd, const struct sockaddr *address, socklen_t address_len) {
  HOOK_SYSCALL(connect);
  SyscallFd *_fd = st_find_fd(fd);

  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(connect)(fd, address, address_len);
  }

  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(connect)(fd, address, address_len);
  } else {
    return ::_connect(fd, address, (int)address_len, _fd->write_timeout);
  }
}

ssize_t st_read(int fd, void *buffer, size_t nbyte) {
  HOOK_SYSCALL(read);
  SyscallFd *_fd = st_find_fd(fd);

  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(read)(fd, buffer, nbyte);
  }

  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(read)(fd, buffer, nbyte);
  } else {
    return ::_read(fd, buffer, nbyte, _fd->read_timeout);
  }
}

ssize_t st_write(int fd, const void *buffer, size_t nbyte) {
  HOOK_SYSCALL(write);
  SyscallFd *_fd = st_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(write)(fd, buffer, nbyte);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(write)(fd, buffer, nbyte);
  } else {
    return ::_write(fd, buffer, nbyte, _fd->write_timeout);
  }
}

ssize_t st_sendto(int fd, const void *buffer, size_t length, int flags,
                  const struct sockaddr *dest_addr, socklen_t dest_len) {
  HOOK_SYSCALL(sendto);
  SyscallFd *_fd = st_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(sendto)(fd, buffer, length, flags, dest_addr, dest_len);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(sendto)(fd, buffer, length, flags, dest_addr, dest_len);
  } else {
    return ::_sendto(fd, buffer, (int)length, flags, dest_addr, dest_len,
                     _fd->write_timeout);
  }
}

ssize_t st_recvfrom(int fd, void *buffer, size_t length, int flags,
                    struct sockaddr *address, socklen_t *address_len) {
  HOOK_SYSCALL(recvfrom);
  SyscallFd *_fd = st_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
  }
  if (hook_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
  } else {
    return ::_recvfrom(fd, buffer, length, flags, address, address_len,
                       _fd->read_timeout);
  }
}

ssize_t st_recv(int fd, void *buffer, size_t length, int flags) {
  HOOK_SYSCALL(recv);
  SyscallFd *_fd = st_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(recv)(fd, buffer, length, flags);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(recv)(fd, buffer, length, flags);
  } else {
    return ::_recv(fd, buffer, length, flags, _fd->read_timeout);
  }
}

ssize_t st_send(int fd, const void *buffer, size_t nbyte, int flags) {
  HOOK_SYSCALL(send);
  SyscallFd *_fd = st_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(send)(fd, buffer, nbyte, flags);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(send)(fd, buffer, nbyte, flags);
  } else {
    return ::_send(fd, buffer, nbyte, flags, _fd->write_timeout);
  }
}

int st_setsockopt(int fd, int level, int option_name, const void *option_value,
                  socklen_t option_len) {
  HOOK_SYSCALL(setsockopt);
  SyscallFd *_fd = st_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(setsockopt)(fd, level, option_name, option_value,
                                 option_len);
  }
  if (SOL_SOCKET == level) {
    struct timeval *val = (struct timeval *)option_value;
    if (SO_RCVTIMEO == option_name) {
      _fd->read_timeout = val->tv_sec * 1000 + val->tv_usec / 1000;
    } else if (SO_SNDTIMEO == option_name) {
      _fd->write_timeout = val->tv_sec * 1000 + val->tv_usec / 1000;
    }
  }
  return REAL_FUNC(setsockopt)(fd, level, option_name, option_value,
                               option_len);
}

int st_fcntl(int fd, int cmd, ...) {
  va_list ap;
  ::va_start(ap, cmd);
  void *arg = va_arg(ap, void *);
  ::va_end(ap);

  HOOK_SYSCALL(fcntl);
  SyscallFd *_fd = st_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(fcntl)(fd, cmd, arg);
  }

  if (cmd == F_SETFL) {
    ::va_start(ap, cmd);
    int flags = va_arg(ap, int);
    ::va_end(ap);

    if (flags & O_NONBLOCK) {
      _fd->sock_flag |= ST_FD_FLG_UNBLOCK;
    }
  }

  return REAL_FUNC(fcntl)(fd, cmd, arg);
}

int st_ioctl(int fd, uint64_t cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  void *arg = va_arg(ap, void *);
  va_end(ap);

  HOOK_SYSCALL(ioctl);
  SyscallFd *_fd = st_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(ioctl)(fd, cmd, arg);
  }

  if (cmd == FIONBIO) {
    int flags = (arg != NULL) ? *((int *)arg) : 0;
    if (flags != 0) {
      _fd->sock_flag |= ST_FD_FLG_UNBLOCK;
    }
  }

  return REAL_FUNC(ioctl)(fd, cmd, arg);
}

int st_accept(int fd, struct sockaddr *address, socklen_t *address_len) {
  HOOK_SYSCALL(accept);
  SyscallFd *_fd = st_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(accept)(fd, address, address_len);
  }

  int at_fd = REAL_FUNC(accept)(fd, address, address_len);
  st_new_fd(at_fd);

  // 设置为非阻塞
  if (at_fd > 0) {
    int flags;
    flags = st_fcntl(at_fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    st_fcntl(at_fd, F_SETFL, flags);
  }

  return at_fd;
}