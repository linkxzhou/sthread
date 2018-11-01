/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_ENUM_H_INCLUDED_
#define _MT_ENUM_H_INCLUDED_

#include "mt_public.h"

MTHREAD_NAMESPACE_BEGIN

enum eBuffType
{
    eBUFF_UNDEF  =  0x0,
    eBUFF_RECV   =  0x1, // 收包
    eBUFF_SEND   =  0x2, // 发包
};

// events事件
enum eEventType
{
    eEVENT_UNDEF     = 0x0,
    eEVENT_THREAD    = 0x1,
    eEVENT_KEEPALIVE = 0x2,
    eEVENT_SESSION   = 0x3,
    eEVENT_TIMEOUT   = 0x4,
    eEVENT_USER1     = 0x5,
    eEVENT_USER2     = 0x6,
    eEVENT_USER3     = 0x7,
};

enum eThreadType
{
    eNORMAL          = 0x1,
    ePRIMORDIAL      = 0x2,
    eDAEMON          = 0x3,
    eSUB_THREAD      = 0x4,
};

enum eThreadFlag
{
    eNOT_INLIST  = 0x0,
    eFREE_LIST   = 0x1,
    eIO_LIST     = 0x2,
    eSLEEP_LIST  = 0x4,
    eRUN_LIST    = 0x8,
    ePEND_LIST   = 0x10,
    eSUB_LIST    = 0x20,
};

enum eThreadState
{
    eINITIAL    = 0x0,
    eRUNABLE    = 0x1,
    eRUNNING    = 0x2,
    eSLEEPING   = 0x3,
    ePENDING    = 0x4,
    eIOWAIT     = 0x5,
};

enum eActionState
{
    eACTION_FLAG_UNDEF   = 0x0,
    eACTION_FLAG_INIT    = 0x1,
    eACTION_FLAG_OPEN    = 0x2,
    eACTION_FLAG_SEND    = 0x4,
    eACTION_FLAG_FIN     = 0x8,
};

enum eActionError
{
    eERR_NONE            =  0,
    eERR_SOCKET_FAIL     = -1, // 创建socket失败
    eERR_CONNECT_FAIL    = -2, // 连接失败
    eERR_SEND_FAIL       = -3, // 发送失败
    eERR_RECV_FAIL       = -4, // 接收失败
    eERR_SEND_TIMEOUT    = -5, // 发送数据超时
    eERR_RECV_TIMEOUT    = -6, // 超时
    eERR_POLL_FAIL       = -7, // event异常
    eERR_FRAME_ERROR     = -8, // 框架失败
    eERR_PEER_CLOSE      = -9, // 对端关闭
    eERR_PARAM_ERROR     = -10, // 参数错误
    eERR_MEMORY_ERROR    = -11, // 内存异常
    eERR_ENCODE_ERROR    = -12, // encode流程错误
    eERR_DST_ADDR_ERROR  = -13, // 目标ip异常
};

enum eSessionFlag
{
    eSESSION_IDLE    = 0x0, // session是idle类型
    eSESSION_INUSE   = 0x1, // session正在使用
    eSESSION_SET     = 0x2, // session已经设置
    eSESSION_UNSET   = 0x3, // session未设置
};

enum eConnType
{
    eUNDEF_CONN     = 0x0, // 连接错误
    eUDP_CONN       = 0x1, // udp client连接
    eTCP_LONG_CONN  = 0x2, // tcp client长连接
    eTCP_SHORT_CONN = 0x3, // tcp client短连接
    eTCP_SERVER     = 0x4,
    eUDP_SERVER     = 0x5,
    eTCP_ACCEPT_CONN    = 0x6,
};

enum eKeepFlag
{
    eKEEP_IN_LIST  = 0x1,
    eKEEP_IN_POLL  = 0x2,
};

MTHREAD_NAMESPACE_END

#endif