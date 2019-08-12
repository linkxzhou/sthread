/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_PUBLIC_H_INCLUDED_
#define _ST_PUBLIC_H_INCLUDED_

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <math.h>
#include <queue>
#include <set>
#include <map>
#include <vector>
#include <errno.h>
#include <unistd.h>

#define ST_NAMESPACE_BEGIN  namespace sthread {
#define ST_NAMESPACE_END    }
#define ST_NAMESPACE_USING  using namespace sthread;

#if defined(__APPLE__)
#if __GNUC__ == 2 && __GNUC_MINOR__ < 96 
#define __builtin_expect(x, expected_value) (x) 
#endif
#define likely(x)   __builtin_expect((x),1) 
#define unlikely(x) __builtin_expect((x),0)
#endif

#define st_safe_delete(ptr)                 \
    do                                      \
    {                                       \
        if (ptr != NULL) delete ptr;        \
        ptr = NULL;                         \
    } while(0)

#define st_safe_delete_array(ptr)           \
    do                                      \
    {                                       \
        if (ptr != NULL) delete [] ptr;     \
        ptr = NULL;                         \
    } while(0)

#define st_safe_free(ptr)                   \
    do                                      \
    {                                       \
        if (ptr != NULL) free(ptr);         \
        ptr = NULL;                         \
    } while(0)

// 对应的错误信息
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

#define ST_MAXINT        0x7fffffff
#define ST_MAXTIME       2177280000

#define ST_ALGIN(size)      ((size) + (8-(size)%8)) // 边界对齐

#define ST_NELEMS(a)        ((sizeof(a)) / sizeof((a)[0]))

#define ST_MIN(a, b)           ((a) < (b) ? (a) : (b))
#define ST_MAX(a, b)           ((a) > (b) ? (a) : (b))

#define ST_SQUARE(d)           ((d) * (d))
#define ST_VAR(s, s2, n)       (((n) < 2) ? 0.0 : ((s2) - ST_SQUARE(s)/(n)) / ((n) - 1))
#define ST_STDDEV(s, s2, n)    (((n) < 2) ? 0.0 : sqrt(ST_VAR((s), (s2), (n))))

// 回调函数
// typedef void (*ThreadRunCallback)(void*);
// typedef unsigned int (*TcpCheckMsgLenCallback)(void* buf, int len);
// typedef void (*FrameCallback)(void*);

typedef enum
{
    eBUFF_UNDEF  =  0x0,
    eBUFF_RECV   =  0x1, // 收包
    eBUFF_SEND   =  0x2, // 发包
} eBuffType;

// events事件
typedef enum
{
    eEVENT_UNDEF     = 0x0,
    eEVENT_THREAD    = 0x1,
    eEVENT_KEEPALIVE = 0x2,
    eEVENT_SESSION   = 0x3,
    eEVENT_TIMEOUT   = 0x4,
    eEVENT_USER1     = 0x5,
    eEVENT_USER2     = 0x6,
    eEVENT_USER3     = 0x7,
    eEVENT_USER4     = 0x8,
} eEventType;

typedef enum
{
    eNORMAL          = 0x1,
    ePRIMORDIAL      = 0x2,
    eDAEMON          = 0x3,
    eSUB_THREAD      = 0x4,
} eThreadType;

typedef enum
{
    eNOT_INLIST  = 0x0,
    eFREE_LIST   = 0x1,
    eIO_LIST     = 0x2,
    eSLEEP_LIST  = 0x4,
    eRUN_LIST    = 0x8,
    ePEND_LIST   = 0x10,
    eSUB_LIST    = 0x20,
} eThreadFlag;

typedef enum
{
    eINITIAL    = 0x0,
    eRUNABLE    = 0x1,
    eRUNNING    = 0x2,
    eSLEEPING   = 0x3,
    ePENDING    = 0x4,
    eIOWAIT     = 0x5,
} eThreadState;

typedef enum
{
    eUNDEF_CONN     = 0x0, // 连接错误
    eUDP_CONN       = 0x1,
    eTCP_LONG_CONN  = 0x2,
    eTCP_SHORT_CONN = 0x3,
} eConnType;

// enum eActionState
// {
//     eACTION_FLAG_UNDEF   = 0x0,
//     eACTION_FLAG_INIT    = 0x1,
//     eACTION_FLAG_OPEN    = 0x2,
//     eACTION_FLAG_SEND    = 0x4,
//     eACTION_FLAG_FIN     = 0x8,
// };

// enum eActionError
// {
//     eERR_NONE            =  0,
//     eERR_SOCKET_FAIL     = -1, // 创建socket失败
//     eERR_CONNECT_FAIL    = -2, // 连接失败
//     eERR_SEND_FAIL       = -3, // 发送失败
//     eERR_RECV_FAIL       = -4, // 接收失败
//     eERR_SEND_TIMEOUT    = -5, // 发送数据超时
//     eERR_RECV_TIMEOUT    = -6, // 超时
//     eERR_POLL_FAIL       = -7, // event异常
//     eERR_FRAME_ERROR     = -8, // 框架失败
//     eERR_PEER_CLOSE      = -9, // 对端关闭
//     eERR_PARAM_ERROR     = -10, // 参数错误
//     eERR_MEMORY_ERROR    = -11, // 内存异常
//     eERR_ENCODE_ERROR    = -12, // encode流程错误
//     eERR_DST_ADDR_ERROR  = -13, // 目标ip异常
// };

// enum eSessionFlag
// {
//     eSESSION_IDLE    = 0x0, // session是idle类型
//     eSESSION_INUSE   = 0x1, // session正在使用
//     eSESSION_SET     = 0x2, // session已经设置
//     eSESSION_UNSET   = 0x3, // session未设置
// };

// enum eKeepFlag
// {
//     eKEEP_IN_LIST  = 0x1,
//     eKEEP_IN_POLL  = 0x2,
// };

#endif