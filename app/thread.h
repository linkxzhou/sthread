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

// 线程启动
static void ActiveThreadCallback(uint ty, uint tx) {
  LOG_TRACE("---------- ActiveThreadCallback -----------");

  Stack *t;
  ulong z;

  z = tx << 16; /* hide undefined 32-bit shift from 32-bit compilers */
  z <<= 16;
  z |= ty;
  t = (Stack *)z;

  LOG_TRACE("ActiveThreadCallback: %p, t: %p, id: %d", t->m_private_, t,
            t->m_id_);
  StThread *thread = (StThread *)(t->m_private_);
  StThreadSchedule *schedule = Instance<StThreadSchedule>();
  if (NULL != thread) {
    LOG_TRACE("---------- [\\\name: %s\\\] -----------", thread->GetName());
    thread->Run();

    // 判断当前线程是否有子线程
    if (thread->IsSubThread()) {
      schedule->WakeupParent(thread);
    }

    // TODO: 系统回收Thread
    // Scheduler::Reclaim();
    schedule->Yield(thread);
  }

  LOG_TRACE("---------- [///name: %s///] -----------", thread->GetName());
  if (thread == scheduler->DaemonThread()) {
    scheduler->SwitchThread(scheduler->PrimoThread(), thread);
  } else {
    scheduler->SwitchThread(scheduler->DaemonThread(), thread);
  }

  context_exit();
}

bool Thread::InitStack() {
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
  // TODO: 修改mmap
  // 创建共享内存
  // static int zero_fd = -1;
  // int mmap_flags = MAP_PRIVATE | MAP_ANON;
  // void *vaddr = ::mmap(NULL, memsize, PROT_READ | PROT_WRITE, mmap_flags,
  // zero_fd, 0); if (vaddr == (void *)MAP_FAILED)
  // {
  //     LOG_ERROR("mmap stack failed, size : %d", memsize);
  //     st_safe_free(m_stack_);
  //     return false;
  // }

  void *vaddr = malloc(memsize * sizeof(uchar));
  m_stack_->m_vaddr_ = (uchar *)vaddr;
  m_stack_->m_vaddr_size_ = memsize;
  m_stack_->m_stk_size_ = m_stack_size_;
  m_stack_->m_id_ = Util::GetUniqid(); // 生成唯一id
  snprintf(m_name_, sizeof(m_name_) - 1, "T%u", m_stack_->m_id_);
  // TODO: 修改mmap
  // mprotect(m_stack_->m_vaddr_, MEM_PAGE_SIZE, PROT_NONE);

  sigset_t zero;
  memset(&m_stack_->m_context_.uc, 0, sizeof(m_stack_->m_context_.uc));
  sigemptyset(&zero);
  sigprocmask(SIG_BLOCK, &zero, &m_stack_->m_context_.uc.uc_sigmask);

  return true;
}

void Thread::FreeStack() {
  if (NULL == m_stack_) {
    LOG_WARN("m_stack_ == NULL");
    return;
  }

  // TODO: 修改mmap
  // ::munmap(m_stack_->m_vaddr_, m_stack_->m_vaddr_size_);
  st_safe_free(m_stack_);
}

void Thread::InitContext() {
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

  makecontext(&m_stack_->m_context_.uc, (void (*)())ActiveThreadCallback, 2, ty,
              tx);
}

void Thread::RestoreContext(StThreadItem *thread) {
  LOG_TRACE("RestoreContext ### begin ### this: %p, thread: %p", this, thread);
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

void Thread::Run() {
  LOG_TRACE("Run begin......");
  // 启动实际入口
  if (NULL != m_callback_) {
    m_callback_->Run();
  }

  LOG_TRACE("Run end......");
}