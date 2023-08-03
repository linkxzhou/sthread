/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

#ifndef _UCONTEXT_H_
#define _UCONTEXT_H_

#if defined(__sun__)
#define __EXTENSIONS__ 1 /* SunOS */
#if defined(__SunOS5_6__) || defined(__SunOS5_7__) || defined(__SunOS5_8__)
/* NOT USING #define __MAKECONTEXT_V2_SOURCE 1 / * SunOS */
#else
#define __MAKECONTEXT_V2_SOURCE 1
#endif
#endif

#define USE_UCONTEXT 1

#if defined(__OpenBSD__) || defined(__mips__)
#undef USE_UCONTEXT
#define USE_UCONTEXT 0
#endif

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#if defined(MAC_OS_X_VERSION_10_5)
#undef USE_UCONTEXT
#define USE_UCONTEXT 0
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
#if USE_UCONTEXT
#include <ucontext.h>
#endif
#include <inttypes.h>
#include <sys/utsname.h>
#include <stdarg.h>

#define nil ((void *)0)
#define nelem(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long long uvlong;
typedef long long vlong;

#if defined(__FreeBSD__) && __FreeBSD__ < 5
extern int getmcontext(mcontext_t *);
extern void setmcontext(const mcontext_t *);
#define setcontext(u) setmcontext(&(u)->uc_mcontext)
#define getcontext(u) getmcontext(&(u)->uc_mcontext)
extern int swapcontext(ucontext_t *, const ucontext_t *);
extern void makecontext(ucontext_t *, void (*)(), int, ...);
#endif

#if defined(__APPLE__)
#define mcontext libthread_mcontext
#define mcontext_t libthread_mcontext_t
#define ucontext libthread_ucontext
#define ucontext_t libthread_ucontext_t
#if defined(__i386__)
#include "ucontext-386.h"
#elif defined(__x86_64__)
#include "ucontext-amd64.h"
#else
#include "ucontext-power.h"
#endif
#endif

#if defined(__OpenBSD__)
#define mcontext libthread_mcontext
#define mcontext_t libthread_mcontext_t
#define ucontext libthread_ucontext
#define ucontext_t libthread_ucontext_t
#if defined __i386__
#include "ucontext-386.h"
#else
#include "ucontext-power.h"
#endif
extern pid_t rfork_thread(int, void *, int (*)(void *), void *);
#endif

#if 0 && defined(__sun__)
#define mcontext libthread_mcontext
#define mcontext_t libthread_mcontext_t
#define ucontext libthread_ucontext
#define ucontext_t libthread_ucontext_t
#include "ucontext-sparc.h"
#endif

#if defined(__arm__)
int getmcontext(mcontext_t *);
void setmcontext(const mcontext_t *);
#define setcontext(u) setmcontext(&(u)->uc_mcontext)
#define getcontext(u) getmcontext(&(u)->uc_mcontext)
#endif

#if defined(__mips__)
#include "ucontext-mips.h"
int getmcontext(mcontext_t *);
void setmcontext(const mcontext_t *);
#define setcontext(u) setmcontext(&(u)->uc_mcontext)
#define getcontext(u) getmcontext(&(u)->uc_mcontext)
#endif

#if defined(__APPLE__)
#if defined(__i386__)
#define NEEDX86MAKECONTEXT
#define NEEDSWAPCONTEXT
#elif defined(__x86_64__)
#define NEEDAMD64MAKECONTEXT
#define NEEDSWAPCONTEXT
#else
#define NEEDPOWERMAKECONTEXT
#define NEEDSWAPCONTEXT
#endif
#endif

#if defined(__FreeBSD__) && defined(__i386__) && __FreeBSD__ < 5
#define NEEDX86MAKECONTEXT
#define NEEDSWAPCONTEXT
#endif

#if defined(__OpenBSD__) && defined(__i386__)
#define NEEDX86MAKECONTEXT
#define NEEDSWAPCONTEXT
#endif

#if defined(__linux__) && defined(__arm__)
#define NEEDSWAPCONTEXT
#define NEEDARMMAKECONTEXT
#endif

#if defined(__linux__) && defined(__mips__)
#define	NEEDSWAPCONTEXT
#define	NEEDMIPSMAKECONTEXT
#endif

#endif