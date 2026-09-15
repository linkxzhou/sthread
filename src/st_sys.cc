#include "app/st_sys.h"
#include "st_sys.h"

using namespace sthread;

int st_sendto(int fd, const void *msg, int len, int flags,
              const struct sockaddr *to, int tolen, int timeout) {
  int64_t start = Util::TimeMs();
  StThreadItem *thread =
      (StThreadItem *)(GlobalThreadSchedule()->GetActiveThread());

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  int64_t now = 0;
  timeout = (timeout <= -1) ? 0x7fffffff : timeout;

  int n = 0;
  while ((n = sys_sendto(fd, msg, len, flags, to, tolen)) < 0) {
    // 对端关闭
    if (n == 0) {
      LOG_ERROR("[n=0]sendto failed, errno: %d, strerr : %s", errno,
                strerror(errno));
      return 0;
    }

    // 判断是否超时
    now = Util::TimeMs();
    if ((int)(now - start) > timeout) {
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

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL, fd: %d", fd);
      return -2;
    }
    item->DisableInput();
    item->EnableOutput();
    item->SetOwnerThread(thread);
    int64_t wakeup_timeout = timeout + Util::TimeMs();
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item,
                                          wakeup_timeout))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
    }
  }

  return n;
}

int st_recvfrom(int fd, void *buf, int len, int flags, struct sockaddr *from,
                socklen_t *fromlen, int timeout) {
  int64_t start = Util::TimeMs();
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  int64_t now = 0;
  timeout = (timeout <= -1) ? 0x7fffffff : timeout;

  while (true) {
    now = Util::TimeMs();
    if ((int)(now - start) > timeout) {
      errno = ETIME;
      return -1;
    }

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL");
      return -2;
    }
    item->DisableOutput();
    item->EnableInput();
    item->SetOwnerThread(thread);
    int64_t wakeup_timeout = timeout + Util::TimeMs();
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item,
                                          wakeup_timeout))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
    }

    int n = sys_recvfrom(fd, buf, len, flags, from, fromlen);
    LOG_TRACE("recvfrom return n: %d, buf: %s, fd: %d, len: %d, flags: %d", n,
              buf, fd, len, flags);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }

      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        LOG_ERROR("recvfrom failed, errno: %d", errno);
        return -1;
      }
    } else if (n == 0) // 对端关闭
    {
      LOG_ERROR("[n=0]recvfrom failed, errno: %d", errno);
      return 0;
    } else {
      return n;
    }
  }
}

int st_connect(int fd, const struct sockaddr *addr, int addrlen, int timeout) {
  int64_t start = Util::TimeMs();
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  int64_t now = 0;
  timeout = (timeout <= -1) ? 0x7fffffff : timeout;

  /* Must call the real syscall, not sys_connect (that re-enters st_connect). */
  HOOK_SYSCALL(connect);
  int n = 0;
  while ((n = REAL_FUNC(connect)(fd, addr, (socklen_t)addrlen)) < 0) {
    LOG_TRACE("connect n: %d, errno: %d, strerror: %s", n, errno,
              strerror(errno));
    now = Util::TimeMs();
    LOG_TRACE("now: %ld, start: %ld", now, start);
    if ((int)(now - start) > timeout) {
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

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL");
      return -2;
    }
    item->DisableInput();
    item->EnableOutput();
    item->SetOwnerThread(thread);
    int64_t wakeup_timeout = timeout + Util::TimeMs();
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item,
                                          wakeup_timeout))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
    }
  }

  return n;
}

ssize_t st_read(int fd, void *buf, size_t nbyte, int timeout) {
  int64_t start = Util::TimeMs();
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  int64_t now = 0;
  timeout = (timeout <= -1) ? 0x7fffffff : timeout;

  ssize_t n = 0;
  while ((n = sys_read(fd, buf, nbyte)) < 0) {
    if (n == 0) // 句柄关闭
    {
      LOG_ERROR("[n=0]read failed, errno: %d", errno);
      return 0;
    }

    now = Util::TimeMs();
    if ((int)(now - start) > timeout) {
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

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL");
      return -2;
    }
    item->DisableOutput();
    item->EnableInput();
    item->SetOwnerThread(thread);
    int64_t wakeup_timeout = timeout + Util::TimeMs();
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item,
                                          wakeup_timeout))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
    }
  }

  return n;
}

