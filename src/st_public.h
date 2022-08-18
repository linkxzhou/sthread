/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_PUBLIC_H_
#define _ST_PUBLIC_H_

#include "stlib/st_test.h"
#include <errno.h>
#include <map>
#include <math.h>
#include <netinet/in.h>
#include <queue>
#include <set>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <vector>

typedef enum {
  eNORMAL = 0x1,     // 通用的
  ePRIMORDIAL = 0x2, // 主thread
  eDAEMON = 0x3,     // 调度thread
  eSUB_THREAD = 0x4, // 子thread
} eThreadType;

typedef enum {
  eNOT_INLIST = 0x0,
  eFREE_LIST = 0x1,  // 空闲线程
  eIO_LIST = 0x2,    // IO线程
  eSLEEP_LIST = 0x4, // sleep线程
  eRUN_LIST = 0x8,   // 运行线程
  ePEND_LIST = 0x10, // 阻塞线程
  eSUB_LIST = 0x20,  // 子线程
} eThreadFlag;

typedef enum {
  eINITIAL = 0x0,  // 初始化
  eRUNABLE = 0x1,  // 可运行
  eRUNNING = 0x2,  // 正在运行
  eSLEEPING = 0x3, // 正在休眠
  ePENDING = 0x4,  // 阻塞中
  eIOWAIT = 0x5,   // IO等待
} eThreadState;

#define IS_UDP_CONN(type)                                                      \
  (((type) == eUDP_CONN) || ((type) == eUDP_UDPSESSION_CONN))
#define IS_TCP_CONN(type)                                                      \
  (((type) == eTCP_CONN) || ((type) == eTCP_KEEPLIVE_CONN))
#define KEEPLIVE_VALUE 0x1
#define IS_KEEPLIVE(type) (((type)&KEEPLIVE_VALUE) == KEEPLIVE_VALUE)

// 规则是最后一位0x1则表示需要保存状态，否则不需要
typedef enum {
  eUNDEF_CONN = 0x0,                            // 连接错误
  eUDP_CONN = 0x10,                             // udp 连接
  eTCP_CONN = 0x20,                             // tcp 短连接
  eTCP_KEEPLIVE_CONN = (0x10 & KEEPLIVE_VALUE), // keeplive
  eUDP_UDPSESSION_CONN = (0x20 & KEEPLIVE_VALUE),
} eConnType;

// 对应的错误
#define eERR_NONE 0
#define eERR_SOCKET_FAIL -1      // 创建socket失败
#define eERR_CONNECT_FAIL -2     // 连接失败
#define eERR_SEND_FAIL -3        // 发送失败
#define eERR_RECV_FAIL -4        // 接收失败
#define eERR_SEND_TIMEOUT -5     // 发送数据超时
#define eERR_RECV_TIMEOUT -6     // 超时
#define eERR_POLL_FAIL -7        // event异常
#define eERR_FRAME_ERROR -8      // 框架失败
#define eERR_PEER_CLOSE -9       // 对端关闭
#define eERR_PARAM_ERROR -10     // 参数错误
#define eERR_MEMORY_ERROR -11    // 内存异常
#define eERR_ENCODE_ERROR -12    // encode流程错误
#define eERR_DEST_ADDR_ERROR -13 // 目标IP异常

#endif