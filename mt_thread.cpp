#include "mt_thread.h"
#include "mt_frame.h"

MTHREAD_NAMESPACE_USING;

#if MT_UTHREAD

extern "C" int save_context(jmp_buf jbf);
extern "C" void restore_context(jmp_buf jbf, int ret);
extern "C" void replace_esp(jmp_buf jbf, void* esp);

// =============== UThread调用 ===============
void UThread::CleanState()
{
    CPP_TAILQ_INIT(&m_fdset_);
    CPP_TAILQ_INIT(&m_sub_list_);
    m_flag_ = eNOT_INLIST;
    m_type_ = eNORMAL;
    m_state_ = eINITIAL;
    m_runfunc_ = NULL;
    m_args_ = NULL;
    m_parent_ = NULL;
}

void UThread::AddSubThread(UThread* sub)
{
    if (!sub->HasFlag(eSUB_LIST))
    {
        CPP_TAILQ_INSERT_TAIL(&m_sub_list_, sub, m_sub_entry_);
        sub->m_parent_ = this;
    }

    sub->SetFlag(eSUB_LIST);
}

// thread睡眠ms
void Thread::Sleep(int ms)
{
    utime64_t now = m_scheduler_->GetTime();
    m_wakeup_time_ = now + ms;
    LOG_TRACE("now :%ld, m_wakeup_time_ :%ld", now, m_wakeup_time_);

    if (save_context(m_jmpbuf_) == 0)
    {
        LOG_TRACE("Sleep doing");
        m_scheduler_->Sleep();
    }
}

void Thread::Wait()
{
    if (save_context(m_jmpbuf_) == 0)
    {
        m_scheduler_->Pend(); // 线程组塞
    }
}

#endif

// Scheduler静态调度系统
void Scheduler::ThreadSchedule()
{
    GetInstance<Frame>()->ThreadSchedule();
}
utime64_t Scheduler::GetTime()
{
    return GetInstance<Frame>()->GetLastClock();
}
void Scheduler::Sleep()
{
    Thread* thread = (Thread *)(GetInstance<Frame>()->GetActiveThread());
    if (!thread)
    {
        LOG_ERROR("active thread NULL (%p)", thread);
        return;
    }
    GetInstance<Frame>()->InsertSleep(thread);
    ThreadSchedule();
}
void Scheduler::Pend()
{
    Thread* thread = (Thread *)(GetInstance<Frame>()->GetActiveThread());
    if (!thread)
    {
        LOG_ERROR("active thread NULL (%p)", thread);
        return;
    }
    GetInstance<Frame>()->InsertPend(thread);
    ThreadSchedule();
}
void Scheduler::Unpend(Thread* pthread)
{
    Thread* thread = (Thread*)pthread;
    if (!thread)
    {
        LOG_ERROR("pthread NULL (%p)", thread);
        return;
    }
    GetInstance<Frame>()->RemovePend(thread);
    GetInstance<Frame>()->InsertRunable(thread);
}
void Scheduler::Reclaim()
{
    Thread* thread = (Thread*)(GetInstance<Frame>()->GetActiveThread());
    if (!thread)
    {
        LOG_ERROR("active thread NULL, (%p)", thread);
        return;
    }
    GetInstance<Frame>()->FreeThread(thread);
}
void Scheduler::StartActiveThread(uint ty, uint tx) 
{
    Stack *t;
	ulong z;

	z = tx<<16;	/* hide undefined 32-bit shift from 32-bit compilers */
	z <<= 16;
	z |= ty;
	t = (Stack*)z;

    LOG_TRACE("StartActiveThread : %p, t : %p, id : %d", 
        GetInstance<Frame>()->GetActiveThread(), t, t->m_id_);
    Thread* thread = (Thread*)(GetInstance<Frame>()->GetActiveThread());
    if (!thread)
    {
        LOG_ERROR("active thread NULL (%p)", thread);
        goto EXIT;
    }
    thread->Run();

EXIT:
    contextexit();
}
int Scheduler::IoWaitToRunable(Thread* thread)
{
    if (!thread)
    {
        LOG_ERROR("active thread NULL (%p)", thread);
        return -1;
    }
    GetInstance<Frame>()->RemoveIoWait(thread);
    GetInstance<Frame>()->InsertRunable(thread);
    return 0;
}
int Scheduler::RemoveEvents(int fd, int events)
{
    return GetInstance<Frame>()->GetEventProxyer()->DelFd(fd, events);
}
int Scheduler::InsertEvents(int fd, int events)
{
    return GetInstance<Frame>()->GetEventProxyer()->AddFd(fd, events);
}
int Scheduler::RemoveIoWait(Thread* thread)
{
    GetInstance<Frame>()->RemoveIoWait(thread);
    return 0;
}
int Scheduler::InsertIoWait(Thread* thread)
{
    GetInstance<Frame>()->InsertIoWait(thread);
    return 0;
}
int Scheduler::InsertRunable(Thread* thread)
{
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
    // LOG_TRACE("vaddr : %p", vaddr);
    m_stack_->m_vaddr_ = (uchar*)vaddr;
    m_stack_->m_vaddr_size_ = memsize;
    m_stack_->m_stk_size_ = m_stack_size_;
    m_stack_->m_id_ = Utils::generate_uniqid(); // 生成唯一id
    mprotect(m_stack_->m_vaddr_, MEM_PAGE_SIZE, PROT_NONE);

    sigset_t zero;
    memset(&m_stack_->m_context_.uc, 0, sizeof m_stack_->m_context_.uc);
	sigemptyset(&zero);
	sigprocmask(SIG_BLOCK, &zero, &m_stack_->m_context_.uc.uc_sigmask);
    m_stack_->m_context_.uc.uc_stack.ss_sp = m_stack_->m_vaddr_+8;
	m_stack_->m_context_.uc.uc_stack.ss_size = m_stack_->m_vaddr_size_-64;

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
    // LOG_TRACE("init context, m_stack_ : %p", m_stack_);
    uint tx, ty;
	ulong tz = (ulong)m_stack_;
	ty = tz;
	tz >>= 16;
	tx = tz>>16;
	if (getcontext(&m_stack_->m_context_.uc) < 0)
    {
		LOG_ERROR("getcontext error");
		return ;
	}
	makecontext(&m_stack_->m_context_.uc, (void(*)())Scheduler::StartActiveThread, 2, ty, tx);
}

