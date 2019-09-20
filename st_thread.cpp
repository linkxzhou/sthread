/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_thread.h"

ST_NAMESPACE_USING;

// 线程启动
static void StartActiveThread(uint ty, uint tx) 
{
    LOG_TRACE("---------- StartActiveThread -----------");

    Stack *t;
	ulong z;

	z = tx<<16;	/* hide undefined 32-bit shift from 32-bit compilers */
	z <<= 16;
	z |= ty;
	t = (Stack*)z;

    LOG_TRACE("StartActiveThread: %p, t: %p, id: %d", 
        t->m_private_, t, t->m_id_);
    Thread *thread = (Thread*)(t->m_private_);
    ThreadScheduler *scheduler = GetInstance<ThreadScheduler>();
    if (thread)
    {
        LOG_TRACE("---------- [name: %s] -----------", thread->GetName());
        thread->Run();

        // 判断当前线程是否有子线程
        if (thread->IsSubThread())
        {
            scheduler->WakeupParent(thread);
        }

        // TODO : 系统回收Thread
        // Scheduler::Reclaim();
        scheduler->Yield(thread);
    }

    if (thread == scheduler->DaemonThread()) 
    {
        scheduler->SwitchThread(scheduler->PrimoThread(), thread);
    }
    else
    {
        scheduler->SwitchThread(scheduler->DaemonThread(), thread);
    }

    context_exit();
}

// 调度系统(run - > stop)
void ThreadScheduler::SwitchThread(StThreadBase *rthread, StThreadBase *sthread)
{
    SetActiveThread(rthread);
    rthread->RestoreContext(sthread);
    SetActiveThread(sthread);
}

// 让出线程
int ThreadScheduler::Yield(StThreadBase *athread)
{
    StThreadBase *thread = NULL;
    if (CPP_TAILQ_EMPTY(&m_run_list_))
    {
        thread = DaemonThread();
    }
    else
    {
        thread = PopRunable();
    }

    LOG_TRACE("active thread: %p, athread: %p", thread, athread);

    thread->SetState(eRUNNING);
    ForeachPrint();
    SwitchThread(thread, athread);

    return 0;
}

int ThreadScheduler::Sleep(StThreadBase *thread)
{
    LOG_TRACE("m_active_thread_, thread : %p, %p", 
        m_active_thread_, thread);
    if (!thread)
    {
        LOG_ERROR("thread NULL (%p)", thread);
        return -1;
    }

    // 如果再sleep列表中则不处理
    if (thread->HasFlag(eSLEEP_LIST))
    {
        return 0;
    }

    thread->SetFlag(eSLEEP_LIST);
    thread->SetState(eSLEEPING);
	m_sleep_list_.HeapPush(thread);
    ForeachPrint();
    if (m_active_thread_ == thread)
    {
        Yield(m_active_thread_);
    }

    return 0;
}

int ThreadScheduler::Pend(StThreadBase *thread)
{
    thread->SetFlag(ePEND_LIST);
    thread->SetState(ePENDING);
	CPP_TAILQ_INSERT_TAIL(&m_pend_list_, thread, m_next_);
    LOG_TRACE("m_active_thread_, thread : %p, %p", 
        m_active_thread_, thread);
    ForeachPrint();
    if (m_active_thread_ == thread)
    {
        Yield(m_active_thread_);
    }

    return 0;
}

int ThreadScheduler::Unpend(StThreadBase *thread)
{
    thread->UnsetFlag(ePEND_LIST);
    CPP_TAILQ_REMOVE(&m_pend_list_, thread, m_next_);
    InsertRunable(thread);
    ForeachPrint();

    return 0;
}

int ThreadScheduler::IOWaitToRunable(StThreadBase *thread)
{
    if (unlikely(NULL == thread))
    {
        LOG_ERROR("thread NULL, (%p)", thread);
        return -1;
    }

    RemoveIOWait(thread);
    InsertRunable(thread);
    ForeachPrint();

    return 0;
}

int ThreadScheduler::RemoveIOWait(StThreadBase *thread)
{
    if (unlikely(NULL == thread))
    {
        LOG_ERROR("thread NULL, (%p)", thread);
        return -1;
    }

    thread->UnsetFlag(eIO_LIST);
    LOG_TRACE("remove thread: %p, m_io_list_ size: %d", 
        thread, CPP_TAILQ_SIZE(&m_io_list_));
    CPP_TAILQ_REMOVE(&m_io_list_, thread, m_next_);
    RemoveSleep(thread);
    ForeachPrint();

    return 0;
}

