/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_c.h"
#include "st_connection.h"
#include "st_manager.h"

using namespace stlib;

// 获取连接的句柄信息
static StExecClientConnection *_get_conn(struct sockaddr_in *dst, int32_t &sock,
                                         eConnType type) {
  StNetAddress addr(*dst);
  StExecClientConnection *conn =
      Instance<StConnectionManager<StExecClientConnection>>()->AllocPtr(type,
                                                                        &addr);
  if (NULL == conn) {
    LOG_ERROR("get connection failed, dst[%p]", dst);
    return NULL;
  }

  int32_t osfd = conn->CreateSocket(addr);
  LOG_TRACE("osfd: %d", osfd);
  if (osfd < 0) {
    LOG_ERROR("create socket failed, ret[%d]", osfd);
    // Instance<
    //     StConnectionManager<StExecClientConnection>
    // >()->FreePtr(conn);
    return NULL;
  }

  sock = osfd;

  return conn;
}

// 获取tcp的接收信息
static int32_t _tcp_check_recv(int32_t sock, char *recvbuf, int32_t &len,
                               int32_t flags, int32_t timeout,
                               CheckLengthCallback callback) {
  int32_t recvlen = 0;
  time64_t start_ms = Util::SysMs();

  do {
    time64_t cost_time = Util::SysMs() - start_ms;
    LOG_TRACE("cost_time: %ld", cost_time);

    if (cost_time > timeout) {
      errno = ETIME;
      LOG_ERROR("tcp socket[%d] recv not ok, timeout", sock);
      return -3;
    }

    int32_t rc = ::_recv(sock, (recvbuf + recvlen), (len - recvlen), 0,
                         (timeout - cost_time));
    LOG_TRACE("sock: %d, rc: %d, recvlen: %d", sock, rc, recvlen);
    if (rc < 0) {
      LOG_ERROR("tcp socket[%d] recv failed ret[%d][%m]", sock, rc);
      return -4;
    } else if (rc == 0) {
      LOG_ERROR("tcp socket[%d] remote close", sock);
      return -5;
    }
    recvlen += rc;

    rc = callback(recvbuf, recvlen);
    if (rc < 0) {
      LOG_ERROR("tcp socket[%d] user check pkg error[%d]", sock, rc);
      return -6;
    } else if (rc == 0) {
      if (len == recvlen) {
        LOG_ERROR("tcp socket[%d] user check pkg not ok, but no more buff",
                  sock);
        return -7;
      }

      continue;
    } else {
      if (rc > recvlen) {
        continue;
      } else {
        len = rc;
        break;
      }
    }
  } while (true);

  return 0;
}

int32_t udp_sendrecv(struct sockaddr_in *dst, void *pkg, int32_t len,
                     void *recvbuf, int32_t &bufsize, int32_t timeout) {
  if (!dst || !pkg || !recvbuf || bufsize <= 0) {
    LOG_ERROR(
        "input params invalid, dst[%p], pkg[%p], recvbuf[%p], bufsize[%d]", dst,
        pkg, recvbuf, bufsize);
    return -1;
  }

  int32_t ret = 0, rc = 0;
  struct sockaddr_in from_addr;
  int32_t addr_len = sizeof(from_addr);

  // 获取时间戳
  time64_t start_ms = Util::SysMs(), cost_time = 0;
  int32_t time_left = 0;
  int32_t sock = -1;

  StExecClientConnection *conn = _get_conn(dst, sock, eUDP_CONN);
  LOG_TRACE("socket: %d, conn: %p", sock, conn);
  if ((conn == NULL) || (sock < 0)) {
    LOG_ERROR("socket[%d] get conn failed, ret", sock);
    ret = -2;
    goto UDP_SENDRECV_EXIT_LABEL;
  }

  // 发送数据
  rc = ::_sendto(sock, pkg, len, 0, (struct sockaddr *)dst,
                 (int32_t)sizeof(*dst), timeout);
  if (rc < 0) {
    LOG_ERROR("udp_sendrecv send failed, rc: %d, errno: %d", rc, errno);
    ret = -3;
    goto UDP_SENDRECV_EXIT_LABEL;
  }

  cost_time = Util::SysMs() - start_ms;
  time_left = (timeout > cost_time) ? (timeout - (int32_t)cost_time) : 0;
  // 接收数据
  rc = ::_recvfrom(sock, recvbuf, bufsize, 0, (struct sockaddr *)&from_addr,
                   (socklen_t *)&addr_len, time_left);
  LOG_TRACE("from_addr %s: %d, time_left: %d", inet_ntoa(from_addr.sin_addr),
            ntohs(from_addr.sin_port), time_left);
  if (rc < 0) {
    LOG_ERROR("udp_sendrecv recv failed, rc: %d, errno: %d", rc, errno);
    ret = -4;
    goto UDP_SENDRECV_EXIT_LABEL;
  }

  bufsize = rc;

UDP_SENDRECV_EXIT_LABEL:
  if (sock > 0) {
    st_close(sock);
    sock = -1;
  }

  // 释放连接
  if (conn != NULL) {
    // Instance<
    //     StConnectionManager<StClientConnection>
    // >()->FreePtr(conn);
  }

  return ret;
}

