/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_MANAGER_H__
#define _ST_MANAGER_H__

#include "st_heap_timer.h"
#include "st_sys.h"
#include "st_thread.h"
#include "st_util.h"

#define GlobalEventScheduler() (GetInstance<EventScheduler>())
#define GlobalThreadScheduler() (GetInstance<ThreadScheduler>())

class Manager {
public:
  Manager()
      : m_daemon_(NULL), m_primo_(NULL), m_heap_timer_(NULL), m_timeout_(1000) {
    Init(); // 初始化
  }

  ~Manager() {
    st_safe_delete(m_primo_);
    st_safe_delete(m_daemon_);
    st_safe_delete(m_heap_timer_);
  }

  void Init(int max_num = 1024) {
    int r = GlobalThreadScheduler()->m_sleep_list_.HeapResize(max_num * 2);
    ASSERT(r >= 0);

    m_heap_timer_ = new StHeapTimer(max_num * 2);
    ASSERT(m_heap_timer_ != NULL);

    // 获取一个daemon线程(从线程池中分配)
    m_daemon_ = new Thread();
    ASSERT(m_daemon_ != NULL);

    m_daemon_->SetType(eDAEMON);
    m_daemon_->SetState(eRUNABLE);
    m_daemon_->SetCallback(NewStClosure(StartDaemonThread, this));
    m_daemon_->SetName("daemon");

    m_primo_ = new Thread();
    ASSERT(m_daemon_ != NULL);

    m_primo_->SetType(ePRIMORDIAL);
    m_primo_->SetState(eRUNNING);
    m_primo_->SetName("primo");

    GlobalThreadScheduler()->SetDaemonThread(m_daemon_);
    GlobalThreadScheduler()->SetPrimoThread(m_primo_);
    GlobalThreadScheduler()->SetActiveThread(m_primo_); // 设置当前的活动线程
    m_last_clock_ = Util::SysMs();

    LOG_TRACE("m_last_clock_: %d", m_last_clock_);
  }

  inline void SetHookFlag() { SET_HOOK_FLAG(); }

  inline void UpdateLastClock() { m_last_clock_ = Util::SysMs(); }

  inline int64_t GetLastClock() { return m_last_clock_; }

  inline StHeapTimer *GetStHeapTimer() { return m_heap_timer_; }

  inline void CheckExpired() {
    int32_t count = 0;
    if (NULL != m_heap_timer_) {
      count = m_heap_timer_->CheckExpired();
    }

    LOG_TRACE("count: %d", count);
  }

  inline int64_t GetTimeout() {
    Thread *thread = dynamic_cast<Thread *>(
        GlobalThreadScheduler()->m_sleep_list_.HeapTop());

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
    Thread *thread = (Thread *)(GlobalThreadScheduler()->GetActiveThread());

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
    return (Thread *)(GetInstance<UtilPtrPool<Thread>>()->AllocPtr());
  }

  static void StartDaemonThread(Manager *manager) {
    ASSERT(manager != NULL);

    Thread *daemon = manager->m_daemon_;
    EventScheduler *event_scheduler = GlobalEventScheduler();
    ThreadScheduler *thread_scheduler = GlobalThreadScheduler();
    if (NULL == daemon || NULL == event_scheduler || NULL == thread_scheduler) {
      LOG_ERROR("(daemon, event_scheduler, thread_scheduler) is NULL");
      return;
    }

    LOG_TRACE("daemon: %p", daemon);

    do {
      event_scheduler->Wait(manager->GetTimeout());
      manager->UpdateLastClock();
      int64_t now = manager->GetLastClock();
      LOG_TRACE("--------[name:%s]--------- : system ms: %ld",
                GlobalThreadScheduler()->GetActiveThread()->GetName(), now);
      // 判断sleep的thread
      thread_scheduler->Wakeup(now);
      manager->CheckExpired();
      // 让出线程
      thread_scheduler->Yield(daemon);
    } while (true);
  }

public:
  Thread *m_daemon_, *m_primo_;
  StHeapTimer *m_heap_timer_;
  int64_t m_last_clock_, m_timeout_;
};

ST_NAMESPACE_END

#ifdef __cplusplus
extern "C" {
#endif

int _sendto(int fd, const void *msg, int len, int flags,
            const struct sockaddr *to, int tolen, int timeout);

int _recvfrom(int fd, void *buf, int len, int flags, struct sockaddr *from,
              socklen_t *fromlen, int timeout);

int _connect(int fd, const struct sockaddr *addr, int addrlen, int timeout);

ssize_t _read(int fd, void *buf, size_t nbyte, int timeout);

ssize_t _write(int fd, const void *buf, size_t nbyte, int timeout);

int _recv(int fd, void *buf, int len, int flags, int timeout);

ssize_t _send(int fd, const void *buf, size_t nbyte, int flags, int timeout);

void _sleep(int ms);

int _accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

#endif