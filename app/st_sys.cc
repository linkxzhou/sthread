/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_sys.h"
#include "src/st_sys.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/socket.h>

SyscallCallbackTab g_syscall_tab;
int g_hook_flag = 0;

using namespace sthread;

/* st_* 用 -2/-3 表示内部状态。hook 出去的 read/recv/send/connect
 * 必须像 libc：失败一律 -1，errno 已经由 st_* 设好（超时为 ETIME）。 */
static ssize_t hook_like_libc(ssize_t rc) {
  if (rc >= -1) {
    return rc;
  }
  if (errno == 0) {
    errno = (rc == -2) ? EINVAL : EIO;
  }
  return -1;
}

static sys_fd g_sys_fdlist[ST_MAX_FD];

sys_fd *sys_find_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_MAX_FD))) {
    return NULL;
  }
  sys_fd *fd_info = &g_sys_fdlist[fd];
  /* Unregistered slots must not look "found" — callers treat NULL as
   * "use real syscall". */
  if (!(fd_info->sock_flag & ST_FD_FLG_INUSE)) {
    return NULL;
  }
  return fd_info;
}

void sys_new_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_MAX_FD))) {
    return;
  }
  sys_fd *fd_info = &g_sys_fdlist[fd];
  fd_info->sock_flag = ST_FD_FLG_INUSE;
  /* Default timeouts in milliseconds (not log2). */
  fd_info->read_timeout = 512;
  fd_info->write_timeout = 512;
  fd_info->hook_item = NULL;
}

void sys_free_fd(int fd) {
  if (unlikely((fd < 0) || (fd >= ST_MAX_FD))) {
    return;
  }
  sys_fd *fd_info = &g_sys_fdlist[fd];
  fd_info->sock_flag = ST_FD_FLG_NOUSE;
  fd_info->read_timeout = 0;
  fd_info->write_timeout = 0;
  fd_info->hook_item = NULL;
}

/* 内核非阻塞用 ::fcntl，不走 sys_fcntl，避免打上 ST_FD_FLG_UNBLOCK。
 * 用户自己的 fcntl(O_NONBLOCK) / ioctl(FIONBIO) 仍会打上这个标志。 */
static void kernel_nonblock(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    flags = 0;
  }
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* fd 已在表里、hook 已打开、用户没要求非阻塞、事件表还没有这项时，
 * hook 自己登记。所有权记在 sys_fd.hook_item，sys_close 只释放这一项。 */
static void hook_ensure_item(int fd) {
  StEventSchedule *sched = GlobalEventSchedule();
  sys_fd *info = sys_find_fd(fd);
  StEventItem *item;
  if (sched == NULL || info == NULL) {
    return;
  }
  if (sched->GetEventItem(fd) != NULL) {
    return;
  }
  item = stlib::Instance<stlib::UtilPtrPool<StEventItem> >()->AllocPtr();
  if (item == NULL) {
    return;
  }
  item->SetOsfd(fd);
  /* 先只占事件表，不打开兴趣。否则 fd 上已有数据、又没有 owner 时，
   * daemon 会在 epoll 上忙转。真正等待时由 st_read / st_connect 再打开。 */
  item->DisableInput();
  item->DisableOutput();
  if (!sched->Add(item)) {
    stlib::UtilPtrPoolFree(item);
    return;
  }
  info->hook_item = item;
}

static void hook_release_item(int fd, sys_fd *info) {
  StEventItem *item;
  StEventSchedule *sched;
  if (info == NULL || info->hook_item == NULL) {
    return;
  }
  item = (StEventItem *)info->hook_item;
  sched = GlobalEventSchedule();
  if (sched != NULL && sched->GetEventItem(fd) == item) {
    sched->ClearItem(item);
  }
  stlib::UtilPtrPoolFree(item);
  info->hook_item = NULL;
}

int sys_socket(int domain, int type, int protocol) {
  int fd = ::socket(domain, type, protocol);
  if (fd < 0) {
    return fd;
  }
  sys_new_fd(fd); // 设置新的FD
  /* 库自己把内核 fd 设成非阻塞，不能当成用户要求的 O_NONBLOCK。
   * 否则 sys_read 会直接走真实 read，协程不 Yield。 */
  kernel_nonblock(fd);
  return fd;
}

int sys_close(int fd) {
  sys_fd *_fd = sys_find_fd(fd);
  if (_fd) {
    /* 只释放 hook 自己登记的项。Create 登记的项仍由 ReleaseItem 归还。 */
    hook_release_item(fd, _fd);
    sys_free_fd(fd);
  }
  /* close 会让内核摘掉 epoll/kqueue 登记，但 StIOState 的 per-fd mask
   * 还在。fd 号一复用，AddEvent 就 MOD 一个不存在的登记（ENOENT），
   * 或因 mask 碰巧相同而直接返回。先把缓存清掉。 */
  if (GlobalEventSchedule() != NULL) {
    GlobalEventSchedule()->ClearOsfd(fd);
  }
  HOOK_SYSCALL(close);
  if (!HAS_REAL(close)) {
    return ::close(fd);
  }
  return REAL_FUNC(close)(fd);
}

int sys_shutdown(int fd) {
  sys_fd *_fd = sys_find_fd(fd);
  if (_fd) {
    sys_free_fd(fd);
  }
  return ::shutdown(fd, SHUT_RDWR);
}