int ThreadScheduler::InsertIOWait(StThreadBase *thread)
{
    if (unlikely(NULL == thread))
    {
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

int ThreadScheduler::InsertRunable(StThreadBase *thread)
{
    if (unlikely(NULL == thread))
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return -1;
    }

    thread->SetFlag(eRUN_LIST);
    thread->SetState(eRUNABLE);
    CPP_TAILQ_INSERT_TAIL(&m_run_list_, thread, m_next_);
    ForeachPrint();

    return 0;
}

int ThreadScheduler::RemoveRunable(StThreadBase *thread)
{
    if (unlikely(NULL == thread))
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return -1;
    }

    thread->UnsetFlag(eRUN_LIST);
    LOG_TRACE("remove thread: %p, m_io_list_ size: %d", 
        thread, CPP_TAILQ_SIZE(&m_io_list_));
    CPP_TAILQ_REMOVE(&m_run_list_, thread, m_next_);
    ForeachPrint();

    return 0;
}

StThreadBase* ThreadScheduler::PopRunable()
{
    StThreadBase *thread = NULL;
    CPP_TAILQ_POP(&m_run_list_, thread, m_next_);
    if (unlikely(thread != NULL))
    {
        thread->UnsetFlag(eRUN_LIST);
    }
    ForeachPrint();

    return thread;
}

int ThreadScheduler::RemoveSleep(StThreadBase *thread)
{
    thread->UnsetFlag(eSLEEP_LIST);
    // 如果HeapSize < 0 则不需要处理
    if (m_sleep_list_.HeapSize() <= 0)
    {
        return -1;
    }

    int rc = m_sleep_list_.HeapDelete(thread);
    if (rc < 0)
    {
        LOG_ERROR("remove heap failed, rc: %d, size: %d", 
            rc, m_sleep_list_.HeapSize());
        return -2;
    }
    ForeachPrint();

    return 0;
}

int ThreadScheduler::InsertSleep(StThreadBase *thread)
{
    thread->SetFlag(eSLEEP_LIST);
    thread->SetState(eSLEEPING);
    m_sleep_list_.HeapPush(thread);
    ForeachPrint();
    
    return 0;
}

void ThreadScheduler::WakeupParent(StThreadBase *thread)
{
    StThreadBase *parent = dynamic_cast<Thread*>(thread->GetParent());
    if (parent)
    {
        parent->RemoveSubThread(thread);
        // TODO : 有问题
        if (parent->HasNoSubThread())
        {
            this->Unpend(parent);
        }
    }
}

void ThreadScheduler::Wakeup(int64_t now)
{
    Thread *thread = dynamic_cast<Thread*>(m_sleep_list_.HeapTop());
    LOG_TRACE("thread GetWakeupTime: %ld", (thread ? thread->GetWakeupTime() : 0));
    while (thread && (thread->GetWakeupTime() <= now))
    {
        if (thread->HasFlag(eIO_LIST))
        {
            RemoveIOWait(thread);
        }
        else
        {
            RemoveSleep(thread);
        }
        
        InsertRunable(thread);
        thread = dynamic_cast<Thread*>(m_sleep_list_.HeapTop());
    }

    ForeachPrint();
}

int EventScheduler::Init(int max_num)
{
    m_maxfd_ = ST_MAX(max_num, m_maxfd_);
    int rc = m_state_->ApiCreate(m_maxfd_);
    if (rc < 0)
    {
        rc = -2;
        goto INIT_EXIT_LABEL;
    }

    m_container_ = (StEventBasePtr*)malloc(sizeof(StEventBasePtr) * m_maxfd_);
    if (NULL == m_container_)
    {
        rc = -3;
        goto INIT_EXIT_LABEL;
    }

    // 初始化设置为空指针
    memset(m_container_, 0, sizeof(StEventBasePtr) * m_maxfd_);

    // 设置系统参数
    struct rlimit rlim;
    memset(&rlim, 0, sizeof(rlim));
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
    {
        if ((int)rlim.rlim_max < m_maxfd_)
        {
            // 重新设置句柄大小
            rlim.rlim_cur = m_maxfd_;
            rlim.rlim_max = m_maxfd_;
            setrlimit(RLIMIT_NOFILE, &rlim);
        }
    }

    m_thread_scheduler_ = GetInstance<ThreadScheduler>();
    ASSERT(m_thread_scheduler_ != NULL);

INIT_EXIT_LABEL:
    if (rc < 0)
    {
        LOG_ERROR("rc: %d error", rc);
        Close();
    }

    return rc;
}       

