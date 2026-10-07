/*
 * Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT
 * Restored from git history (stlib/st_ucontext.cc @ 478d209).
 * makecontext/swapcontext live in stlib/ucontext/; this file only
 * owns the scheduler-facing context_switch / context_exit helpers.
 */

#include "stlib/st_context.h"

#if defined(__GNUC__)
#define ST_TLS __thread
#else
#define ST_TLS
#endif

/* Per-OS-thread initial and running contexts (matches historical __THREAD). */
static ST_TLS Context *g_context_initial = 0;
static ST_TLS Context *g_context_running = 0;

void context_init(Context *c) { g_context_initial = c; }

int context_switch(Context *from, Context *to) {
  if (0 == g_context_initial)
    context_init(from);
  if (g_context_running != to)
    g_context_running = to;
  return swapcontext(&from->uc, &to->uc);
}

void context_exit(int _errno) {
  (void)_errno;
  if (g_context_running != 0 && g_context_initial != 0 &&
      g_context_initial != g_context_running) {
    context_switch(g_context_running, g_context_initial);
  }
}

/* ty/tx 用 ulong 拼回指针，要求 ulong 装得下 void*（LP64 满足，LLP64 不满足）。
 */
typedef char st_ulong_holds_pointer[sizeof(ulong) >= sizeof(void *) ? 1 : -1];

int context_make(Stack **out, void (*entry)(), unsigned int stack_size,
                 unsigned int memsize) {
  Stack *stack;
  uint32_t tx, ty;
  uint64_t tz;
  sigset_t zero;

  (void)sizeof(st_ulong_holds_pointer);
  if (out == 0) {
    return -1;
  }
  *out = 0;

  stack = (Stack *)calloc(1, sizeof(Stack));
  if (stack == 0) {
    return -1;
  }
  stack->m_vaddr_ = (uchar *)malloc((size_t)memsize);
  if (stack->m_vaddr_ == 0) {
    free(stack);
    return -1;
  }
  stack->m_vaddr_size_ = (int)memsize;
  stack->m_stk_size_ = (int)stack_size;

  memset(&stack->m_context_.uc, 0, sizeof(stack->m_context_.uc));
  sigemptyset(&zero);
  sigprocmask(SIG_BLOCK, &zero, &stack->m_context_.uc.uc_sigmask);

  tz = (uint64_t)stack;
  ty = (uint32_t)tz;
  tz >>= 16;
  tx = (uint32_t)(tz >> 16);

  if (getcontext(&stack->m_context_.uc) < 0) {
    *out = stack;
    return -2;
  }

  /* Guard + 16-byte SP base (arm64 Darwin). 余量与历史 InitContext 相同。 */
  stack->m_context_.uc.uc_stack.ss_sp = stack->m_vaddr_ + 16;
  stack->m_context_.uc.uc_stack.ss_size = stack->m_vaddr_size_ - 64;
  makecontext(&stack->m_context_.uc, entry, 2, ty, tx);
  *out = stack;
  return 0;
}

void context_free(Stack *stack) {
  if (stack == 0) {
    return;
  }
  if (stack->m_vaddr_ != 0) {
    free(stack->m_vaddr_);
    stack->m_vaddr_ = 0;
  }
  free(stack);
}