void Thread::RestoreContext(ThreadBase* switch_thread)
{
    LOG_TRACE("this : %p, switch_thread : %p", this, switch_thread);
    LOG_TRACE("================= run thread =================[%p]", this);
    contextswitch(&(((Thread*)switch_thread)->GetStack()->m_context_), 
        &(this->GetStack()->m_context_));
    // TODO :
    // GetInstance<Frame>()->GetEventProxyer()->Dispatch();
    // LOG_TRACE("Thread Dispatch ...");
}

void Thread::Run()
{
    LOG_TRACE("Run ......");
    // 启动实际入口
    if (NULL != m_runfunc_)
    {
        m_runfunc_(m_args_);
    }
    if (IsSubThread())
    {
        WakeupParent();
    }
    // 线程调度
    Scheduler::Reclaim();
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
    else
    {
        LOG_ERROR("sub thread no parent");
    }
}

bool Thread::HasNoSubThread()
{
    return CPP_TAILQ_EMPTY(&m_sub_list_);
}

void Thread::RemoveSubThread(Thread* sub)
{
    if (sub->HasFlag(eSUB_LIST))
    {
        CPP_TAILQ_REMOVE(&m_sub_list_, sub, m_sub_entry_);
        sub->m_parent_ = NULL;
    }
    sub->UnsetFlag(eSUB_LIST);
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
            if (thread)
            {
                safe_delete(thread);
            }
            continue;
        }
        thread->SetFlag(eFREE_LIST);
        m_free_list_.push(thread);
    }

    m_total_num_ = m_free_list_.size();
    m_max_num_  = max_num;
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
        thread->Destroy();
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
        LOG_ERROR("total_num_ : %d, max_num_ : %d total_num_ > max_num_", 
            m_total_num_, m_max_num_);
        return NULL;
    }

    thread = new Thread();
    if ((NULL == thread) || (false == thread->Initial()))
    {
        LOG_ERROR("thread alloc failed, thread: %p", thread);
        if (thread)
        {
            safe_delete(thread);
        }

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
        thread->Destroy();
        safe_delete(thread);
        m_total_num_--;
    }
}