bool EventScheduler::Add(StEventBaseQueue &fdset)
{
    bool ret = true;

    // 保存最后一个出错的位置
    StEventBase *item = NULL, *temp_item = NULL; 
    CPP_TAILQ_FOREACH(item, &fdset, m_next_)
    {
        if (!Add(item))
        {
            LOG_ERROR("item add failed, fd: %d", item->GetOsfd());
            temp_item = item;
            ret = false;

            goto ADD_EXIT_LABEL;
        }

        LOG_TRACE("item add success: %p, fd: %d", item, item->GetOsfd());
    }

ADD_EXIT_LABEL:
    // 如果失败则回退
    if (!ret)
    {
        CPP_TAILQ_FOREACH(item, &fdset, m_next_)
        {
            if (item == temp_item)
            {
                break;
            }

            Delete(item);
        }
    }

    return ret;
}

bool EventScheduler::Delete(StEventBaseQueue &fdset)
{
    bool ret = true;
    StEventBase *item = NULL, *temp_item = NULL;
    CPP_TAILQ_FOREACH(item, &fdset, m_next_)
    {
        if (!Delete(item))
        {
            LOG_ERROR("item delete failed, fd: %d", item->GetOsfd());
            temp_item = item;
            ret = false;

            goto DELETE_EXIT_LABEL;
        }
    }

DELETE_EXIT_LABEL:
    // 如果失败则回退
    if (!ret)
    {
        CPP_TAILQ_FOREACH(item, &fdset, m_next_)
        {
            if (item == temp_item)
            {
                break;
            }

            Add(item);
        }
    }

    return ret;
}

bool EventScheduler::Add(StEventBase *item)
{
    if (NULL == item)
    {
        LOG_ERROR("item input invalid, %p", item);
        return false;
    }

    int osfd = item->GetOsfd();
    if (unlikely(!IsValidFd(osfd)))
    {
        LOG_ERROR("IsValidFd osfd, %d", osfd);
        return false;
    }

    int new_events = item->GetEvents();
    StEventBase *old_item = m_container_[osfd];
    LOG_TRACE("add old_item: %p, item: %p", old_item, item);
    if (NULL == old_item)
    {
        m_container_[osfd] = item;
        if (!AddFd(osfd, new_events))
        {
            LOG_ERROR("add fd: %d failed", osfd);
            return false;
        }
    }
    else
    {
        if (old_item != item)
        {
            LOG_ERROR("item conflict, fd: %d, old: %p, new: %p", 
                osfd, old_item, item);
            return false;
        }

        if (!AddFd(osfd, new_events))
        {
            LOG_ERROR("add fd: %d failed", osfd);
            return false;
        }

        old_item->SetEvents(new_events);
    }

    return true;
}

bool EventScheduler::Delete(StEventBase *item)
{
    if (NULL == item)
    {
        LOG_ERROR("item input invalid: %p", item);
        return false;
    }

    int osfd = item->GetOsfd();
    if (unlikely(!IsValidFd(osfd)))
    {
        LOG_ERROR("IsValidFd osfd: %d", osfd);
        return false;
    }

    int new_events = item->GetEvents();
    StEventBase *old_item = m_container_[osfd];
    LOG_TRACE("delete old_item: %p, item: %p", old_item, item);
    if (NULL == old_item)
    {
        m_container_[osfd] = item;
        if (!DeleteFd(osfd, new_events))
        {
            LOG_ERROR("del fd: %d failed", osfd);
            return false;
        }
    }
    else
    {
        if (old_item != item)
        {
            LOG_ERROR("item conflict, fd: %d, old: %p, new: %p", 
                osfd, old_item, item);
            return false;
        }

        if (!DeleteFd(osfd, new_events))
        {
            LOG_ERROR("del fd: %d failed", osfd);
            return false;
        }
        
        old_item->SetEvents(new_events);
    }

    return true;
}

bool EventScheduler::AddFd(int fd, int events)
{
    LOG_TRACE("fd: %d, events: %d", fd, events);
    if (unlikely(!IsValidFd(fd)))
    {
        LOG_ERROR("fd: %d not find", fd);
        return false;
    }

    int rc = m_state_->ApiAddEvent(fd, events);
    if (rc < 0)
    {
        LOG_ERROR("add event failed, fd: %d", fd);
        return false;
    }

    return true;
}

bool EventScheduler::DeleteFd(int fd, int events)
{
    LOG_TRACE("fd: %d, events: %d", fd, events);
    if (unlikely(!IsValidFd(fd)))
    {
        LOG_ERROR("fd: %d not find", fd);
        return false;
    }

    int rc = m_state_->ApiDelEvent(fd, events);
    if (rc < 0)
    {
        LOG_ERROR("del event failed, fd: %d", fd);
        return false;
    }

    return true;
}

