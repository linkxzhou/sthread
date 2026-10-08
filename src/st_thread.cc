/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_thread.h"
#include "st_sys.h"

using namespace stlib;
using namespace sthread;

// (run - > stop)
void StThreadSchedule::SwitchThread(StThreadItem *rthread,
                                    StThreadItem *sthread) {
  SetActiveThread(rthread);
  rthread->RestoreContext(sthread);
  SetActiveThread(sthread);
}

void StThreadSchedule::DetachForReuse(StThread *thread) {
  if (thread->HasFlag(eIO_LIST)) {
    RemoveIOWait(thread);
  } else if (thread->HasFlag(eSLEEP_LIST)) {
    RemoveSleep(thread);
  }
  if (thread->HasFlag(eRUN_LIST)) {
    RemoveRunable(thread);
  }
  if (thread->HasFlag(ePEND_LIST)) {
    thread->UnsetFlag(ePEND_LIST);
    CPP_TAILQ_REMOVE(&m_pend_list_, thread, m_next_);
  }
  if (thread->HasFlag(eSUB_LIST)) {
    StThreadItem *parent = thread->GetParent();
    if (parent != NULL) {
      parent->RemoveSubStThread(thread);
    } else {
      thread->UnsetFlag(eSUB_LIST);
    }
  }
}

void StThreadSchedule::ReapReclaim(StThreadItem *current) {
  StThreadItem *hold = NULL;
  unsigned int n = CPP_TAILQ_SIZE(&m_reclaim_list_);
  unsigned int i;

  for (i = 0; i < n; i++) {
    StThreadItem *t = NULL;
    CPP_TAILQ_POP(&m_reclaim_list_, t, m_next_);
    if (t == NULL) {
      break;
    }
    if (t == current) {
      hold = t;
      continue;
    }
    Instance<UtilPtrPool<StThread> >()->NoteDestroyed();
    delete static_cast<StThread *>(t);
  }
  if (hold != NULL) {
    CPP_TAILQ_INSERT_TAIL(&m_reclaim_list_, hold, m_next_);
  }
}

void StThreadSchedule::Recycle(StThread *thread) {
  UtilPtrPool<StThread> *pool;
  if (thread == NULL) {
    return;
  }
  DetachForReuse(thread);
  thread->Reset();
  pool = Instance<UtilPtrPool<StThread> >();
  if (pool->IdleSize() < pool->MaxFree()) {
    pool->PushIdle(thread);
    Yield(thread);
    return;
  }
  /* 空闲池已满。不能在当前栈上 delete 自己，切走后再由 ReapReclaim 释放。 */
  CPP_TAILQ_INSERT_TAIL(&m_reclaim_list_, thread, m_next_);
  Yield(thread);
}

// 让出线程
int32_t StThreadSchedule::Yield(StThreadItem *athread) {
  StThreadItem *thread = NULL;
  ReapReclaim(athread);
  if (CPP_TAILQ_EMPTY(&m_run_list_)) {
    thread = DaemonThread();
  } else {
    thread = PopRunable();
  }
  LOG_TRACE("active thread: %p, athread: %p", thread, athread);
  thread->SetState(eRUNNING);
  ForeachPrint();
  SwitchThread(thread, athread);
  return 0;
}

int32_t StThreadSchedule::Sleep(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("thread NULL, (%p)", thread);
    return -1;
  }
  // 如果再sleep列表中则不处理
  if (thread->HasFlag(eSLEEP_LIST)) {
    return 0;
  }
  thread->SetFlag(eSLEEP_LIST);
  thread->SetState(eSLEEPING);
  m_sleep_list_.HeapPush(thread);
  ForeachPrint();
  if (m_active_thread_ == thread) {
    Yield(m_active_thread_);
  }
  return 0;
}