// tcp的发送和接收信息(长连接)
int32_t tcp_sendrecv(struct sockaddr_in *dst, void *pkg, int32_t len,
                     void *recvbuf, int32_t &bufsize, int32_t timeout,
                     CheckLengthCallback callback, bool keeplive) {
  if (!dst || !pkg || !recvbuf || !callback || bufsize <= 0) {
    LOG_ERROR("input params invalid, dst[%p], pkg[%p], recvbuf[%p], "
              "callback[%p], bufsize[%d]",
              dst, pkg, recvbuf, callback, bufsize);
    return -10;
  }

  int32_t ret = 0, rc = 0;
  int32_t addr_len = sizeof(struct sockaddr_in);
  // 获取时间戳
  time64_t start_ms = Util::SysMs(), cost_time = 0;
  // 连接超时时间
  int32_t time_left = timeout;
  int32_t sock = -1;

  // 判断是否为长连接
  StConnection *conn =
      _get_conn(dst, sock, keeplive ? eTCP_KEEPLIVE_CONN : eTCP_CONN);
  LOG_TRACE("socket: %d, conn: %p", sock, conn);
  if ((conn == NULL) || (sock < 0)) {
    LOG_ERROR("socket[%d] get conn failed, ret", sock);
    ret = -1;
    goto TCP_SENDRECV_EXIT_LABEL;
  }

  cost_time = Util::SysMs() - start_ms;
  time_left = (timeout > cost_time) ? (timeout - cost_time) : 0;
  // 先将数据包发送
  rc = ::_send(sock, pkg, len, 0, time_left);
  if (rc < 0) {
    LOG_ERROR("socket[%d] send failed, ret[%d]", sock, rc);
    ret = -2;
    goto TCP_SENDRECV_EXIT_LABEL;
  }

  cost_time = Util::SysMs() - start_ms;
  time_left = (timeout > cost_time) ? (timeout - cost_time) : 0;
  rc = _tcp_check_recv(sock, (char *)recvbuf, bufsize, 0, time_left, callback);
  if (rc < 0) {
    LOG_ERROR("socket[%d] recv failed, ret[%d]", sock, rc);
    ret = rc;
    goto TCP_SENDRECV_EXIT_LABEL;
  }

TCP_SENDRECV_EXIT_LABEL:
  // 短连接close
  if (!keeplive) {
    conn->CloseSocket();
    // 释放链接
  }

  return ret;
}

// 设置私有数据
void st_set_private(void *data) {
  Thread *athread = (Thread *)(GlobalThreadScheduler()->GetActiveThread());
  ASSERT(athread == NULL);
  Thread *thread = (Thread *)(athread->GetRootThread());

  if (thread != NULL) {
    thread->SetPrivate(data);
  }
}

// 获取私有数据
void *st_get_private() {
  Thread *athread = (Thread *)(GlobalThreadScheduler()->GetActiveThread());
  ASSERT(athread == NULL);
  Thread *thread = (Thread *)(athread->GetRootThread());

  if (thread != NULL) {
    return thread->GetPrivate();
  }

  return NULL;
}

void st_set_hook_flag() { Instance<Manager>()->SetHookFlag(); }