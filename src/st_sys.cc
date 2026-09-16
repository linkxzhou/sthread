#include "app/st_sys.h"
#include "st_sys.h"

using namespace sthread;

namespace {

int NormalizeTimeoutMs(int timeout) {
  return (timeout <= -1) ? 0x7fffffff : timeout;
}

/* C1: 统一 fd 等待骨架。
 * 返回 0 就绪；-1 超时（errno=ETIME）；-2 无 item；-3 Schedule 失败。 */
int WaitFdReady(StThreadItem *thread, int fd, bool want_read, int64_t start,
                int timeout) {
  int64_t now = Util::TimeMs();
  if ((int)(now - start) > timeout) {
    errno = ETIME;
    return -1;
  }

  StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
  if (item == NULL) {
    LOG_ERROR("item is NULL, fd: %d", fd);
    return -2;
  }

  if (want_read) {
    item->DisableOutput();
    item->EnableInput();
  } else {
    item->DisableInput();
    item->EnableOutput();
  }
  item->SetOwnerThread(thread);

  int64_t wakeup_timeout = timeout + Util::TimeMs();
  if (!(GlobalEventSchedule()->Schedule(thread, NULL, item, wakeup_timeout))) {
    LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
              strerror(errno));
    /* Do not UtilPtrPoolFree(item): it is the live GetEventItem(fd). */
    return -3;
  }
  return 0;
}

StThreadItem *RequireActiveThread() {
  StThreadItem *thread = GlobalThreadSchedule()->GetActiveThread();
  if (thread == NULL) {
    LOG_ERROR("active thread is NULL");
    errno = EINVAL;
  }
  return thread;
}

} // namespace

int st_sendto(int fd, const void *msg, int len, int flags,
              const struct sockaddr *to, int tolen, int timeout) {
  HOOK_SYSCALL(sendto);
  if (!HAS_REAL(sendto)) {
    LOG_ERROR("dlsym(sendto) failed");
    errno = ENOSYS;
    return -1;
  }
  int64_t start = Util::TimeMs();
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  timeout = NormalizeTimeoutMs(timeout);

  int n = 0;
  while ((n = REAL_FUNC(sendto)(fd, msg, (size_t)len, flags, to,
                                (socklen_t)tolen)) < 0) {
    if ((int)(Util::TimeMs() - start) > timeout) {
      errno = ETIME;
      return -1;
    }
    if (errno == EINTR) {
      continue;
    }
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      LOG_ERROR("sendto failed, errno: %d, strerr : %s", errno,
                strerror(errno));
      return -1;
    }
    int wr = WaitFdReady(thread, fd, false, start, timeout);
    if (wr < 0) {
      return wr;
    }
  }

  return n;
}

int st_recvfrom(int fd, void *buf, int len, int flags, struct sockaddr *from,
                socklen_t *fromlen, int timeout) {
  HOOK_SYSCALL(recvfrom);
  if (!HAS_REAL(recvfrom)) {
    LOG_ERROR("dlsym(recvfrom) failed");
    errno = ENOSYS;
    return -1;
  }
  int64_t start = Util::TimeMs();
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  timeout = NormalizeTimeoutMs(timeout);

  while (true) {
    int wr = WaitFdReady(thread, fd, true, start, timeout);
    if (wr < 0) {
      return wr;
    }

    int n = REAL_FUNC(recvfrom)(fd, buf, (size_t)len, flags, from, fromlen);
    LOG_TRACE("recvfrom return n: %d, buf: %p, fd: %d, len: %d, flags: %d", n,
              buf, fd, len, flags);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        LOG_ERROR("recvfrom failed, errno: %d", errno);
        return -1;
      }
      continue;
    }
    if (n == 0) {
      /* D3/B16: 历史语义当对端关闭；UDP 零长包合法但保持兼容 */
      LOG_ERROR("[n=0]recvfrom failed, errno: %d", errno);
      return 0;
    }
    return n;
  }
}

int st_connect(int fd, const struct sockaddr *addr, int addrlen, int timeout) {
  int64_t start = Util::TimeMs();
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  timeout = NormalizeTimeoutMs(timeout);

  /* Must call the real syscall, not sys_connect (that re-enters st_connect). */
  HOOK_SYSCALL(connect);
  if (!HAS_REAL(connect)) {
    LOG_ERROR("dlsym(connect) failed");
    errno = ENOSYS;
    return -1;
  }
  int n = 0;
  while ((n = REAL_FUNC(connect)(fd, addr, (socklen_t)addrlen)) < 0) {
    LOG_TRACE("connect n: %d, errno: %d, strerror: %s", n, errno,
              strerror(errno));
    if ((int)(Util::TimeMs() - start) > timeout) {
      errno = ETIME;
      return -1;
    }
    if (errno == EISCONN) {
      LOG_WARN("errno = EISCONN");
      return 0;
    }
    if (errno == EINTR) {
      continue;
    }
    if (!((errno == EAGAIN) || (errno == EINPROGRESS))) {
      LOG_ERROR("connect failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      return -1;
    }
    int wr = WaitFdReady(thread, fd, false, start, timeout);
    if (wr < 0) {
      return wr;
    }
  }

  return n;
}

ssize_t st_read(int fd, void *buf, size_t nbyte, int timeout) {
  HOOK_SYSCALL(read);
  if (!HAS_REAL(read)) {
    LOG_ERROR("dlsym(read) failed");
    errno = ENOSYS;
    return -1;
  }
  int64_t start = Util::TimeMs();
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  timeout = NormalizeTimeoutMs(timeout);

  ssize_t n = 0;
  while ((n = REAL_FUNC(read)(fd, buf, nbyte)) < 0) {
    if ((int)(Util::TimeMs() - start) > timeout) {
      errno = ETIME;
      return -1;
    }
    if (errno == EINTR) {
      continue;
    }
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      LOG_ERROR("read failed, errno: %d", errno);
      return -1;
    }
    int wr = WaitFdReady(thread, fd, true, start, timeout);
    if (wr < 0) {
      return wr;
    }
  }

  return n;
}

