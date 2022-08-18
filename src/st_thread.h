/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_THREAD_H_
#define _ST_THREAD_H_

#include "st_poll.h"
#include "stlib/st_heap_timer.h"
#include "stlib/st_util.h"

namespace sthread {

using namespace stlib;

#define MEM_PAGE_SIZE 2048
#define DAEMON_NAME "daemon"
#define PRIMO_NAME "primo"

class StThreadSchedule {
public:
  StThreadSchedule() : m_active_thread_(NULL), m_daemon_(NULL), m_primo_(NULL) {
    CPP_TAILQ_INIT(&m_run_list_);     // 运行队列
    CPP_TAILQ_INIT(&m_io_list_);      // io队列
    CPP_TAILQ_INIT(&m_pend_list_);    // 阻塞队列
    CPP_TAILQ_INIT(&m_reclaim_list_); // 恢复队列
  }

  ~StThreadSchedule() {
    m_active_thread_ = NULL;

    // TODO: 回收sthread
    m_daemon_ = NULL;
    m_primo_ = NULL;
  }

  // 启动函数入口
  static void Startup(StThreadSchedule *ss);

  inline StThreadItem *DaemonThread(void) {
    if (m_daemon_ == NULL) {
      m_daemon_ = new Thread();
      m_daemon_->SetType(eDAEMON);
      m_daemon_->SetState(eRUNABLE);
      m_daemon_->SetCallback(NewStClosure(Startup, this));
      m_daemon_->SetName(DAEMON_NAME);
    }

    return m_daemon_;
  }

  inline StThreadItem *PrimoThread(void) {
    if (m_primo_ == NULL) {
      m_primo_ = new Thread();
      m_primo_->SetType(ePRIMORDIAL);
      m_primo_->SetState(eRUNNING);
      m_primo_->SetName(PRIMO_NAME);
    }

    return m_primo_;
  }

  inline void ResetHeapSize(int32_t max_num) {
    m_sleep_list_.HeapResize(max_num);
  }

  inline StThreadItem *GetActiveThread() { return m_active_thread_; }

  inline void SetActiveThread(StThreadItem *thread) {
    m_active_thread_ = thread;
  }

  void SwitchThread(StThreadItem *rthread, StThreadItem *sthread);

  // 让出当前线程
  int32_t Yield(StThreadItem *thread);

  int32_t Sleep(StThreadItem *thread);

  int32_t Pend(StThreadItem *thread);

  int32_t Unpend(StThreadItem *thread);

  int32_t IOWaitToRunable(StThreadItem *thread);

  int32_t InsertIOWait(StThreadItem *thread);

  int32_t RemoveIOWait(StThreadItem *thread);

  int32_t InsertRunable(StThreadItem *thread);

  int32_t RemoveRunable(StThreadItem *thread);

  int32_t InsertSleep(StThreadItem *thread);

  int32_t RemoveSleep(StThreadItem *thread);

  int32_t ReclaimThread(StThreadItem *thread);

  // 唤醒父亲线程
  void WakeupParent(StThreadItem *thread);

  // 唤醒sleep的列表中的线程
  void Wakeup(int64_t now);

  // pop最新一个运行的线程
  StThreadItem *PopRunable();

  StThread *CreateThread(StClosure *closure, bool runable = true);

  // 创建线程
  inline Thread *AllocThread() {
    return (StThread *)(GetInstance<UtilPtrPool<StThread>>()->AllocPtr());
  }

  inline void ForeachPrint() {
    LOG_TRACE("m_run_list_ size: %d, m_io_list_ size: %d,"
              " m_pend_list_ size: %d, m_reclaim_list_: %d",
              CPP_TAILQ_SIZE(&m_run_list_), CPP_TAILQ_SIZE(&m_io_list_),
              CPP_TAILQ_SIZE(&m_pend_list_), CPP_TAILQ_SIZE(&m_reclaim_list_));
  }

public:
  StThreadItemQueue m_run_list_, m_io_list_, m_pend_list_, m_reclaim_list_;
  StHeapList<StThreadItem> m_sleep_list_;
  StThreadItem *m_active_thread_, *m_daemon_, *m_primo_;
};

class StEventSchedule {
public:
  typedef StEventItem *StEventItemPtr;

  StEventSchedule(int32_t max_num = 1024)
      : m_maxfd_(65535), m_apistate_(new StApiState()), m_event_(NULL),
        m_timeout_(30000) // 默认超时30s
  {
    int32_t r = Init(max_num);
    LOG_ASSERT(r >= 0);
  }

  ~StEventSchedule() {
    m_apistate_->ApiFree(); // 释放链接
    st_safe_delete(m_apistate_);
    st_safe_delete_array(m_event_);
  }

  int32_t Init(int32_t max_num);

  bool Schedule(StThreadItem *thread, StEventItemQueue *item_list,
                StEventItem *item, uint64_t wakeup_timeout);

  bool Add(StEventItemQueue &item_list);

  bool Add(StEventItem *item);

  bool Delete(StEventItemQueue &item_list);

  bool Delete(StEventItem *item);

  bool AddFd(int32_t fd, int32_t new_events);

  bool DeleteFd(int32_t fd, int32_t new_events);

  inline bool IsValidFd(int32_t fd) {
    return ((fd >= m_maxfd_) || (fd < 0)) ? false : true;
  }

  inline void BindItem(int32_t fd, StEventItem *item) {
    if (unlikely(IsValidFd(fd))) {
      m_event_[fd] = item;
    }
  }

  inline StEventItem *GetEventItem(int32_t fd) {
    return IsValidFd(fd) ? m_event_[fd] : NULL;
  }

  void Dispatch(int32_t fdnum);

  void Wait(int32_t timeout); // 等待毫秒数

  inline void ClearItem(StEventItem *item) {
    Delete(item);
    int osfd = item->GetOsfd();
    if (unlikely(!IsValidFd(osfd))) {
      LOG_ERROR("IsValidFd osfd, %d", osfd);
      return;
    }
    m_event_[osfd] = NULL;
  }

protected:
  int32_t m_maxfd_, m_timeout_;
  StApiState *m_apistate_;
  StEventItemPtr *m_event_;
  StThreadSchedule *m_sschedule_;
};

class StThread : public StThreadItem {
public:
  StThread() : StThreadItem() {
    LOG_ASSERT(InitStack());
    InitContext();
  }

  virtual ~StThread() { FreeStack(); }

  virtual void Run(void);

  // 设置休眠时间
  virtual void Sleep(int32_t ms) {
    uint64_t now = Util::TimeMs();
    m_wakeup_time_ = now + ms;
    LOG_TRACE("now: %ld, m_wakeup_time_: %ld", now, m_wakeup_time_);
  }

  virtual void RestoreContext(StThreadItem *switch_thread);

  void WakeupParent();

protected:
  bool InitStack();

  void FreeStack();

  void InitContext();
};

} // namespace sthread

#endif // _ST_THREAD_H_