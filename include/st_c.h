/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_C_H_INCLUDED_
#define _ST_C_H_INCLUDED_

#include <netinet/in.h>
#include <vector>
#include "st_util.h"
#include "st_thread.h"
#include "st_manager.h"

typedef uint32_t (*CheckLengthCallback)(void *buf, int len);

#ifdef  __cplusplus
extern "C" {
#endif

int udp_sendrecv(struct sockaddr_in *dst, 
    void *pkg, int len, 
    void *recvbuf, int &bufsize, 
    int timeout);

// int tcp_sendrecv(struct sockaddr_in *dst, 
//     void *pkg, int len, 
//     void *recv_buf, int &buf_size, 
//     int timeout, 
//     CheckLengthCallback callback, bool keeplive = false);

void st_set_private(void *data); // 设置私有数据

void* st_get_private();

void st_set_hook_flag();

#ifdef  __cplusplus
}
#endif

#endif