ssize_t st_write(int fd, const void *buf, size_t nbyte, int timeout) {
  HOOK_SYSCALL(write);
  if (!HAS_REAL(write)) {
    LOG_ERROR("dlsym(write) failed");
    errno = ENOSYS;
    return -1;
  }
  int64_t start = Util::TimeMs();
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  timeout = NormalizeTimeoutMs(timeout);

  ssize_t n = 0;
  size_t send_len = 0;
  while (send_len < nbyte) {
    if ((int)(Util::TimeMs() - start) > timeout) {
      errno = ETIME;
      return -1;
    }

    n = REAL_FUNC(write)(fd, (char *)buf + send_len, nbyte - send_len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        LOG_ERROR("write failed, errno: %d", errno);
        return -1;
      }
    } else if (n == 0) {
      LOG_ERROR("[n=0]write failed, errno: %d", errno);
      return 0;
    } else {
      send_len += n;
      if (send_len >= nbyte) {
        return nbyte;
      }
    }

    int wr = WaitFdReady(thread, fd, false, start, timeout);
    if (wr < 0) {
      return wr;
    }
  }

  return nbyte;
}

int st_recv(int fd, void *buf, int len, int flags, int timeout) {
  HOOK_SYSCALL(recv);
  if (!HAS_REAL(recv)) {
    LOG_ERROR("dlsym(recv) failed");
    errno = ENOSYS;
    return -1;
  }
  int64_t start = Util::TimeMs();
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  LOG_TRACE("---------- [name: %s] -----------", thread->GetName());
  timeout = NormalizeTimeoutMs(timeout);

  while (true) {
    int wr = WaitFdReady(thread, fd, true, start, timeout);
    if (wr < 0) {
      return wr;
    }

    int n = REAL_FUNC(recv)(fd, buf, (size_t)len, flags);
    LOG_TRACE("recv return n: %d, buf: %p, fd: %d, len: %d, flags: %d", n, buf,
              fd, len, flags);
    if (n < 0) {
      LOG_ERROR("recv failed, errno: %d, strerr: %s", errno, strerror(errno));
      if (errno == EINTR) {
        continue;
      }
      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        return -1;
      }
      continue;
    }
    if (n == 0) {
      LOG_ERROR("[n=0]recv failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      return 0;
    }
    return n;
  }
}

ssize_t st_send(int fd, const void *buf, size_t nbyte, int flags, int timeout) {
  HOOK_SYSCALL(send);
  if (!HAS_REAL(send)) {
    LOG_ERROR("dlsym(send) failed");
    errno = ENOSYS;
    return -1;
  }
  int64_t start = Util::TimeMs();
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  timeout = NormalizeTimeoutMs(timeout);

  ssize_t n = 0;
  size_t send_len = 0;
  while (send_len < nbyte) {
    if ((int)(Util::TimeMs() - start) > timeout) {
      errno = ETIME;
      return -1;
    }

    n = REAL_FUNC(send)(fd, (char *)buf + send_len, nbyte - send_len, flags);
    LOG_TRACE("send fd: %d, nbyte: %d, send_len: %d, flags: %d, n: %d", fd,
              (int)nbyte, (int)send_len, flags, (int)n);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        LOG_ERROR("write failed, errno: %d, strerr: %s", errno,
                  strerror(errno));
        return -1;
      }
    } else if (n == 0) {
      LOG_ERROR("[n=0]write failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      return 0;
    } else {
      send_len += n;
      if (send_len >= nbyte) {
        LOG_TRACE("send_len : %d", (int)send_len);
        return nbyte;
      }
    }

    int wr = WaitFdReady(thread, fd, false, start, timeout);
    if (wr < 0) {
      return wr;
    }
  }

  return nbyte;
}

void st_sleep(int ms) {
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());
  if (thread != NULL) {
    thread->Sleep(ms);
    GlobalThreadSchedule()->Sleep(thread);
  }
}

int st_accept(int fd, struct sockaddr *addr, socklen_t *addrlen) {
  HOOK_SYSCALL(accept);
  if (!HAS_REAL(accept)) {
    LOG_ERROR("dlsym(accept) failed");
    errno = ENOSYS;
    return -1;
  }
  StThreadItem *thread = RequireActiveThread();
  if (thread == NULL) {
    return -1;
  }

  int64_t start = Util::TimeMs();
  int timeout = 0x7fffffff;
  int connfd = -1;
  while ((connfd = REAL_FUNC(accept)(fd, addr, addrlen)) < 0) {
    if (errno == EINTR) {
      continue;
    }
    if (!((errno == EAGAIN) || (errno == EINPROGRESS))) {
      LOG_ERROR("accept failed, errno: %d, strerr: %s", errno, strerror(errno));
      return -1;
    }
    int wr = WaitFdReady(thread, fd, true, start, timeout);
    if (wr < 0) {
      return wr;
    }
  }

  return connfd;
}
