/* Finite arm64/libthread ucontext smoke (no StThread daemon loop). */
#include "stlib/ucontext/ucontext.h"
#include <stdio.h>

static ucontext_t main_uc, child_uc;
static volatile int child_ran;

static void child(void) {
  child_ran = 1;
  printf("child running\n");
  fflush(stdout);
  setcontext(&main_uc);
}

int main(void) {
  char stack[64 * 1024];
  child_ran = 0;
  if (getcontext(&child_uc) < 0) {
    perror("getcontext");
    return 1;
  }
  child_uc.uc_stack.ss_sp = stack;
  child_uc.uc_stack.ss_size = sizeof stack;
  child_uc.uc_link = 0;
  makecontext(&child_uc, child, 2, 0, 0);
  printf("main before swap\n");
  fflush(stdout);
  if (swapcontext(&main_uc, &child_uc) < 0) {
    perror("swapcontext");
    return 2;
  }
  printf("main after swap, child_ran=%d\n", child_ran);
  return child_ran == 1 ? 0 : 3;
}