int32_t StThreadSchedule::Pend(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("thread NULL, (%p)", thread);
    return -1;
  }
  thread->SetFlag(ePEND_LIST);
  thread->SetState(ePENDING);
  CPP_TAILQ_INSERT_TAIL(&m_pend_list_, thread, m_next_);
  ForeachPrint();
  if (m_active_thread_ == thread) {
    Yield(m_active_thread_);
  }
  return 0;
}

int32_t StThreadSchedule::Unpend(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("thread NULL, (%p)", thread);
    return -1;
  }
  thread->UnsetFlag(ePEND_LIST);
  CPP_TAILQ_REMOVE(&m_pend_list_, thread, m_next_);
  InsertRunable(thread);
  ForeachPrint();
  return 0;
}

int32_t StThreadSchedule::IOWaitToRunable(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("thread NULL, (%p)", thread);
    return -1;
  }
  RemoveIOWait(thread);
  InsertRunable(thread);
  ForeachPrint();
  return 0;
}

int32_t StThreadSchedule::RemoveIOWait(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("thread NULL, (%p)", thread);
    return -1;
  }
  thread->UnsetFlag(eIO_LIST);
  CPP_TAILQ_REMOVE(&m_io_list_, thread, m_next_);
  RemoveSleep(thread);
  ForeachPrint();
  return 0;
}

int32_t StThreadSchedule::InsertIOWait(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("active thread NULL, (%p)", thread);
    return -1;
  }
  thread->SetFlag(eIO_LIST);
  thread->SetState(eIOWAIT);
  CPP_TAILQ_INSERT_TAIL(&m_io_list_, thread, m_next_);
  InsertSleep(thread);
  ForeachPrint();
  return 0;
}

int32_t StThreadSchedule::InsertRunable(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("active thread NULL, (%p)", thread);
    return -1;
  }
  thread->SetFlag(eRUN_LIST);
  thread->SetState(eRUNABLE);
  CPP_TAILQ_INSERT_TAIL(&m_run_list_, thread, m_next_);
  ForeachPrint();
  return 0;
}

int32_t StThreadSchedule::RemoveRunable(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("active thread NULL, (%p)", thread);
    return -1;
  }
  thread->UnsetFlag(eRUN_LIST);
  CPP_TAILQ_REMOVE(&m_run_list_, thread, m_next_);
  ForeachPrint();
  return 0;
}

int32_t StThreadSchedule::RemoveSleep(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("thread NULL, (%p)", thread);
    return -1;
  }
  thread->UnsetFlag(eSLEEP_LIST);
  // 如果HeapSize < 0 则不需要处理
  if (m_sleep_list_.HeapSize() <= 0) {
    return -1;
  }
  int rc = m_sleep_list_.HeapDelete(thread);
  if (rc < 0) {
    LOG_ERROR("remove heap failed, rc: %d, size: %d", rc,
              m_sleep_list_.HeapSize());
    return -2;
  }
  ForeachPrint();
  return 0;
}

int32_t StThreadSchedule::InsertSleep(StThreadItem *thread) {
  if (unlikely(NULL == thread)) {
    LOG_ERROR("thread NULL, (%p)", thread);
    return -1;
  }
  thread->SetFlag(eSLEEP_LIST);
  thread->SetState(eSLEEPING);
  m_sleep_list_.HeapPush(thread);
  ForeachPrint();
  return 0;
}

StThreadItem *StThreadSchedule::PopRunable() {
  StThreadItem *thread = NULL;
  CPP_TAILQ_POP(&m_run_list_, thread, m_next_);
  if (unlikely(thread != NULL)) {
    thread->UnsetFlag(eRUN_LIST);
  }
  ForeachPrint();
  return thread;
}

