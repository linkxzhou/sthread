/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_thread.h"
#include "mt_frame.h"

ST_NAMESPACE_USING;

// 线程启动
static void StartActiveThread(uint ty, uint tx) 
{
    Stack *t;
	ulong z;

	z = tx<<16;	/* hide undefined 32-bit shift from 32-bit compilers */
	z <<= 16;
	z |= ty;
	t = (Stack*)z;

// TODO : 复用thread
AGAIN:
    LOG_TRACE("StartActiveThread: %p, t: %p, id: %d", t->m_private_, t, t->m_id_);
    Thread* thread = (Thread*)t->m_private_;
    if (thread)
    {
        thread->Run();
        LOG_TRACE("StartActiveThread end ...");
    }

    // 线程退出处理
    if (thread == GetInstance<Frame>()->DaemonThread()) 
    {
        Scheduler::SwitchPrimoThread(thread);
    }
    else
    {
        // 是否重新复用
        if (thread->IsReset())
        {
            goto AGAIN;
        }
        else
        {
            Scheduler::SwitchDaemonThread(thread);
        }
    }
}

// 调度系统
void Scheduler::SwitchDaemonThread(Thread *thread)
{
    Thread *daemon = (Thread *)GetInstance<Frame>()->DaemonThread();
    GetInstance<Frame>()->SetActiveThread(daemon);
    daemon->RestoreContext(thread);
}

void Scheduler::SwitchPrimoThread(Thread *thread)
{
    Thread *primo = (Thread *)GetInstance<Frame>()->PrimoThread();
    GetInstance<Frame>()->SetActiveThread(primo);
    primo->RestoreContext(thread);
}
// 线程切换调度
int Scheduler::ThreadSchedule()
{
    return GetInstance<Frame>()->ThreadSchedule();
}

time64_t Scheduler::GetTime()
{
    return GetInstance<Frame>()->GetLastClock();
}

void Scheduler::Sleep()
{
    Thread* thread = (Thread *)(GetInstance<Frame>()->GetActiveThread());
    if (!thread)
    {
        LOG_ERROR("active thread NULL (%p)", thread);
        return ;
    }

    GetInstance<Frame>()->InsertSleep(thread);
    Scheduler::ThreadSchedule();
}

void Scheduler::Pend()
{
    Thread* thread = (Thread *)(GetInstance<Frame>()->GetActiveThread());
    if (!thread)
    {
        LOG_ERROR("active thread NULL (%p)", thread);
        return ;
    }

    GetInstance<Frame>()->InsertPend(thread);
    Scheduler::ThreadSchedule();
}

void Scheduler::Unpend(Thread* pthread)
{
    Thread* thread = (Thread*)pthread;
    if (!thread)
    {
        LOG_ERROR("thread NULL, (%p)", thread);
        return ;
    }

    GetInstance<Frame>()->RemovePend(thread);
    Scheduler::InsertRunable(thread);
}

void Scheduler::Reclaim()
{
    Thread* thread = (Thread*)(GetInstance<Frame>()->GetActiveThread());
    if (!thread)
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return ;
    }

    GetInstance<Frame>()->FreeThread(thread);
}

int Scheduler::IoWaitToRunable(Thread* thread)
{
    if (!thread)
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return -1;
    }

    Scheduler::RemoveIoWait(thread);
    Scheduler::InsertRunable(thread);
    return 0;
}

int Scheduler::RemoveEvents(int fd, int events)
{
    return GetInstance<Frame>()->GetEventDriver()->DelFd(fd, events);
}

int Scheduler::InsertEvents(int fd, int events)
{
    return GetInstance<Frame>()->GetEventDriver()->AddFd(fd, events);
}

int Scheduler::RemoveIoWait(Thread* thread)
{
    if (NULL == thread)
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return -1;
    }

    GetInstance<Frame>()->RemoveIoWait(thread);
    return 0;
}

