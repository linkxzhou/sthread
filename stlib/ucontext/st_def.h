/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_DEF_H_
#define _ST_DEF_H_

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <queue>
#include <set>
#include <map>
#include <vector>
#include <errno.h>

#ifdef ST_THREAD
    #define __THREAD        __thread // 判断是否要支持多线程
#else
    #define __THREAD    
#endif

#if __GNUC__ == 2 && __GNUC_MINOR__ < 96 
#define __builtin_expect(x, expected_value) (x) 
#endif

#define likely(x)   __builtin_expect((x),1) 
#define unlikely(x) __builtin_expect((x),0)

#define st_safe_delete(ptr)     do          \
    {                                       \
        if (ptr != NULL) delete ptr;        \
        ptr = NULL;                         \
    } while(0)

#define st_safe_delete_array(ptr)   do      \
    {                                       \
        if (ptr != NULL) delete [] ptr;     \
        ptr = NULL;                         \
    } while(0)

#define st_safe_free(ptr)   do              \
    {                                       \
        if (ptr != NULL) free(ptr);         \
        ptr = NULL;                         \
    } while(0)

// 对应的错误信息
#define ST_OK           0
#define ST_ERROR        -1
#define ST_UNKOWN       -2
#define ST_BUSY         -3
#define ST_DONE         -4
#define ST_DECLINED     -5
#define ST_ABORT        -6

#define ST_NONE 		0
#define ST_READABLE 	1 // EPOLLIN
#define	ST_WRITEABLE 	2 // EPOLLOUT
#define ST_EVERR		4 // ERR, HUP

#define ST_HOOK_MAX_FD      65535 * 2
#define ST_FD_FLG_NOUSE     0x0
#define ST_FD_FLG_INUSE     0x1
#define ST_FD_FLG_UNBLOCK   0x2
#define ST_LISTEN_LEN       1024

#define ST_DEBUG            true

#define ST_MAXINT           0x7fffffff
#define ST_MAXTIME          2177280000

#ifndef ST_SENDRECV_BUFFSIZE
    #define ST_SENDRECV_BUFFSIZE    8192
#endif

#define ST_ALGIN(size)      ((size) + (8-(size)%8)) // 边界对齐

#define ST_NELEMS(a)        ((sizeof(a)) / sizeof((a)[0]))

#define ST_MIN(a, b)           ((a) < (b) ? (a) : (b))
#define ST_MAX(a, b)           ((a) > (b) ? (a) : (b))

#define ST_SQUARE(d)           ((d) * (d))
#define ST_VAR(s, s2, n)       (((n) < 2) ? 0.0 : ((s2) - ST_SQUARE(s)/(n)) / ((n) - 1))
#define ST_STDDEV(s, s2, n)    (((n) < 2) ? 0.0 : sqrt(ST_VAR((s), (s2), (n))))

#define stlib_namespace_begin   namespace stlib {
#define stlib_namespace_end     }
#define stlib_namespace_using   using namespace stlib;

#endif