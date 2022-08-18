/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

#ifndef _ST_UCONTEXT_C_H_
#define _ST_UCONTEXT_C_H_

#if defined(__sun__)
#define __EXTENSIONS__ 1 /* SunOS */
#if defined(__SunOS5_6__) || defined(__SunOS5_7__) || defined(__SunOS5_8__)
/* NOT USING #define __MAKECONTEXT_V2_SOURCE 1 / * SunOS */
#else
#define __MAKECONTEXT_V2_SOURCE 1
#endif
#endif

#define _UCONTEXT 1

#if defined(__OpenBSD__) || defined(__mips__)
#undef _UCONTEXT
#define _UCONTEXT 0
#endif

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#if defined(MAC_OS_X_VERSION_10_5)
#pragma message("MAC_OS_X_VERSION_10_5 activated!")
#undef _UCONTEXT
#define _UCONTEXT 0
#endif
#endif

#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if _UCONTEXT
#include <ucontext.h>
#endif

#include "st_def.h"
#include <inttypes.h>
#include <sys/utsname.h>

#if defined(__APPLE__)
#define mcontext libthread_mcontext
#define mcontext_t libthread_mcontext_t
#define ucontext libthread_ucontext
#define ucontext_t libthread_ucontext_t
#if defined(__i386__)
#include "st_ucontext_386.h"
#pragma message("__APPLE__ __i386__ activated!")
#elif defined(__x86_64__)
#include "st_ucontext_amd64.h"
#pragma message("__APPLE__ __x86_64__ activated!")
#else
#include "st_ucontext_power.h"
#pragma message("__APPLE__ default activated!")
#endif

#elif defined(__arm__)
int getmcontext(mcontext_t *);
void setmcontext(const mcontext_t *);
#define setcontext(u) setmcontext(&(u)->uc_mcontext)
#define getcontext(u) getmcontext(&(u)->uc_mcontext)
#pragma message("__arm__ default activated!")

#elif defined(__mips__)
#include "st_ucontext_mips.h"
int getmcontext(mcontext_t *);
void setmcontext(const mcontext_t *);
#define setcontext(u) setmcontext(&(u)->uc_mcontext)
#define getcontext(u) getmcontext(&(u)->uc_mcontext)
#pragma message("__mips__ default activated!")

#elif defined(__FreeBSD__)
#define mcontext libthread_mcontext
#define mcontext_t libthread_mcontext_t
#define ucontext libthread_ucontext
#define ucontext_t libthread_ucontext_t

#if defined __i386__
#include "st_ucontext_386.h"
#pragma message("__OpenBSD__ __i386__ activated!")
#else
#include "st_ucontext_power.h"
#pragma message("__OpenBSD__ default activated!")
#endif
extern pid_t rfork_thread(int, void *, int (*)(void *), void *);

#else
typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long long uvlong;
typedef long long vlong;

extern int getcontext(ucontext_t *);
extern int setcontext(const ucontext_t *);
extern int swapcontext(ucontext_t *, const ucontext_t *);
extern void makecontext(ucontext_t *, void (*)(), int, ...);
#pragma message("__FreeBSD__ < 5 activated!")

#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  STACK = 260096 // 256K
};

typedef unsigned char uchar;

typedef struct Context {
  ucontext_t uc;
} Context;

typedef struct ustack {
  int m_stk_size_;
  int m_vaddr_size_;
  uchar *m_vaddr_;
  void *m_private_;
  Context m_context_;
  uint m_id_;
} Stack;

void context_init(Context *c);

int context_switch(Context *from, Context *to);

void context_exit(int _errno);

#ifdef __cplusplus
}
#endif

#endif