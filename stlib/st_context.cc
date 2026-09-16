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