void EventScheduler::Dispatch(int fdnum)
{
    int ret = 0, osfd = 0, revents = 0;
    StEventBase *item = NULL;

    for (int i = 0; i < fdnum; i++)
    {
        osfd = m_state_->m_fired_[i].fd;
        if (unlikely(!IsValidFd(osfd)))
        {
            LOG_ERROR("fd not find, fd: %d, ev_fdnum: %d", osfd, fdnum);
            continue;
        }

        revents = m_state_->m_fired_[i].mask; // 获取对应的event
        item = m_container_[osfd];
        if (NULL == item)
        {
            LOG_TRACE("item is NULL, fd: %d", osfd);
            DeleteFd(osfd, (revents & (ST_READABLE | ST_WRITEABLE | ST_EVERR)));
            continue;
        }

        StThreadBase *thread = item->GetOwnerThread();
        ASSERT(thread != NULL);

        item->SetRecvEvents(revents); // 设置收到的事件
        if (revents & ST_EVERR)
        {
            LOG_TRACE("ST_EVERR osfd: %d fdnum: %d", osfd, fdnum);
            item->HangupNotify();
            Delete(item);
            continue;
        }

        if (revents & ST_READABLE)
        {
            ret = item->InputNotify();
            LOG_TRACE("ST_READABLE osfd: %d, ret: %d fdnum: %d", 
                osfd, ret, fdnum);
            if (ret != 0)
            {
                LOG_ERROR("revents & ST_READABLE, InputNotify ret: %d", ret);
                continue;
            }

            if (thread != NULL && thread->HasFlag(eIO_LIST))
            {
                m_thread_scheduler_->IOWaitToRunable(thread);
            }

            LOG_TRACE("ST_READABLE ok");
        }

        if (revents & ST_WRITEABLE)
        {
            ret = item->OutputNotify();
            LOG_TRACE("ST_WRITEABLE osfd: %d, ret: %d fdnum: %d", 
                osfd, ret, fdnum);
            if (ret != 0)
            {
                LOG_ERROR("revents & ST_WRITEABLE, OutputNotify ret: %d", ret);
                continue;
            }

            if (thread != NULL && thread->HasFlag(eIO_LIST))
            {
                m_thread_scheduler_->IOWaitToRunable(thread);
            }

            LOG_TRACE("ST_WRITEABLE ok");
        }
    }
}

// 等待触发事件
void EventScheduler::Wait(int timeout)
{
    int wait_time = ST_MIN(m_timeout_, timeout);
    LOG_TRACE("wait_time: %d ms", wait_time);
    int nfd = 0;
    if (wait_time <= 0)
    {
        nfd = m_state_->ApiPoll(NULL);
    }
    else
    {
        struct timeval tv = {0, wait_time * 1000};
        if (wait_time >= 1000)
        {
            tv.tv_sec = (int)(wait_time/1000);
            tv.tv_usec = (wait_time%1000) * 1000;
        }
        nfd = m_state_->ApiPoll(&tv);
    }

    LOG_TRACE("wait poll nfd: %d, --------[name:%s]---------", 
        nfd, m_thread_scheduler_->GetActiveThread()->GetName());

    if (nfd <= 0)
    {
        return ;
    }

    Dispatch(nfd);
    // TODO : 增加优先级调度线程算法
}

// 调度信息
bool EventScheduler::Schedule(StThreadBase *thread, 
        StEventBaseQueue *fdset,
        StEventBase *item,
        uint64_t wakeup_timeout)
{
    if (NULL == thread)
    {
        LOG_ERROR("active thread NULL, schedule failed");
        return false;
    }

    if (NULL != fdset)
    {
        LOG_TRACE("fdset: %p", fdset);
        thread->Add(fdset);
    }

    if (NULL != item)
    {
        LOG_TRACE("item: %p", item);
        thread->Add(item);
    }
    
    thread->SetWakeupTime(wakeup_timeout);
    if (!Add(thread->GetFdSet()))
    {
        LOG_ERROR("add fdset, errno: %d", errno);
        return false;
    }
    
    LOG_TRACE("---------- [name: %s] -----------", thread->GetName());
    m_thread_scheduler_->InsertIOWait(thread); // 线程切换为IO等待
    if (m_thread_scheduler_->GetActiveThread() == thread)
    {
        m_thread_scheduler_->Yield(thread); // 让出当前线程
    }

    int recv_num = 0;
    StEventBaseQueue &recv_fdset = thread->GetFdSet();
    StEventBase *_item = NULL;
    CPP_TAILQ_FOREACH(_item, &recv_fdset, m_next_)
    {
        LOG_TRACE("GetRecvEvents: %d, GetEvents: %d, fd: %d", 
            _item->GetRecvEvents(), 
            _item->GetEvents(), 
            _item->GetOsfd());
        if (_item->GetRecvEvents() != 0)
        {
        	recv_num++;
        }
    }

    Delete(recv_fdset);
    
    // 如果没有收到任何recv事件则表示超时或者异常
    if (recv_num == 0)
    {
        errno = ETIME;
        LOG_ERROR("recv_num: 0");
        return false;
    }

    LOG_TRACE("recv_num: %d", recv_num);

    return true;
}