int32_t StThreadSchedule::Notify(StThreadItem *target) {
  if (unlikely(target == NULL)) {
    errno = EINVAL;
    return -1;
  }
  target->SetNotified(1);
  /* 自己通知自己：只留粘滞位。此时再 InsertRunable 会把当前协程重复入队。 */
  if (target == m_active_thread_) {
    return 0;
  }
  /* RemoveIOWait / RemoveSleep 不能作用在不在对应队列上的线程，先看标志。
   * pend 队列是父子协程的，这里不
   * Unpend。已经在跑或已在可运行队列：只留粘滞位。 */
  if (target->HasFlag(eIO_LIST)) {
    RemoveIOWait(target);
    InsertRunable(target);
  } else if (target->HasFlag(eSLEEP_LIST)) {
    RemoveSleep(target);
    InsertRunable(target);
  }
  return 0;
}

void StThreadSchedule::WakeupParent(StThreadItem *thread) {
  StThreadItem *parent = thread->GetParent(); /* B17: 无需 dynamic_cast */
  if (parent) {
    parent->RemoveSubStThread(thread);
    if (parent->HasNoSubStThread()) {
      this->Unpend(parent);
    }
  }
}

void StThreadSchedule::Wakeup(int64_t now) {
  StThread *thread = dynamic_cast<StThread *>(m_sleep_list_.HeapTop());
  LOG_TRACE("thread GetWakeupTime: %ld",
            (thread ? thread->GetWakeupTime() : 0));
  while (thread && (thread->GetWakeupTime() <= now)) {
    if (thread->HasFlag(eIO_LIST)) {
      RemoveIOWait(thread);
    } else {
      RemoveSleep(thread);
    }
    InsertRunable(thread);
    thread = dynamic_cast<StThread *>(m_sleep_list_.HeapTop());
  }
  ForeachPrint();
}

StThread *StThreadSchedule::CreateThread(StClosure *closure, bool runable) {
  StThread *thread = AllocThread();
  if (NULL == thread) {
    LOG_ERROR("alloc thread failed");
    return NULL;
  }
  thread->SetCallback(closure);
  if (runable)
    GlobalThreadSchedule()->InsertRunable(thread); // 插入运行线程
  return thread;
}

int StEventSchedule::Init(int max_num) {
  /* B13/D4: 容量取 min(rlim_cur, 65535)，避免默认按 65535 吃 ~2.3MB/线程 */
  int cap = max_num;
  struct rlimit rlim;
  memset(&rlim, 0, sizeof(rlim));
  if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
    int lim = (int)rlim.rlim_cur;
    if (lim > 65535) {
      lim = 65535;
    }
    if (lim > 0 && (cap <= 0 || cap > lim)) {
      cap = lim;
    }
    if ((int)rlim.rlim_max < cap) {
      rlim.rlim_cur = cap;
      rlim.rlim_max = cap;
      if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        LOG_WARN("setrlimit(RLIMIT_NOFILE) failed, errno: %d", errno);
      }
    }
  } else if (cap > 65535) {
    cap = 65535;
  }
  m_maxfd_ = ST_MAX(cap, m_maxfd_);
  int rc = m_iostate_->Create(m_maxfd_);
  if (rc < 0) {
    rc = -2;
    goto INIT_EXIT_LABEL;
  }

  m_event_ = (StEventItemPtr *)malloc(sizeof(StEventItemPtr) * m_maxfd_);
  if (NULL == m_event_) {
    rc = -3;
    goto INIT_EXIT_LABEL;
  }

  // 初始化设置为空指针
  memset(m_event_, 0, sizeof(StEventItemPtr) * m_maxfd_);

  m_thread_schedule_ = Instance<StThreadSchedule>();
  LOG_ASSERT(m_thread_schedule_ != NULL);

INIT_EXIT_LABEL:
  if (rc < 0) {
    LOG_ERROR("rc: %d error", rc);
    Reset();
  }

  return rc;
}