ssize_t st_write(int fd, const void *buf, size_t nbyte, int timeout) {
  int64_t start = Util::TimeMs();
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  int64_t now = 0;
  timeout = (timeout <= -1) ? 0x7fffffff : timeout;

  ssize_t n = 0;
  size_t send_len = 0;
  while (send_len < nbyte) {
    now = Util::TimeMs();
    if ((int)(now - start) > timeout) {
      errno = ETIME;
      return -1;
    }

    n = sys_write(fd, (char *)buf + send_len, nbyte - send_len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }

      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        LOG_ERROR("write failed, errno: %d", errno);
        return -1;
      }
    } else if (n == 0) // 已经关闭句柄
    {
      LOG_ERROR("[n=0]write failed, errno: %d", errno);
      return 0;
    } else {
      send_len += n;
      if (send_len >= nbyte) {
        return nbyte;
      }
    }

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL");
      return -2;
    }
    item->DisableInput();
    item->EnableOutput();
    item->SetOwnerThread(thread);
    int64_t wakeup_timeout = timeout + Util::TimeMs();
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item,
                                          wakeup_timeout))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
    }
  }

  return nbyte;
}

int st_recv(int fd, void *buf, int len, int flags, int timeout) {
  int64_t start = Util::TimeMs();
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

  LOG_TRACE("---------- [name: %s] -----------", thread->GetName());
  int64_t now = 0;
  timeout = (timeout <= -1) ? 0x7fffffff : timeout;

  while (true) {
    now = Util::TimeMs();
    LOG_TRACE("now time: %ld, start time: %ld", now, start);
    if ((int)(now - start) > timeout) {
      errno = ETIME;
      return -1;
    }

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL");
      return -2;
    }
    item->DisableOutput();
    item->EnableInput();
    item->SetOwnerThread(thread);
    int64_t wakeup_timeout = timeout + Util::TimeMs();
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item,
                                          wakeup_timeout))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
    }

    int n = sys_recv(fd, buf, len, flags);
    LOG_TRACE("recv return n: %d, buf: %s, fd: %d, len: %d, flags: %d", n, buf,
              fd, len, flags);
    if (n < 0) {
      LOG_ERROR("recv failed, errno: %d, strerr: %s", errno, strerror(errno));
      if (errno == EINTR) {
        continue;
      }
      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        return -1;
      }
    } else if (n == 0) // 对端关闭连接
    {
      LOG_ERROR("[n=0]recv failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      return 0;
    } else {
      return n;
    }
  }
}

ssize_t st_send(int fd, const void *buf, size_t nbyte, int flags, int timeout) {
  int64_t start = Util::TimeMs();
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

  LOG_TRACE("---------- [name : %s] -----------", thread->GetName());
  int64_t now = 0;
  timeout = (timeout <= -1) ? 0x7fffffff : timeout;

  ssize_t n = 0;
  size_t send_len = 0;
  while (send_len < nbyte) {
    now = Util::TimeMs();
    if ((int)(now - start) > timeout) {
      errno = ETIME; // 超时请求
      return -1;
    }

    n = sys_send(fd, (char *)buf + send_len, nbyte - send_len, flags);
    LOG_TRACE("send fd: %d, nbyte: %d, send_len: %d, flags: %d, n: %d", fd,
              nbyte, send_len, flags, n);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }

      if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        LOG_ERROR("write failed, errno: %d, strerr: %s", errno,
                  strerror(errno));
        return -1;
      }
    } else if (n == 0) // 对端关闭连接
    {
      LOG_ERROR("[n=0]write failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      return 0;
    } else {
      send_len += n;
      if (send_len >= nbyte) {
        LOG_TRACE("send_len : %d", send_len);
        return nbyte;
      }
    }

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL");
      return -2;
    }
    item->DisableInput();
    item->EnableOutput();
    item->SetOwnerThread(thread);
    int64_t wakeup_timeout = timeout + Util::TimeMs();
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item,
                                          wakeup_timeout))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
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
  StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

  int connfd = -1;
  while ((connfd = sys_accept(fd, addr, addrlen)) < 0) {
    if (errno == EINTR) {
      continue;
    }

    if (!((errno == EAGAIN) || (errno == EINPROGRESS))) {
      LOG_ERROR("accept failed, errno: %d, strerr: %s", errno, strerror(errno));
      return -1;
    }

    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item == NULL) {
      LOG_ERROR("item is NULL");
      return -2;
    }

    item->DisableOutput();
    item->EnableInput();
    item->SetOwnerThread(thread);
    if (!(GlobalEventSchedule()->Schedule(thread, NULL, item, -1))) {
      LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                strerror(errno));
      // 释放item数据
      UtilPtrPoolFree(item);
      return -3;
    }
  }

  return connfd;
}