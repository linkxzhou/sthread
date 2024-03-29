#ifndef _UCONTEXT_C_
#define _UCONTEXT_C_

#include "ucontext.h"

#ifdef NEEDPOWERMAKECONTEXT
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...) {
  ulong *sp, *tos;
  va_list arg;

  tos = (ulong *)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size / sizeof(ulong);
  sp = tos - 16;
  ucp->mc.pc = (long)func;
  ucp->mc.sp = (long)sp;
  va_start(arg, argc);
  ucp->mc.r3 = va_arg(arg, long);
  va_end(arg);
}
#endif

#ifdef NEEDX86MAKECONTEXT
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...) {
  int *sp;

  sp = (int *)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size / 4;
  sp -= argc;
  sp = (void *)((uintptr_t)sp - (uintptr_t)sp % 16); /* 16-align for OS X */
  memmove(sp, &argc + 1, argc * sizeof(int));

  *--sp = 0; /* return address */
  ucp->uc_mcontext.mc_eip = (long)func;
  ucp->uc_mcontext.mc_esp = (int)sp;
}
#endif

#ifdef NEEDAMD64MAKECONTEXT
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...) {
  long *sp;
  va_list va;

  memset(&ucp->uc_mcontext, 0, sizeof ucp->uc_mcontext);
  assert(argc == 2);
  va_start(va, argc);
  ucp->uc_mcontext.mc_rdi = va_arg(va, int);
  ucp->uc_mcontext.mc_rsi = va_arg(va, int);
  va_end(va);
  sp = (long *)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size / sizeof(long);
  sp -= argc;
  sp = (void *)((uintptr_t)sp - (uintptr_t)sp % 16); /* 16-align for OS X */
  *--sp = 0;                                         /* return address */
  ucp->uc_mcontext.mc_rip = (long)func;
  ucp->uc_mcontext.mc_rsp = (long)sp;
}
#endif

#ifdef NEEDARMMAKECONTEXT
void makecontext(ucontext_t *uc, void (*fn)(void), int argc, ...) {
  int i, *sp;
  va_list arg;

  sp = (int *)uc->uc_stack.ss_sp + uc->uc_stack.ss_size / 4;
  va_start(arg, argc);
  for (i = 0; i < 4 && i < argc; i++)
    uc->uc_mcontext.gregs[i] = va_arg(arg, uint);
  va_end(arg);
  uc->uc_mcontext.gregs[13] = (uint)sp;
  uc->uc_mcontext.gregs[14] = (uint)fn;
}
#endif

#ifdef NEEDMIPSMAKECONTEXT
void makecontext(ucontext_t *uc, void (*fn)(void), int argc, ...) {
  int i, *sp;
  va_list arg;

  va_start(arg, argc);
  sp = (int *)uc->uc_stack.ss_sp + uc->uc_stack.ss_size / 4;
  for (i = 0; i < 4 && i < argc; i++)
    uc->uc_mcontext.mc_regs[i + 4] = va_arg(arg, int);
  va_end(arg);
  uc->uc_mcontext.mc_regs[29] = (int)sp;
  uc->uc_mcontext.mc_regs[31] = (int)fn;
}
#endif

#ifdef NEEDSWAPCONTEXT
int swapcontext(ucontext_t *oucp, const ucontext_t *ucp) {
  if (getcontext(oucp) == 0)
    setcontext(ucp);
  return 0;
}
#endif

#endif