/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 编译期平台选择。业务代码读这里的宏，不要再散落 __APPLE__ / __linux__。
 * 只放宏，不放需要链接的符号。asm.S 仍用编译器预定义宏（见 plan/10）。
 */

#ifndef _ST_PLATFORM_H_
#define _ST_PLATFORM_H_

#if defined(__ANDROID__)
#define ST_OS_ANDROID 1
#else
#define ST_OS_ANDROID 0
#endif

#if defined(__APPLE__)
#define ST_OS_DARWIN 1
#else
#define ST_OS_DARWIN 0
#endif

/* Android 的三元组同时定义 __linux__，这里把两者分开。 */
#if defined(__linux__) && !ST_OS_ANDROID
#define ST_OS_LINUX 1
#else
#define ST_OS_LINUX 0
#endif

#if defined(__OpenBSD__)
#define ST_OS_OPENBSD 1
#else
#define ST_OS_OPENBSD 0
#endif

#if defined(__FreeBSD__)
#define ST_OS_FREEBSD 1
#else
#define ST_OS_FREEBSD 0
#endif

/*
 * 与改动前 src/st_poll.h 一致：Apple / OpenBSD 走 kqueue，其余走 epoll。
 * FreeBSD 仍落在 epoll（本期不接 kqueue）。Android 走 epoll。
 */
#if ST_OS_DARWIN || ST_OS_OPENBSD
#define ST_POLL_KQUEUE 1
#define ST_POLL_EPOLL 0
#else
#define ST_POLL_KQUEUE 0
#define ST_POLL_EPOLL 1
#endif

/*
 * 1：dlsym 找真实 syscall，失败则直接调用（plan/10 D7）。
 * 0：不包含 dlfcn.h，REAL_FUNC 直接是系统调用。本期默认 1；
 *    没有 dlfcn 的平台以后再把默认改成 0。
 */
#ifndef ST_HOOK
#define ST_HOOK 1
#endif

#endif /* _ST_PLATFORM_H_ */