bool StEventSchedule::Add(StEventItemQueue &fdset) {
  bool ret = true;

  // 保存最后一个出错的位置
  StEventItem *item = NULL, *temp_item = NULL;
  CPP_TAILQ_FOREACH(item, &fdset, m_next_) {
    if (!Add(item)) {
      LOG_ERROR("item add failed, fd: %d", item->GetOsfd());
      temp_item = item;
      ret = false;
      goto ADD_EXIT_LABEL;
    }
    LOG_TRACE("item add success: %p, fd: %d", item, item->GetOsfd());
  }

ADD_EXIT_LABEL:
  // 如果失败则回退
  if (!ret) {
    CPP_TAILQ_FOREACH(item, &fdset, m_next_) {
      if (item == temp_item) {
        break;
      }
      Delete(item);
    }
  }

  return ret;
}

bool StEventSchedule::Delete(StEventItemQueue &fdset) {
  bool ret = true;
  StEventItem *item = NULL, *temp_item = NULL;
  CPP_TAILQ_FOREACH(item, &fdset, m_next_) {
    if (!Delete(item)) {
      LOG_ERROR("item delete failed, fd: %d", item->GetOsfd());
      temp_item = item;
      ret = false;
      goto DELETE_EXIT_LABEL;
    }
  }

DELETE_EXIT_LABEL:
  // 如果失败则回退
  if (!ret) {
    CPP_TAILQ_FOREACH(item, &fdset, m_next_) {
      if (item == temp_item) {
        break;
      }
      Add(item);
    }
  }

  return ret;
}

bool StEventSchedule::Add(StEventItem *item) {
  if (NULL == item) {
    LOG_ERROR("item input invalid, %p", item);
    return false;
  }

  int osfd = item->GetOsfd();
  if (unlikely(!IsValidFd(osfd))) {
    LOG_ERROR("vaild osfd, %d", osfd);
    return false;
  }

  int new_events = item->GetEvents();
  StEventItem *old_item = m_event_[osfd];
  LOG_TRACE("add old_item: %p, item: %p", old_item, item);
  if (old_item != NULL && old_item != item) {
    /* Stale mapping (fd reuse without ClearItem). Detach old from the
     * table/kqueue without freeing the object — caller owns lifetimes. */
    LOG_WARN("item replace, fd: %d, old: %p, new: %p", osfd, old_item, item);
    DeleteFd(osfd, old_item->GetEvents());
    m_event_[osfd] = NULL;
    old_item = NULL;
  }

  if (!AddFd(osfd, new_events)) {
    LOG_ERROR("add fd: %d failed", osfd);
    return false;
  }

  m_event_[osfd] = item;
  item->SetEvents(new_events);
  return true;
}

bool StEventSchedule::Delete(StEventItem *item) {
  if (NULL == item) {
    LOG_ERROR("item input invalid: %p", item);
    return false;
  }

  int osfd = item->GetOsfd();
  if (unlikely(!IsValidFd(osfd))) {
    LOG_ERROR("IsValidFd osfd: %d", osfd);
    return false;
  }

  int del_events = item->GetEvents();
  StEventItem *old_item = m_event_[osfd];
  LOG_TRACE("delete old_item: %p, item: %p", old_item, item);
  if (old_item == NULL) {
    /* Nothing registered for this fd — still try DelEvent for cleanliness. */
    (void)DeleteFd(osfd, del_events);
    return true;
  }
  if (old_item != item) {
    LOG_WARN("delete skip mismatch, fd: %d, old: %p, item: %p", osfd, old_item,
             item);
    return false;
  }

  if (!DeleteFd(osfd, del_events)) {
    LOG_ERROR("del fd: %d failed", osfd);
    return false;
  }

  /* Keep m_event_[osfd] so GetEventItem still works until ClearItem.
   * Events on the item are whatever the caller set; do not reassign slot. */
  return true;
}

bool StEventSchedule::AddFd(int fd, int events) {
  LOG_TRACE("fd: %d, events: %d", fd, events);
  if (unlikely(!IsValidFd(fd))) {
    LOG_ERROR("fd: %d not find", fd);
    return false;
  }

  int rc = m_iostate_->AddEvent(fd, events);
  if (rc < 0) {
    LOG_ERROR("add event failed, fd: %d", fd);
    return false;
  }

  return true;
}

