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
    st_safe_delete(m_primo_);
    st_safe_delete(m_daemon_);
    st_safe_delete(m_heap_timer_);
  }

  void Init(int max_num = 1024) {
    int r = GlobalThreadSchedule()->m_sleep_list_.HeapResize(max_num * 2);
    ASSERT(r >= 0);

    m_heap_timer_ = new StHeapTimer(max_num * 2);
    ASSERT(m_heap_timer_ != NULL);

    // 获取一个daemon线程(从线程池中分配)
    m_daemon_ = new Thread();
    ASSERT(m_daemon_ != NULL);

    m_daemon_->SetType(eDAEMON);
    m_daemon_->SetState(eRUNABLE);
    m_daemon_->SetCallback(NewStClosure(StartUp, this));
    m_daemon_->SetName(THREAD_DAEMON_NAME);

    m_primo_ = new Thread();
    ASSERT(m_daemon_ != NULL);

    m_primo_->SetType(ePRIMORDIAL);
    m_primo_->SetState(eRUNNING);
    m_primo_->SetName(THREAD_PRIMO_NAME);

    GlobalThreadSchedule()->SetDaemonThread(m_daemon_);
    GlobalThreadSchedule()->SetPrimoThread(m_primo_);
    GlobalThreadSchedule()->SetActiveThread(m_primo_); // 设置当前的活动线程
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
    Thread *thread =
        dynamic_cast<Thread *>(GlobalThreadSchedule()->m_sleep_list_.HeapTop());

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
    Thread *thread = (Thread *)(GlobalThreadSchedule()->GetActiveThread());

    int64_t now = 0;
    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

    while (true) {
      now = GetLastClock();
      if ((int)(now - start) > timeout) {
        LOG_TRACE("timeout is over");
        errno = ETIME;
        return -1;
      }

      StEventSuper *item = GlobalEventScheduler()->GetEventItem(fd);
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
          GlobalEventScheduler()->Schedule(thread, NULL, item, wakeup_timeout);
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

  Thread *CreateThread(StClosure *StClosure, bool runable = true) {
    Thread *thread = AllocThread();
    if (NULL == thread) {
      LOG_ERROR("alloc thread failed");
      return NULL;
    }

    thread->SetCallback(StClosure);
    if (runable) {
      GlobalThreadScheduler()->InsertRunable(thread); // 插入运行线程
    }

    return thread;
  }

  inline Thread *AllocThread() {
    return (Thread *)(Instance<UtilPtrPool<Thread>>()->AllocPtr());
  }

  static void StartUp(StSysSchedule *schedule) {
    LOG_ASSERT(schedule != NULL);
    Thread *daemon = schedule->m_daemon_;
    EventSchedule *event_schedule = GlobalEventSchedule();
    ThreadSchedule *thread_schedule = GlobalThreadSchedule();
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
  Thread *m_daemon_, *m_primo_;
  StHeapTimer *m_heap_timer_;
  int64_t m_last_clock_, m_timeout_;
};

} // namespace sthread

#ifdef __cplusplus
extern "C" {
#endif

int __sendto(int fd, const void *msg, int len, int flags,
             const struct sockaddr *to, int tolen, int timeout);

int __recvfrom(int fd, void *buf, int len, int flags, struct sockaddr *from,
               socklen_t *fromlen, int timeout);

int __connect(int fd, const struct sockaddr *addr, int addrlen, int timeout);

ssize_t __read(int fd, void *buf, size_t nbyte, int timeout);

ssize_t st_write(int fd, const void *buf, size_t nbyte, int timeout);

int __recv(int fd, void *buf, int len, int flags, int timeout);

ssize_t __send(int fd, const void *buf, size_t nbyte, int flags, int timeout);

void __sleep(int ms);

int __accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

#endif