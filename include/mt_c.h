/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_C_H_INCLUDED_
#define _MT_C_H_INCLUDED_

#include <netinet/in.h>
#include <vector>
#include "mt_utils.h"
#include "mt_connection.h"
#include "mt_thread.h"
#include "mt_ext.h"

#ifdef  __cplusplus
extern "C" {
#endif

int udp_sendrecv(struct sockaddr_in* dst, void* pkg, int len, 
        void* recv_buf, int& buf_size, int timeout);

int tcp_sendrecv(struct sockaddr_in* dst, void* pkg, int len, 
        void* recv_buf, int& buf_size, int timeout, 
        TcpCheckMsgLenCallback check_func, bool is_keep = false);

void mt_sleep(int ms);

unsigned long long mt_time_ms(void); // 获取时间

void mt_set_private(void *data); // 设置私有数据

void* mt_get_private();

bool mt_init_frame();

void mt_set_stack_size(unsigned int bytes);

void mt_set_hook_flag();

void mt_set_timeout(int timeout_ms);

#ifdef  __cplusplus
}
#endif

#endif
