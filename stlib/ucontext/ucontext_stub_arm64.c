/*
 * Apple Silicon / aarch64 stub (asm.S has no arm64 path yet).
 */
#if defined(__APPLE__) && defined(__aarch64__)

#include "ucontext.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int getmcontext(mcontext_t *m) {
  (void)m;
  fprintf(stderr, "sthread: getmcontext stub on arm64 (not implemented)\\n");
  return -1;
}

void setmcontext(const mcontext_t *m) {
  (void)m;
  fprintf(stderr, "sthread: setmcontext stub on arm64 (not implemented)\\n");
}

int swapcontext(ucontext_t *oucp, const ucontext_t *ucp) {
  (void)oucp;
  (void)ucp;
  fprintf(stderr, "sthread: swapcontext stub on arm64 (not implemented)\\n");
  return -1;
}

void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...) {
  (void)ucp;
  (void)func;
  (void)argc;
  fprintf(stderr, "sthread: makecontext stub on arm64 (not implemented)\\n");
}

#endif
