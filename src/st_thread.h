/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_THREAD_H_
#define _ST_THREAD_H_

#include "st_poll.h"
#include "st_public.h"
#include "stlib/st_heap_timer.h"
#include "stlib/st_util.h"

namespace sthread {

using namespace stlib;

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
      m_daemon_ = new StThread();
      m_daemon_->SetType(eDAEMON);
      m_daemon_->SetState(eRUNABLE);
      m_daemon_->SetCallback(NewStClosure(Startup, this));
      m_daemon_->SetName(THREAD_DAEMON_NAME);
    }
    return m_daemon_;
  }

  inline StThreadItem *PrimoThread(void) {
    if (m_primo_ == NULL) {
      m_primo_ = new StThread();
      m_primo_->SetType(ePRIMORDIAL);
      m_primo_->SetState(eRUNNING);
      m_primo_->SetName(THREAD_PRIMO_NAME);
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

  // 唤醒父亲线程
  void WakeupParent(StThreadItem *thread);

  // 唤醒sleep的列表中的线程
  void Wakeup(int64_t now);

  // pop最新一个运行的线程
  StThreadItem *PopRunable();

  StThread *CreateThread(StClosure *closure, bool runable = true);

  // 创建线程
  inline StThread *AllocThread() {
    return (StThread *)(Instance<UtilPtrPool<StThread>>()->AllocPtr());
  }

  // !debug print
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
      : m_maxfd_(65535), m_iostate_(new StIOState()), m_event_(NULL),
        m_timeout_(30000) // 默认超时30s
  {
    int32_t r = Init(max_num);
    LOG_ASSERT(r >= 0);
  }

  ~StEventSchedule() {
    m_iostate_->Free(); // 释放链接
    st_safe_delete(m_iostate_);
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
  StIOState *m_iostate_;
  StEventItemPtr *m_event_;
  StThreadSchedule *m_thread_schedule_;
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
  bool InitStack() {
    if (NULL != m_stack_) {
      LOG_TRACE("m_stack_ = %p", m_stack_);
      return true;
    }
    m_stack_ = (Stack *)calloc(1, sizeof(Stack));
    if (NULL == m_stack_) {
      LOG_ERROR("calloc stack failed, size : %ld", sizeof(Stack));
      return false;
    }
    int memsize =
        MEM_PAGE_SIZE * 2 + (m_stack_size_ / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE;
    void *vaddr = malloc(memsize * sizeof(uchar));
    m_stack_->m_vaddr_ = (uchar *)vaddr;
    m_stack_->m_vaddr_size_ = memsize;
    m_stack_->m_stk_size_ = m_stack_size_;
    m_stack_->m_id_ = Util::GetUniqid(); // 生成唯一id
    snprintf(m_name_, sizeof(m_name_) - 1, "T%u", m_stack_->m_id_);
    sigset_t zero;
    memset(&m_stack_->m_context_.uc, 0, sizeof(m_stack_->m_context_.uc));
    sigemptyset(&zero);
    sigprocmask(SIG_BLOCK, &zero, &m_stack_->m_context_.uc.uc_sigmask);
    return true;
  }

  void FreeStack() {
    if (NULL == m_stack_) {
      LOG_WARN("m_stack_ == NULL");
      return;
    }
    st_safe_free(m_stack_);
  }

  void InitContext() {
    uint32_t tx, ty;
    uint64_t tz = (uint64_t)m_stack_;
    ty = tz;
    tz >>= 16;
    tx = tz >> 16;
    m_stack_->m_private_ = this; // 保存私有数据
    if (getcontext(&m_stack_->m_context_.uc) < 0) {
      LOG_ERROR("getcontext error");
      return;
    }

    m_stack_->m_context_.uc.uc_stack.ss_sp = m_stack_->m_vaddr_ + 8;
    m_stack_->m_context_.uc.uc_stack.ss_size = m_stack_->m_vaddr_size_ - 64;

    makecontext(&m_stack_->m_context_.uc, (void (*)())ActiveThreadStartUp, 2,
                ty, tx);
  }

  void RestoreContext(StThreadItem *thread) {
    LOG_TRACE("RestoreContext ### begin ### this: %p, thread: %p", this,
              thread);
    // 切换的线程相同
    if (thread == this) {
      return;
    }
    LOG_TRACE("================= run thread[name:%s] =================[%p]",
              this->GetName(), this);
    LOG_TRACE("contextswitch: (%p,%p) -> (%p,%p)", thread,
              &(thread->GetStack()->m_context_), this,
              &(this->GetStack()->m_context_));
    context_switch(&(thread->GetStack()->m_context_),
                   &(this->GetStack()->m_context_));
    LOG_TRACE("RestoreContext ### end ### this: %p, thread: %p", this, thread);
  }

  static void ActiveThreadStartUp(uint ty, uint tx) {
    LOG_TRACE("---------- ActiveThreadStartUp -----------");
    Stack *t;
    ulong z;
    z = tx << 16; /* hide undefined 32-bit shift from 32-bit compilers */
    z <<= 16;
    z |= ty;
    t = (Stack *)z;
    LOG_TRACE("ActiveThreadCallback: %p, t: %p, id: %d", t->m_private_, t,
              t->m_id_);
    StThread *thread = (StThread *)(t->m_private_);
    StThreadSchedule *thread_schedule = Instance<StThreadSchedule>();
    if (NULL != thread) {
      LOG_TRACE("---------- [\\\name: %s\\\] -----------", thread->GetName());
      if (NULL != thread->m_callback_) {
        thread->m_callback_->Run();
      }
      // 判断当前线程是否有子线程
      if (thread->IsSubThread()) {
        thread_schedule->WakeupParent(thread);
      }
      thread_schedule->Yield(thread);
    }
    LOG_TRACE("---------- [///name: %s///] -----------", thread->GetName());
    if (thread == thread_schedule->DaemonThread()) {
      thread_schedule->SwitchThread(thread_schedule->PrimoThread(), thread);
    } else {
      thread_schedule->SwitchThread(thread_schedule->DaemonThread(), thread);
    }
    context_exit(0);
  }
};

} // namespace sthread

#endif // _ST_THREAD_H_