int Scheduler::InsertIoWait(Thread* thread)
{
    if (NULL == thread)
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return -1;
    }

    GetInstance<Frame>()->InsertIoWait(thread);
    return 0;
}

int Scheduler::InsertRunable(Thread* thread)
{
    if (NULL == thread)
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return -1;
    }

    GetInstance<Frame>()->InsertRunable(thread);
    return 0;
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

    int memsize = MEM_PAGE_SIZE*2 + m_stack_size_;
    memsize = (memsize + MEM_PAGE_SIZE - 1)/MEM_PAGE_SIZE*MEM_PAGE_SIZE;
    // 创建共享内存
    static int zero_fd = -1;
    int mmap_flags = MAP_PRIVATE | MAP_ANON;
    void* vaddr = mmap(NULL, memsize, PROT_READ | PROT_WRITE, mmap_flags, zero_fd, 0);
    if (vaddr == (void *)MAP_FAILED)
    {
        LOG_ERROR("mmap stack failed, size : %d", memsize);
        safe_free(m_stack_);
        return false;
    }

    m_stack_->m_vaddr_ = (uchar*)vaddr;
    m_stack_->m_vaddr_size_ = memsize;
    m_stack_->m_stk_size_ = m_stack_size_;
    m_stack_->m_id_ = Utils::generate_uniqid(); // 生成唯一id
    mprotect(m_stack_->m_vaddr_, MEM_PAGE_SIZE, PROT_NONE);

    sigset_t zero;
    memset(&m_stack_->m_context_.uc, 0, sizeof m_stack_->m_context_.uc);
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

    munmap(m_stack_->m_vaddr_, m_stack_->m_vaddr_size_);
    safe_free(m_stack_);
}