bool StEventSchedule::DeleteFd(int fd, int events) {
  LOG_TRACE("fd: %d, events: %d", fd, events);
  if (unlikely(!IsValidFd(fd))) {
    LOG_ERROR("fd: %d not find", fd);
    return false;
  }

  int rc = m_iostate_->DelEvent(fd, events);
  if (rc < 0) {
    LOG_ERROR("del event failed, fd: %d", fd);
    return false;
  }

  return true;
}

void StEventSchedule::Dispatch(int fdnum) {
  int ret = 0, osfd = 0, revents = 0;
  StEventItem *item = NULL;

  for (int i = 0; i < fdnum; i++) {
    osfd = m_iostate_->m_fired_[i].fd;
    if (unlikely(!IsValidFd(osfd))) {
      LOG_ERROR("fd not find, fd: %d, ev_fdnum: %d", osfd, fdnum);
      continue;
    }

    // 收到的事件
    revents = m_iostate_->m_fired_[i].mask;
    item = m_event_[osfd];
    if (NULL == item) {
      LOG_TRACE("item is NULL, fd: %d", osfd);
      DeleteFd(osfd, (revents & (ST_READABLE | ST_WRITEABLE | ST_EVERR)));
      continue;
    }

    StThreadItem *thread = item->GetOwnerStThread();

    item->SetRecvEvents(revents); // 设置收到的事件
    if (revents & ST_EVERR) {
      LOG_TRACE("ST_EVERR osfd: %d fdnum: %d", osfd, fdnum);
      item->EvHangup();
      /* B4: 唤醒等待协程并清 m_event_ 槽，避免僵死到 sleep 超时 */
      if (thread != NULL && thread->HasFlag(eIO_LIST)) {
        m_thread_schedule_->IOWaitToRunable(thread);
      }
      ClearItem(item);
      continue;
    }

    /* B5: listen 等无 owner 的 item 不可 abort */
    if (thread == NULL) {
      LOG_ERROR("Dispatch: owner thread NULL, fd: %d, revents: %d", osfd,
                revents);
      continue;
    }

    if (revents & ST_READABLE) {
      ret = item->EvInput();
      LOG_TRACE("ST_READABLE osfd: %d, ret: %d fdnum: %d", osfd, ret, fdnum);
      if (ret != 0) {
        LOG_ERROR("revents & ST_READABLE, EvInput ret: %d", ret);
        continue;
      }
      if (thread != NULL && thread->HasFlag(eIO_LIST)) {
        m_thread_schedule_->IOWaitToRunable(thread);
      }
    }

    if (revents & ST_WRITEABLE) {
      ret = item->EvOutput();
      LOG_TRACE("ST_WRITEABLE osfd: %d, ret: %d fdnum: %d", osfd, ret, fdnum);
      if (ret != 0) {
        LOG_ERROR("revents & ST_WRITEABLE, EvOutput ret: %d", ret);
        continue;
      }
      if (thread != NULL && thread->HasFlag(eIO_LIST)) {
        m_thread_schedule_->IOWaitToRunable(thread);
      }
    }
  }
}

// 等待触发事件
void StEventSchedule::Wait(int timeout) {
  int wait_time = ST_MIN(m_timeout_, timeout);
  LOG_TRACE("wait_time: %d ms", wait_time);
  int nfd = 0;
  if (wait_time <= 0) {
    nfd = m_iostate_->Poll(NULL);
  } else {
    /* C6: 一次拆分 tv_sec/tv_usec */
    struct timeval tv;
    tv.tv_sec = (time_t)(wait_time / 1000);
    tv.tv_usec = (suseconds_t)((wait_time % 1000) * 1000);
    nfd = m_iostate_->Poll(&tv);
  }
  {
    StThreadItem *active = m_thread_schedule_->GetActiveThread();
    LOG_TRACE("wait poll nfd: %d, --------[name:%s]---------", nfd,
              active != NULL ? active->GetName() : "(null)");
  }
  if (nfd <= 0) {
    return;
  }
  Dispatch(nfd);
}

