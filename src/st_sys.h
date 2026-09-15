/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_MANAGER_H__
#define _ST_MANAGER_H__

#include "st_public.h"
#include "st_thread.h"
#include "stlib/st_heap_timer.h"
#include "stlib/st_util.h"

namespace sthread {

class StSysSchedule {
public:
  StSysSchedule()
      : m_daemon_(NULL), m_primo_(NULL), m_heap_timer_(NULL), m_timeout_(1000) {
    Init(); // 初始化
  }

  ~StSysSchedule() {
    // daemon/primo owned by StThreadSchedule; do not delete
    st_safe_delete(m_heap_timer_);
  }

  void Init(int max_num = 1024) {
    int r = GlobalThreadSchedule()->m_sleep_list_.HeapResize(max_num * 2);
    LOG_ASSERT(r >= 0);

    m_heap_timer_ = new StHeapTimer(max_num * 2);
    LOG_ASSERT(m_heap_timer_ != NULL);

    // D3(B): schedule owns daemon/primo via lazy getters; we only alias + rebind callback.
    m_daemon_ = dynamic_cast<StThread *>(GlobalThreadSchedule()->DaemonThread());
    LOG_ASSERT(m_daemon_ != NULL);
    m_daemon_->SetCallback(NewStClosure(StartUp, this));

    m_primo_ = dynamic_cast<StThread *>(GlobalThreadSchedule()->PrimoThread());
    LOG_ASSERT(m_primo_ != NULL);

    GlobalThreadSchedule()->SetActiveThread(m_primo_);
    m_last_clock_ = Util::TimeMs();

    LOG_TRACE("m_last_clock_: %d", m_last_clock_);
  }

  inline void UpdateLastClock() { m_last_clock_ = Util::TimeMs(); }

  inline int64_t GetLastClock() { return m_last_clock_; }

  inline StHeapTimer *GetStHeapTimer() { return m_heap_timer_; }

  inline void CheckExpired() {
    int32_t count = 0;
    if (NULL != m_heap_timer_) {
      count = m_heap_timer_->CheckExpired();
    }
    LOG_TRACE("CheckExpired count: %d", count);
  }

  inline int64_t GetTimeout() {
    StThread *thread =
        dynamic_cast<StThread *>(GlobalThreadSchedule()->m_sleep_list_.HeapTop());

    int64_t now = GetLastClock();
    if (!thread) {
      return m_timeout_;
    } else if (thread->GetWakeupTime() < now) {
      return 0;
    } else {
      return (int64_t)(thread->GetWakeupTime() - now);
    }
  }

  int WaitEvents(int fd, int events, int timeout) {
    int64_t start = GetLastClock();
    StThread *thread = (StThread *)(GlobalThreadSchedule()->GetActiveThread());

    int64_t now = 0;
    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

    while (true) {
      now = GetLastClock();
      if ((int)(now - start) > timeout) {
        LOG_TRACE("timeout is over");
        errno = ETIME;
        return -1;
      }

      StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
      if (NULL == item) {
        LOG_TRACE("item is NULL");
        return -2;
      }

      item->SetOwnerThread(thread);

      if (events & ST_READABLE) {
        item->EnableInput();
      }

      if (events & ST_WRITEABLE) {
        item->EnableOutput();
      }

      int64_t wakeup_timeout = timeout + GetLastClock();
      bool rc =
          GlobalEventSchedule()->Schedule(thread, NULL, item, wakeup_timeout);
      if (!rc) {
        LOG_ERROR("item schedule failed, errno: %d, strerr: %s", errno,
                  strerror(errno));
        // 释放item数据
        UtilPtrPoolFree(item);
        return -3;
      }

      if (item->GetRecvEvents() > 0) {
        return 0;
      }
    }
  }

  StThread *CreateThread(StClosure *closure, bool runable = true) {
    StThread *thread = AllocThread();
    if (NULL == thread) {
      LOG_ERROR("alloc thread failed");
      return NULL;
    }

    thread->SetCallback(closure);
    if (runable) {
      GlobalThreadSchedule()->InsertRunable(thread); // 插入运行线程
    }

    return thread;
  }

  inline StThread *AllocThread() {
    return (StThread *)(Instance<UtilPtrPool<StThread> >()->AllocPtr());
  }

  static void StartUp(StSysSchedule *schedule) {
    LOG_ASSERT(schedule != NULL);
    StThread *daemon = schedule->m_daemon_;
    StEventSchedule *event_schedule = GlobalEventSchedule();
    StThreadSchedule *thread_schedule = GlobalThreadSchedule();
    if (NULL == daemon || NULL == event_schedule || NULL == thread_schedule) {
      LOG_ERROR("daemon: %p, event_schedule: %p, thread_schedule: %p", daemon,
                event_schedule, thread_schedule);
      return;
    }

    LOG_TRACE("--------daemon: %p", daemon);

    do {
      event_schedule->Wait(schedule->GetTimeout());
      schedule->UpdateLastClock();
      int64_t now = schedule->GetLastClock();
      LOG_TRACE("--------[name:%s]--------- : system ms: %ld",
                GlobalThreadSchedule()->GetActiveThread()->GetName(), now);
      // 判断sleep的thread
      thread_schedule->Wakeup(now);
      schedule->CheckExpired();
      // 让出线程
      thread_schedule->Yield(daemon);
    } while (true);
  }

public:
  StThread *m_daemon_, *m_primo_;
  StHeapTimer *m_heap_timer_;
  int64_t m_last_clock_, m_timeout_;
};

} // namespace sthread

#ifdef __cplusplus
extern "C" {
#endif

int st_sendto(int fd, const void *msg, int len, int flags,
             const struct sockaddr *to, int tolen, int timeout);

int st_recvfrom(int fd, void *buf, int len, int flags, struct sockaddr *from,
               socklen_t *fromlen, int timeout);

int st_connect(int fd, const struct sockaddr *addr, int addrlen, int timeout);

ssize_t st_read(int fd, void *buf, size_t nbyte, int timeout);

ssize_t st_write(int fd, const void *buf, size_t nbyte, int timeout);

int st_recv(int fd, void *buf, int len, int flags, int timeout);

ssize_t st_send(int fd, const void *buf, size_t nbyte, int flags, int timeout);

void st_sleep(int ms);

int st_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

#endif