/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_PUBLICLIB_H_INCLUDED_
#define _MT_PUBLICLIB_H_INCLUDED_

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <math.h>
#include <queue>
#include <set>
#include <map>
#include <vector>
#include <errno.h>

#define MTHREAD_NAMESPACE_BEGIN namespace mthread {
#define MTHREAD_NAMESPACE_END   }
#define MTHREAD_NAMESPACE_USING using namespace mthread;

#define mt_min(x,y) (((x)<(y))?(x):(y))
#define mt_max(x,y) (((x)<(y))?(x):(y))

#define DEFAULT_MAX_FD_NUM  1000000

// 安全DELETE
#define safe_delete(ptr) do {               \
    if (ptr != NULL) delete ptr;            \
    ptr = NULL;                             \
} while(0)

// 安全DELETE数组
#define safe_delete_arr(ptr) do {           \
    if (ptr != NULL) delete [] ptr;         \
    ptr = NULL;                             \
} while(0)

// 安全的Free
#define safe_free(ptr) do {                 \
    if (ptr != NULL) free(ptr);         \
    ptr = NULL;                             \
} while(0)

// 对应的错误信息
#define MT_ERROR      -1
#define MT_UNKOWN     -2
#define MT_BUSY       -3
#define MT_DONE       -4
#define MT_DECLINED   -5
#define MT_ABORT      -6

#define MT_NONE 		0
#define MT_READABLE 	1 // EPOLLIN
#define	MT_WRITABLE 	2 // EPOLLOUT
#define MT_EVERR		4 // ERR, HUP

// 判断是否使用协程
#define MT_HOOK_MAX_FD      65535 * 2
#define MT_FD_FLG_NOUSE     0x0
#define MT_FD_FLG_INUSE     0x1
#define MT_FD_FLG_UNBLOCK   0x2

// 回调函数
typedef void (*ThreadRunFunction)(void*);
typedef unsigned int (*TcpCheckMsgLenFunction)(void* buf, int len);

// 定义打印日志数据
#define TRACE 1

#if TRACE
#define LOG_TRACE(fmt, args...) fprintf(stdout, (char*)"[TRACE][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_DEBUG(fmt, args...) fprintf(stdout, (char*)"[DEBUG][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_WARN(fmt, args...) fprintf(stdout, (char*)"[WARN][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_ERROR(fmt, args...) fprintf(stdout, (char*)"[ERROR][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_CHECK_FUNCTION	fprintf(stdout, (char*)"[TRACE][%-10s][%-4d][%-10s] check function ...\n", __FILE__, __LINE__, __FUNCTION__);
#elif DEBUG
#define LOG_TRACE(fmt, args...)
#define LOG_DEBUG(fmt, args...) fprintf(stdout, (char*)"[DEBUG][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_WARN(fmt, args...) fprintf(stdout, (char*)"[WARN][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_ERROR(fmt, args...) fprintf(stdout, (char*)"[ERROR][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_CHECK_FUNCTION
#else
#define LOG_TRACE(fmt, args...)
#define LOG_DEBUG(fmt, args...)
#define LOG_WARN(fmt, args...) fprintf(st`dout, (char*)"[WARN][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_ERROR(fmt, args...) fprintf(stdout, (char*)"[ERROR][%-10s][%-4d][%-10s]"fmt"\n", __FILE__, __LINE__, __FUNCTION__, ##args);
#define LOG_CHECK_FUNCTION
#endif

#endif