int sys_connect(int fd, const struct sockaddr *address, socklen_t address_len) {
  sys_fd *_fd = sys_find_fd(fd);
  if (!_fd) {
    HOOK_SYSCALL(connect);
    if (!HAS_REAL(connect)) {
      errno = ENOSYS;
      return -1;
    }
    return REAL_FUNC(connect)(fd, address, address_len);
  }
  if (HOOK_ACTIVE() && !(_fd->sock_flag & ST_FD_FLG_UNBLOCK)) {
    hook_ensure_item(fd);
  }
  return (int)hook_like_libc(
      st_connect(fd, address, (int)address_len, _fd->write_timeout));
}

ssize_t sys_read(int fd, void *buffer, size_t nbyte) {
  HOOK_SYSCALL(read);
  if (!HAS_REAL(read)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);

  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(read)(fd, buffer, nbyte);
  }

  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(read)(fd, buffer, nbyte);
  }
  hook_ensure_item(fd);
  return hook_like_libc(st_read(fd, buffer, nbyte, _fd->read_timeout));
}

ssize_t sys_write(int fd, const void *buffer, size_t nbyte) {
  HOOK_SYSCALL(write);
  if (!HAS_REAL(write)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(write)(fd, buffer, nbyte);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(write)(fd, buffer, nbyte);
  }
  hook_ensure_item(fd);
  return hook_like_libc(st_write(fd, buffer, nbyte, _fd->write_timeout));
}

ssize_t sys_sendto(int fd, const void *buffer, size_t length, int flags,
                   const struct sockaddr *de__addr, socklen_t de__len) {
  HOOK_SYSCALL(sendto);
  if (!HAS_REAL(sendto)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(sendto)(fd, buffer, length, flags, de__addr, de__len);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(sendto)(fd, buffer, length, flags, de__addr, de__len);
  } else {
    return hook_like_libc(st_sendto(fd, buffer, (int)length, flags, de__addr,
                                   de__len, _fd->write_timeout));
  }
}

ssize_t sys_recvfrom(int fd, void *buffer, size_t length, int flags,
                     struct sockaddr *address, socklen_t *address_len) {
  HOOK_SYSCALL(recvfrom);
  if (!HAS_REAL(recvfrom)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(recvfrom)(fd, buffer, length, flags, address, address_len);
  } else {
    return hook_like_libc(st_recvfrom(fd, buffer, length, flags, address,
                                     address_len, _fd->read_timeout));
  }
}

ssize_t sys_recv(int fd, void *buffer, size_t length, int flags) {
  HOOK_SYSCALL(recv);
  if (!HAS_REAL(recv)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(recv)(fd, buffer, length, flags);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(recv)(fd, buffer, length, flags);
  }
  hook_ensure_item(fd);
  return hook_like_libc(st_recv(fd, buffer, length, flags, _fd->read_timeout));
}

ssize_t sys_send(int fd, const void *buffer, size_t nbyte, int flags) {
  HOOK_SYSCALL(send);
  if (!HAS_REAL(send)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!HOOK_ACTIVE() || !_fd) {
    return REAL_FUNC(send)(fd, buffer, nbyte, flags);
  }
  if (_fd->sock_flag & ST_FD_FLG_UNBLOCK) {
    return REAL_FUNC(send)(fd, buffer, nbyte, flags);
  }
  hook_ensure_item(fd);
  return hook_like_libc(st_send(fd, buffer, nbyte, flags, _fd->write_timeout));
}

int sys_setsockopt(int fd, int level, int option_name, const void *option_value,
                   socklen_t option_len) {
  HOOK_SYSCALL(setsockopt);
  if (!HAS_REAL(setsockopt)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
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

int sys_fcntl(int fd, int cmd, ...) {
  va_list ap;
  ::va_start(ap, cmd);
  void *arg = va_arg(ap, void *);
  ::va_end(ap);

  HOOK_SYSCALL(fcntl);
  if (!HAS_REAL(fcntl)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(fcntl)(fd, cmd, arg);
  }

  if (cmd == F_SETFL) {
    ::va_start(ap, cmd);
    int flags = va_arg(ap, int);
    ::va_end(ap);

    if (flags & O_NONBLOCK) {
      _fd->sock_flag |= ST_FD_FLG_UNBLOCK | ST_FD_FLG_INUSE;
    }
  }

  return REAL_FUNC(fcntl)(fd, cmd, arg);
}

int sys_ioctl(int fd, uint64_t cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  void *arg = va_arg(ap, void *);
  va_end(ap);

  HOOK_SYSCALL(ioctl);
  if (!HAS_REAL(ioctl)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(ioctl)(fd, cmd, arg);
  }

  if (cmd == FIONBIO) {
    int flags = (arg != NULL) ? *((int *)arg) : 0;
    if (flags != 0) {
      _fd->sock_flag |= ST_FD_FLG_UNBLOCK | ST_FD_FLG_INUSE;
    }
  }

  return REAL_FUNC(ioctl)(fd, cmd, arg);
}

int sys_accept(int fd, struct sockaddr *address, socklen_t *address_len) {
  HOOK_SYSCALL(accept);
  if (!HAS_REAL(accept)) {
    errno = ENOSYS;
    return -1;
  }
  sys_fd *_fd = sys_find_fd(fd);
  if (!_fd) {
    return REAL_FUNC(accept)(fd, address, address_len);
  }

  int at_fd = REAL_FUNC(accept)(fd, address, address_len);
  sys_new_fd(at_fd);

  // 设置为非阻塞
  if (at_fd > 0) {
    int flags;
    flags = sys_fcntl(at_fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    sys_fcntl(at_fd, F_SETFL, flags);
  }

  return at_fd;
}