bool Thread::InitStack()
{
    if (m_stack_)
    {
        LOG_TRACE("m_stack_ = %p", m_stack_);
        return true;
    }

    m_stack_ = (Stack*)calloc(1, sizeof(Stack));
    if (NULL == m_stack_)
    {
        LOG_ERROR("calloc stack failed, size : %ld", sizeof(Stack));
        return false;
    }

    int memsize = MEM_PAGE_SIZE * 2 + (m_stack_size_ / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE;
    // 创建共享内存
    static int zero_fd = -1;
    int mmap_flags = MAP_PRIVATE | MAP_ANON;
    void *vaddr = ::mmap(NULL, memsize, PROT_READ | PROT_WRITE, mmap_flags, zero_fd, 0);
    if (vaddr == (void *)MAP_FAILED)
    {
        LOG_ERROR("mmap stack failed, size : %d", memsize);
        st_safe_free(m_stack_);
        return false;
    }

    m_stack_->m_vaddr_ = (uchar*)vaddr;
    m_stack_->m_vaddr_size_ = memsize;
    m_stack_->m_stk_size_ = m_stack_size_;
    m_stack_->m_id_ = Util::GetUniqid(); // 生成唯一id
    snprintf(m_name_, sizeof(m_name_) - 1, "T%u", m_stack_->m_id_);
    mprotect(m_stack_->m_vaddr_, MEM_PAGE_SIZE, PROT_NONE);

    sigset_t zero;
    memset(&m_stack_->m_context_.uc, 0, sizeof(m_stack_->m_context_.uc));
	sigemptyset(&zero);
	sigprocmask(SIG_BLOCK, &zero, &m_stack_->m_context_.uc.uc_sigmask);

    return true;
}

void Thread::FreeStack()
{
    if (!m_stack_)
    {
        LOG_WARN("m_stack_ == NULL");
        return;
    }

    ::munmap(m_stack_->m_vaddr_, m_stack_->m_vaddr_size_);
    st_safe_free(m_stack_);
}

void Thread::InitContext()
{
    uint32_t tx, ty;
    uint64_t tz = (uint64_t)m_stack_;
    ty = tz;
    tz >>= 16;
    tx = tz >> 16;
    m_stack_->m_private_ = this; // 保存私有数据
    if (getcontext(&m_stack_->m_context_.uc) < 0)
    {
        LOG_ERROR("getcontext error");
        return ;
    }

    m_stack_->m_context_.uc.uc_stack.ss_sp = m_stack_->m_vaddr_+8;
	m_stack_->m_context_.uc.uc_stack.ss_size = m_stack_->m_vaddr_size_-64;

	makecontext(&m_stack_->m_context_.uc, (void(*)())StartActiveThread, 2, ty, tx);
}

void Thread::RestoreContext(StThreadBase *thread)
{
    LOG_TRACE("RestoreContext ### begin ### this : %p, thread : %p", this, thread);
    // 切换的线程相同
    if (thread == this)
    {
        return ;
    }

    LOG_TRACE("================= run thread[name:%s] =================[%p]", 
        this->GetName(), this);
    LOG_TRACE("contextswitch: (%p,%p) -> (%p,%p)", 
        thread, 
        &(thread->GetStack()->m_context_), 
        this, 
        &(this->GetStack()->m_context_)
    );
    
    context_switch(&(thread->GetStack()->m_context_), &(this->GetStack()->m_context_));
    LOG_TRACE("RestoreContext ### end ### this : %p, thread : %p", this, thread);
}

void Thread::Run()
{
    LOG_TRACE("Run begin......");
    // 启动实际入口
    if (NULL != m_callback_)
    {
        m_callback_->Run();
    }

    LOG_TRACE("Run end......");
}