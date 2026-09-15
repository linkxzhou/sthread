/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_sys.h"
#include <stdarg.h>

using namespace sthread;

static sys_fd g_sys_fdlist[ST_MAX_FD];

sys_fd *__find_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_MAX_FD))) {
    return NULL;
  }
  return &g_sys_fdlist[fd];
}

void __new_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_MAX_FD))) {
    return;
  }
  sys_fd *fd_info = &g_sys_fdlist[fd];
  fd_info->read_timeout = 9;  // 设置等待的ms，默认2^9ms
  fd_info->write_timeout = 9; // 设置等待的ms，默认2^9ms
}

void __free_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_MAX_FD))) {
    return;
  }
  sys_fd *fd_info = &g_sys_fdlist[fd];
  fd_info->read_timeout = 0;
  fd_info->write_timeout = 0;
}

int __socket(int domain, int type, int protocol) {
  int fd = ::socket(domain, type, protocol);
  if (fd < 0) {
    return fd;
  }
  __new_fd(fd); // 设置新的FD
  int flags;    // 默认都设置为非阻塞
  flags = __fcntl(fd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  __fcntl(fd, F_SETFL, flags);
  return fd;
}

int __close(int fd) {
  sys_fd *_fd = __find_fd(fd);
  if (!_fd) {
    return ::close(fd);
  }
  __free_fd(fd);
}

int __showdown(int fd) {
  sys_fd *_fd = __find_fd(fd);
  if (!_fd) {
    return ::shutdown(fd);
  }
  __free_fd(fd);
}

int __connect(int fd, const struct sockaddr *address, socklen_t address_len) {
  sys_fd *_fd = __find_fd(fd);
  return __connect(fd, address, (int)address_len, _fd->write_timeout);
}

ssize_t __read(int fd, void *buffer, size_t nbyte) {
  HOOK_SYSCALL(read);
  sys_fd *_fd = __find_fd(fd);

  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(read)(fd, buffer, nbyte);
  }

  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(read)(fd, buffer, nbyte);
  } else {
    return ::_read(fd, buffer, nbyte, _fd->read_timeout);
  }
}

ssize_t __write(int fd, const void *buffer, size_t nbyte) {
  HOOK_SYSCALL(write);
  sys_fd *_fd = __find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(write)(fd, buffer, nbyte);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(write)(fd, buffer, nbyte);
  } else {
    return ::_write(fd, buffer, nbyte, _fd->write_timeout);
  }
}

ssize_t __sendto(int fd, const void *buffer, size_t length, int flags,
                 const struct sockaddr *de__addr, socklen_t de__len) {
  HOOK_SYSCALL(sendto);
  sys_fd *_fd = __find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(sendto)(fd, buffer, length, flags, de__addr, de__len);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(sendto)(fd, buffer, length, flags, de__addr, de__len);
  } else {
    return ::_sendto(fd, buffer, (int)length, flags, de__addr, de__len,
                     _fd->write_timeout);
  }
}

ssize_t __recvfrom(int fd, void *buffer, size_t length, int flags,
                   struct sockaddr *address, socklen_t *address_len) {
  HOOK_SYSCALL(recvfrom);
  sys_fd *_fd = __find_fd(fd);
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

ssize_t __recv(int fd, void *buffer, size_t length, int flags) {
  HOOK_SYSCALL(recv);
  sys_fd *_fd = __find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(recv)(fd, buffer, length, flags);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(recv)(fd, buffer, length, flags);
  } else {
    return ::_recv(fd, buffer, length, flags, _fd->read_timeout);
  }
}

ssize_t __send(int fd, const void *buffer, size_t nbyte, int flags) {
  HOOK_SYSCALL(send);
  sys_fd *_fd = __find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(send)(fd, buffer, nbyte, flags);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(send)(fd, buffer, nbyte, flags);
  } else {
    return ::_send(fd, buffer, nbyte, flags, _fd->write_timeout);
  }
}

int __setsockopt(int fd, int level, int option_name, const void *option_value,
                 socklen_t option_len) {
  HOOK_SYSCALL(setsockopt);
  sys_fd *_fd = __find_fd(fd);
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

int __fcntl(int fd, int cmd, ...) {
  va_list ap;
  ::va_start(ap, cmd);
  void *arg = va_arg(ap, void *);
  ::va_end(ap);

  HOOK_SYSCALL(fcntl);
  sys_fd *_fd = __find_fd(fd);
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

int __ioctl(int fd, uint64_t cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  void *arg = va_arg(ap, void *);
  va_end(ap);

  HOOK_SYSCALL(ioctl);
  sys_fd *_fd = __find_fd(fd);
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

int __accept(int fd, struct sockaddr *address, socklen_t *address_len) {
  HOOK_SYSCALL(accept);
  sys_fd *_fd = __find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(accept)(fd, address, address_len);
  }

  int at_fd = REAL_FUNC(accept)(fd, address, address_len);
  __new_fd(at_fd);

  // 设置为非阻塞
  if (at_fd > 0) {
    int flags;
    flags = __fcntl(at_fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    __fcntl(at_fd, F_SETFL, flags);
  }

  return at_fd;
}