// 调度信息
bool StEventSchedule::Schedule(StThreadItem *thread, StEventItemQueue *fdset,
                               StEventItem *item, uint64_t wakeup_timeout) {
  if (NULL == thread) {
    LOG_ERROR("active thread NULL, schedule failed");
    errno = EINVAL;
    return false;
  }

  StEventItem *first_added = NULL;
  unsigned int added_count = 0;
  if (NULL != fdset) {
    LOG_TRACE("fdset: %p", fdset);
    first_added = CPP_TAILQ_FIRST(fdset);
    added_count = CPP_TAILQ_SIZE(fdset);
    thread->Add(fdset);
  }

  if (NULL != item) {
    LOG_TRACE("item: %p", item);
    thread->Add(item);
  }

  thread->SetWakeupTime(wakeup_timeout);
  if (!Add(thread->GetFdSet())) {
    /* Add 失败不是超时。回滚日志可能改 errno，先留下内核错误。 */
    int saved = errno;
    LOG_ERROR("add fdset, errno: %d", saved);
    /* 将本次合并的事件项归还原队列，保留线程先前持有的事件项。 */
    if (NULL != item) {
      CPP_TAILQ_REMOVE_SELF(item, m_next_);
    }
    StEventItem *cursor = first_added;
    for (unsigned int i = 0; i < added_count; i++) {
      StEventItem *next = CPP_TAILQ_NEXT(cursor, m_next_);
      CPP_TAILQ_REMOVE(&thread->GetFdSet(), cursor, m_next_);
      CPP_TAILQ_INSERT_TAIL(fdset, cursor, m_next_);
      cursor = next;
    }
    if (saved == 0 || saved == ETIME) {
      errno = EIO;
    } else {
      errno = saved;
    }
    return false;
  }

  LOG_TRACE("---------- [name: %s] -----------", thread->GetName());
  m_thread_schedule_->InsertIOWait(thread); // 线程切换为IO等待
  if (m_thread_schedule_->GetActiveThread() == thread) {
    m_thread_schedule_->Yield(thread); // 让出当前线程
  }

  int recv_num = 0;
  StEventItemQueue &recv_fdset = thread->GetFdSet();
  StEventItem *_item = NULL;
  CPP_TAILQ_FOREACH(_item, &recv_fdset, m_next_) {
    LOG_TRACE("GetRecvEvents: %d, GetEvents: %d, fd: %d",
              _item->GetRecvEvents(), _item->GetEvents(), _item->GetOsfd());
    if (_item->GetRecvEvents() != 0) {
      recv_num++;
    }
  }
  Delete(recv_fdset);
  /* Schedule 只摘内核兴趣；若不把 item 从线程 m_fdset_ 摘掉，
   * 下次 WaitFdReady 会带着陈旧 item 再 Delete —— fd 复用时
   * m_event_[fd] 已是新 item，触发 "item delete failed" 误报。 */
  {
    StEventItem *_it = NULL, *_nx = NULL;
    CPP_TAILQ_FOREACH_SAFE(_it, &recv_fdset, m_next_, _nx) {
      CPP_TAILQ_REMOVE(&recv_fdset, _it, m_next_);
    }
  }
  /* 没有 IO 事件：定时器唤醒，按超时报告。真正的 Schedule/Add 失败在上面返回。
   */
  if (recv_num == 0) {
    LOG_ERROR("recv_num: 0");
    errno = ETIME;
    return false;
  }
  LOG_TRACE("recv_num: %d", recv_num);
  return true;
}
