/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_C_H__
#define _ST_C_H__

#include "src/st_connection.h"
#include "src/st_manager.h"
#include "src/st_thread.h"
#include "stlib/st_util.h"
#include <netinet/in.h>
#include <vector>

using namespace stlib;

class StExecClientConnection : public StClientConnection<StEventItem> {};

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*CheckLengthCallback)(void *buf, int len);

/* udp_sendrecv / tcp_sendrecv 的状态码是这一层自己的，不是 st_* 那套。
 * st_* 超时是 -1 且 errno=ETIME，-3 只表示 Schedule 失败（见 src/st_sys.h）。
 * 这里超时仍通过 errno=ETIME 辨认，但返回值按下表，避免和「参数/建连失败」撞车。
 *
 * udp_sendrecv:
 *   0  成功，bufsize 为收到的字节数
 *  -1  参数非法
 *  -2  拿不到连接
 *  -3  发送失败（含超时，errno=ETIME）
 *  -4  接收失败（含超时，errno=ETIME）
 *
 * tcp_sendrecv:
 *   0  成功，bufsize 为回调认可的包长
 *  -1  拿不到连接
 *  -2  发送失败（含超时，errno=ETIME）
 *  -3  接收超时（errno=ETIME）
 *  -4  接收失败（非超时）
 *  -5  对端关闭
 *  -6  回调认为包错误
 *  -7  缓冲已满但包还不完整
 * -10  参数非法
 */
int udp_sendrecv(struct sockaddr_in *dst, void *pkg, int len, void *recvbuf,
                 int &bufsize, int timeout);

int tcp_sendrecv(struct sockaddr_in *dst, void *pkg, int len, void *recvbuf,
                 int &bufsize, int timeout, CheckLengthCallback callback,
                 bool keeplive = false);

void st_set_private(void *data); // 设置私有数据

void *st_get_private();

void st_set_hook_flag();

bool st_init_frame();

/* Historical aliases used by sample apps */
#define mt_init_frame st_init_frame
#define mt_set_hook_flag st_set_hook_flag

#ifdef __cplusplus
}
#endif

#endif