void Thread::InitContext()
{
    uint tx, ty;
    ulong tz = (ulong)m_stack_;
    ty = tz;
    tz >>= 16;
    tx = tz>>16;
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

// 参数从switch_thread切换到thread
void Thread::RestoreContext(ThreadBase* switch_thread)
{
    LOG_TRACE("this : %p, switch_thread : %p", this, switch_thread);
    // 切换的线程相同
    if (switch_thread == this)
    {
        return ;
    }

    LOG_TRACE("================= run thread =================[%p]", this);
    LOG_TRACE("contextswitch: (%p,%p) -> (%p,%p)", switch_thread, 
        &(((Thread*)switch_thread)->GetStack()->m_context_), 
        this, &(this->GetStack()->m_context_)
    );
    
    contextswitch(&(((Thread*)switch_thread)->GetStack()->m_context_), 
        &(this->GetStack()->m_context_));
    LOG_TRACE("Thread Dispatch ...[%p]", this);
}

void Thread::Run()
{
    LOG_TRACE("Run begin......");
    // 启动实际入口
    if (NULL != m_callback_)
    {
        m_callback_(m_args_);
    }

    if (IsSubThread())
    {
        WakeupParent();
    }

    LOG_TRACE("Run end......");
    // 系统回收Thread
    Scheduler::Reclaim();
    // 线程调度
    Scheduler::ThreadSchedule();
}

// 唤醒父线程
void Thread::WakeupParent()
{
    Thread* parent = dynamic_cast<Thread*>(this->GetParent());
    if (parent)
    {
        parent->RemoveSubThread(this);
        if (parent->HasNoSubThread())
        {
            Scheduler::Unpend(parent);
        }
    }
}

// 用户态现场池
unsigned int ThreadPool::s_default_thread_num_ = DEFAULT_THREAD_NUM;
unsigned int ThreadPool::s_default_stack_size_ = DEFAULT_STACK_SIZE;

// 创建一个线程池
bool ThreadPool::InitialPool(int max_num)
{
    Thread *thread = NULL;
    for (unsigned int i = 0; i < s_default_thread_num_; i++)
    {
        thread = new Thread();
        if ((NULL == thread) || (false == thread->Initial()))
        {
            LOG_ERROR("init pool thread %p init failed", thread);
            safe_delete(thread);
            continue;
        }
        thread->SetFlag(eFREE_LIST);
        m_free_list_.push(thread);
    }

    LOG_TRACE("max_num : %d, size : %d", max_num, m_free_list_.size());

    m_total_num_ = m_free_list_.size();
    m_max_num_ = max_num;
    m_use_num_ = 0;
    if (m_total_num_ <= 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

void ThreadPool::DestroyPool()
{
    Thread* thread = NULL;
    while (!m_free_list_.empty())
    {
        thread = m_free_list_.front();
        m_free_list_.pop();
        safe_delete(thread);
    }
    m_total_num_ = 0;
    m_use_num_ = 0;
}

Thread* ThreadPool::AllocThread()
{
    Thread* thread = NULL;
    if (!m_free_list_.empty())
    {
        thread = m_free_list_.front();
        m_free_list_.pop();
        thread->UnsetFlag(eFREE_LIST);
        m_use_num_++;
        return thread;
    }

    if (m_total_num_ > m_max_num_)
    {
        LOG_ERROR("total_num_: %d, max_num_: %d total_num_ > max_num_", 
            m_total_num_, m_max_num_);
        return NULL;
    }

    thread = new Thread();
    if ((NULL == thread) || (false == thread->Initial()))
    {
        LOG_ERROR("thread alloc failed, thread: %p", thread);
        safe_delete(thread);
        return NULL;
    }

    m_total_num_++;
    m_use_num_++;

    return thread;
}

void ThreadPool::FreeThread(Thread* thread)
{
    thread->Reset();
    m_use_num_--;
    m_free_list_.push(thread);
    thread->SetFlag(eFREE_LIST);
    
    // 对于预分配大于的情况下需要删除
    unsigned int free_num = m_free_list_.size();
    if ((free_num > s_default_thread_num_) && (free_num > 1))
    {
        thread = m_free_list_.front();
        m_free_list_.pop();
        safe_delete(thread);
        m_total_num_--;
    }
}

int EventDriver::Init(int max_num)
{
    m_maxfd_ = mt_max(max_num, m_maxfd_);
    int rc = m_state_->ApiCreate(m_maxfd_);
    if (rc < 0)
    {
        rc = -2;
        goto EXIT_LABEL;
    }

    m_ev_array_ = (EventerPtr*)malloc(sizeof(EventerPtr) * m_maxfd_);
    if (NULL == m_ev_array_)
    {
        rc = -3;
        goto EXIT_LABEL;
    }
    // 初始化设置为空指针
    memset(m_ev_array_, 0, sizeof(EventerPtr) * m_maxfd_);

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

EXIT_LABEL:
    if (rc < 0)
    {
        LOG_ERROR("rc : %d error", rc);
        Close();
    }

    return rc;
}       

void EventDriver::Close()
{
    safe_delete_arr(m_ev_array_);
    m_state_->ApiFree(); // 释放链接
}

bool EventDriver::AddList(EventerList &list)
{
    bool ret = true;
    // 保存最后一个出错的位置
    Eventer *ev = NULL, *ev_error = NULL; 

    CPP_TAILQ_FOREACH(ev, &list, m_entry_)
    {
        if (!AddEventer(ev))
        {
            LOG_ERROR("ev add failed, fd: %d", ev->GetOsfd());
            ev_error = ev;
            ret = false;

            goto EXIT_LABEL;
        }
        LOG_TRACE("ev add success ev: %p, fd: %d", ev, ev->GetOsfd());
    }

EXIT_LABEL:
    // 如果失败则回退
    if (!ret)
    {
        CPP_TAILQ_FOREACH(ev, &list, m_entry_)
        {
            if (ev == ev_error)
            {
                break;
            }
            DelEventer(ev);
        }
    }

    return ret;
}

bool EventDriver::DelList(EventerList &list)
{
    bool ret = true;
    // 保存最后一个出错的位置
    Eventer *ev = NULL, *ev_error = NULL; 

    CPP_TAILQ_FOREACH(ev, &list, m_entry_)
    {
        if (!DelEventer(ev))
        {
            LOG_ERROR("ev del failed, fd: %d", ev->GetOsfd());
            ev_error = ev;
            ret = false;

            goto EXIT_LABEL1;
        }
    }

EXIT_LABEL1:
    // 如果失败则回退
    if (!ret)
    {
        CPP_TAILQ_FOREACH(ev, &list, m_entry_)
        {
            if (ev == ev_error)
            {
                break;
            }
            AddEventer(ev);
        }
    }

    return ret;
}

bool EventDriver::AddEventer(Eventer *ev)
{
    if (NULL == ev)
    {
        LOG_ERROR("ev input invalid, %p", ev);
        return false;
    }

    int osfd = ev->GetOsfd();
    // 非法的fd直接返回
    if (unlikely(!IsValidFd(osfd)))
    {
        LOG_ERROR("IsValidFd osfd, %d", osfd);
        return false;
    }

    int new_events = ev->GetEvents();
    Eventer* old_ev = m_ev_array_[osfd];
    LOG_TRACE("AddEventer old_ev: %p, ev: %p", old_ev, ev);
    if (NULL == old_ev)
    {
        m_ev_array_[osfd] = ev;
        if (!AddFd(osfd, new_events))
        {
            LOG_ERROR("add fd: %d failed", osfd);
            return false;
        }
    }
    else
    {
        if (old_ev != ev)
        {
            LOG_ERROR("ev conflict, fd: %d, old: %p, now: %p", osfd, old_ev, ev);
            return false;
        }

        // 获取原来的events
        int old_events = old_ev->GetEvents();
        new_events = old_events | new_events;
        if (old_events == new_events)
        {
            return true;
        }
        if (!AddFd(osfd, new_events))
        {
            LOG_ERROR("add fd: %d failed", osfd);
            return false;
        }
        // 设置新的events
        old_ev->SetEvents(new_events);
    }

    return true;
}

bool EventDriver::DelEventer(Eventer* ev)
{
    if (NULL == ev)
    {
        LOG_ERROR("ev input invalid, %p", ev);
        return false;
    }

    int osfd = ev->GetOsfd();
    // 非法的fd直接返回
    if (unlikely(!IsValidFd(osfd)))
    {
        LOG_ERROR("IsValidFd osfd, %d", osfd);
        return false;
    }

    int new_events = ev->GetEvents();
    Eventer* old_ev = m_ev_array_[osfd];
    LOG_TRACE("DelEventer old_ev: %p, ev: %p", old_ev, ev);
    if (NULL == old_ev)
    {
        m_ev_array_[osfd] = ev;
        if (!DelFd(osfd, new_events))
        {
            LOG_ERROR("del fd: %d failed", osfd);
            return false;
        }
    }
    else
    {
        if (old_ev != ev)
        {
            LOG_ERROR("ev conflict, fd: %d, old: %p, now: %p", osfd, old_ev, ev);
            return false;
        }
        // TODO : 处理异常
        int old_events = old_ev->GetEvents();
        new_events = old_events & ~new_events;
        if (!DelFd(osfd, new_events))
        {
            LOG_ERROR("del fd: %d failed", osfd);
            return false;
        }
        // 去除删除的属性
        old_ev->SetEvents(new_events);
    }

    return true;
}

bool EventDriver::AddFd(int fd, int events)
{
    LOG_TRACE("fd : %d, events: %d", fd, events);
    if (unlikely(!IsValidFd(fd)))
    {
        LOG_ERROR("fd : %d not find", fd);
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

bool EventDriver::DelFd(int fd, int events)
{
    return DelRef(fd, events, false);
}

bool EventDriver::DelRef(int fd, int events, bool useref)
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

    if (useref)
    {
        // TODO : 需要对引用处理
        // pass
    }

    return true;
}

void EventDriver::DisposeEventerList(int ev_fdnum)
{
    int ret = 0, osfd = 0, revents = 0;
    Eventer* ev = NULL;

    for (int i = 0; i < ev_fdnum; i++)
    {
        osfd = m_state_->m_fired_[i].fd;
        if (unlikely(!IsValidFd(osfd)))
        {
            LOG_ERROR("fd not find, fd: %d, ev_fdnum: %d", osfd, ev_fdnum);
            continue;
        }

        revents = m_state_->m_fired_[i].mask; // 获取对应的event
        ev = m_ev_array_[osfd];
        if (NULL == ev)
        {
            LOG_TRACE("ev is NULL, fd: %d", osfd);
            DelFd(osfd, (revents & (ST_READABLE | ST_WRITEABLE | ST_EVERR)));
            continue;
        }
        ev->SetRecvEvents(revents); // 设置收到的事件
        if (revents & ST_EVERR)
        {
            LOG_TRACE("ST_EVERR osfd: %d ev_fdnum: %d", osfd, ev_fdnum);
            ev->HangupNotify();
            continue;
        }
        if (revents & ST_READABLE)
        {
            ret = ev->InputNotify();
            LOG_TRACE("ST_READABLE osfd: %d, ret: %d ev_fdnum: %d", osfd, ret, ev_fdnum);
            if (ret != 0)
            {
                LOG_ERROR("revents & ST_READABLE, InputNotify ret: %d", ret);
                continue;
            }
        }
        if (revents & ST_WRITEABLE)
        {
            ret = ev->OutputNotify();
            LOG_TRACE("ST_WRITEABLE osfd: %d, ret: %d ev_fdnum: %d", osfd, ret, ev_fdnum);
            if (ret != 0)
            {
                LOG_ERROR("revents & ST_WRITEABLE, OutputNotify ret: %d", ret);
                continue;
            }
        }
    }
}

//  等待触发事件
void EventDriver::Dispatch()
{
    int wait_time = GetTimeout(); // 获取需要等待时间
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
    LOG_TRACE("wait poll nfd: %d", nfd);
    if (nfd <= 0)
    {
        return ;
    }
    DisposeEventerList(nfd);
}

// 调度信息
bool EventDriver::Schedule(ThreadBase* thread, EventerList* ev_list, 
        Eventer* ev, time64_t wakeup_timeout)
{
    // 当前的active thread调度
    if (NULL == thread)
    {
        LOG_ERROR("active thread NULL, eventer schedule failed");
        return false;
    }

    // 清空之前的句柄列表
    DelList(thread->GetFdSet());
    thread->ClearAllFd();

    if (ev_list)
    {
        thread->AddFdList(ev_list);
    }
    if (ev)
    {
        thread->AddFd(ev);
    }
    
    // 设置微线程的唤醒时间
    thread->SetWakeupTime(wakeup_timeout);
    if (!AddList(thread->GetFdSet()))
    {
        LOG_ERROR("list add failed, errno: %d", errno);
        return false;
    }
    thread->InsertIoWait(); // 线程切换为IO等待
    thread->SwitchContext(); // 切换上下文

    LOG_TRACE("thread: %p", thread);
    int recv_num = 0;
    EventerList& recv_fds = thread->GetFdSet();
    Eventer* _ev = NULL;
    CPP_TAILQ_FOREACH(_ev, &recv_fds, m_entry_)
    {
        LOG_TRACE("GetRecvEvents: %d, GetEvents: %d, fd: %d", 
            _ev->GetRecvEvents(), _ev->GetEvents(), _ev->GetOsfd());
        if (_ev->GetRecvEvents() != 0)
        {
        	recv_num++;
        }
    }
    DelList(recv_fds);
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

void IMtConnection::ResetEventer()
{
    // 释放event
    if (NULL != m_ev_)
    {
        GetInstance<EventerPool>()->FreeEventer(m_ev_);
    }
    m_ev_ = NULL;
}