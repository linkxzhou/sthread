/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_C_H__
#define _ST_C_H__

#include <netinet/in.h>
#include <vector>
#include "st_util.h"
#include "st_thread.h"
#include "st_manager.h"
#include "st_connection.h"

ST_NAMESPACE_BEGIN

class StExecClientConnection : public StClientConnection<StEventSuper>
{ };

ST_NAMESPACE_END

#ifdef  __cplusplus
extern "C" {
#endif

typedef int32_t (*CheckLengthCallback)(void *buf, int len);

int udp_sendrecv(struct sockaddr_in *dst, 
    void *pkg, int len, 
    void *recvbuf, int &bufsize, 
    int timeout);

int tcp_sendrecv(struct sockaddr_in *dst, 
    void *pkg, int len, 
    void *recvbuf, int &bufsize, 
    int timeout, 
    CheckLengthCallback callback, bool keeplive = false);

void st_set_private(void *data); // 设置私有数据

void* st_get_private();

void st_set_hook_flag();

#ifdef  __cplusplus
}
#endif

#endif
