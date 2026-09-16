/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_CONNECTION_H__
#define _ST_CONNECTION_H__

#include "app/st_sys.h"
#include "st_poll.h"
#include "st_public.h"
#include "st_thread.h"
#include "stlib/st_buffer.h"
#include "stlib/st_heap_timer.h"
#include "stlib/st_util.h"

using namespace stlib;
using namespace sthread;

/* 用途：连接基类；SendData/RecvData 对业务是同步语义，内部可能 Yield。
 * 线程模型：绑定当前 OS 线程的事件/协程调度器。
 * 所有权：收发 StBuffer 从池借用，Reset() 归还；派生类勿泄漏 buffer。 */
class StConnection : public referenceable {
public:
  StConnection()
      : m_type_(eUNDEF_CONN), m_osfd_(-1), m_timeout_(30000), m_sendbuf_(NULL),
        m_recvbuf_(NULL), m_item_(NULL) {
    m_recvbuf_ = Instance<StBufferPool>()->GetBuffer(ST_RECV_BUFFSIZE);
    m_sendbuf_ = Instance<StBufferPool>()->GetBuffer(ST_SEND_BUFFSIZE);
  }

  virtual ~StConnection() {
    this->Close();
    this->Reset();
  }

  virtual int32_t Create(const StNetAddr &addr) { return -1; }

  void Close() {
    if (m_osfd_ > 0) {
      sys_close(m_osfd_);
      m_osfd_ = -1;
    }
  }

  inline void SetAddr(const StNetAddr &addr) { m_addr_ = addr; }

  inline StNetAddr &GetAddr() { return m_addr_; }

  inline void SetDestAddr(const StNetAddr &destaddr) { m_destaddr_ = destaddr; }

  inline StNetAddr &GetDestAddr() { return m_destaddr_; }

  inline void SetOsfd(int fd) { m_osfd_ = fd; }

  inline int GetOsfd() { return m_osfd_; }

  inline eConnType GetConnType() { return m_type_; }

  inline void SetConnType(eConnType type) { m_type_ = type; }

  // 设置超时时间
  inline void SetTimeout(int32_t timeout) { m_timeout_ = timeout; }

  inline int32_t GetTimeout() { return m_timeout_; }

  virtual void Reset() {
    Instance<StBufferPool>()->FreeBuffer(m_sendbuf_);
    Instance<StBufferPool>()->FreeBuffer(m_recvbuf_);

    m_osfd_ = -1;
    m_sendbuf_ = NULL;
    m_recvbuf_ = NULL;
    m_type_ = eUNDEF_CONN;
    m_timeout_ = 30000;
  }

  // 判断是否支持 keepalive（与 eConnType 末位 0x1 规则一致）
  inline bool Keeplive() { return IS_KEEPLIVE(m_type_); }

  inline StBuffer *GetSendBuffer() { return m_sendbuf_; }

  inline StBuffer *GetRecvBuffer() { return m_recvbuf_; }

  int32_t SendData();

  int32_t RecvData();

  // 处理操作
  virtual int32_t DoOutput(void *buf, int32_t &len) { return 0; }

  virtual int32_t DoInput(void *buf, int32_t len) { return 0; }

  virtual int32_t DoProcess() { return 0; }

  virtual int32_t DoError(int32_t err) { return 0; }

protected:
  int m_osfd_;
  StBuffer *m_sendbuf_, *m_recvbuf_;
  StNetAddr m_addr_, m_destaddr_;
  eConnType m_type_;
  int32_t m_timeout_;
  StEventItem *m_item_;
};

template <class ConnectionT>
/* 用途：客户端连接；Create 建 socket、注册事件，TCP 时 connect。
 * 线程模型：同 StConnection；兼 StTimer 可入定时器堆。
 * 所有权：通常经 StConnectionManager 分配/复用（仅 keepalive 类型走 hash）。 */
class StClientConnection : public StConnection, public StTimer {
public:
  StClientConnection() : StConnection() {}

  virtual int32_t Create(const StNetAddr &addr) {
    m_destaddr_ = addr;

    int protocol = SOCK_STREAM;
    if (IS_UDP_CONN(m_type_)) {
      protocol = SOCK_DGRAM;
    }

    m_osfd_ = sys_socket(addr.IsIPV6() ? AF_INET6 : AF_INET, protocol, 0);
    LOG_TRACE("m_osfd_: %d", m_osfd_);
    if (m_osfd_ < 0) {
      LOG_ERROR("create socket failed, ret[%d]", m_osfd_);
      return -1;
    }

    m_item_ = Instance<UtilPtrPool<ConnectionT> >()->AllocPtr();
    LOG_ASSERT(m_item_ != NULL);

    m_item_->SetOsfd(m_osfd_);
    m_item_->EnableOutput();
    m_item_->DisableInput();
    GlobalEventSchedule()->Add(m_item_); // TODO:

    if (IS_TCP_CONN(m_type_)) {
      int32_t rc = Connect(addr);
      if (rc < 0) {
        LOG_ERROR("connect error, rc: %d", rc);
        GlobalEventSchedule()->ClearItem(m_item_); // TODO:
        UtilPtrPoolFree(m_item_);
        Close();
        return -2;
      }
    }

    return m_osfd_;
  }

  virtual int32_t Connect(const StNetAddr &addr) {
    struct sockaddr *destaddr = addr.GetSockAddr();

    int32_t err = 0;
    int32_t ret =
        st_connect(m_osfd_, destaddr, sizeof(struct sockaddr_in), m_timeout_);
    if (ret < 0) {
      err = errno;
      if (err == EISCONN) {
        return 0;
      } else {
        if ((err == EINPROGRESS) || (err == EALREADY) || (err == EINTR)) {
          LOG_ERROR("connect not ok, sock: %d, errno: %d, strerr: %s", m_osfd_,
                    err, strerror(err));
          return -1;
        } else {
          LOG_ERROR("connect not ok, sock: %d, errno: %d, strerr: %s", m_osfd_,
                    err, strerror(err));
          return -2;
        }
      }
    }

    return 0;
  }
};

/* 用途：连接池；对 IS_KEEPLIVE 类型用 StHashList 按地址复用。
 * 线程模型：线程局部 Instance 使用。
 * 所有权：AllocPtr/FreePtr 配对；IS_KEEPLIVE 类型按地址走 hash 复用。 */
template <class ConnectionT> class StConnectionManager {
public:
  typedef ConnectionT *ConnectionTPtr;

  ConnectionTPtr AllocPtr(eConnType type, const StNetAddr *destaddr = NULL,
                          const StNetAddr *srcaddr = NULL) {
    StNetAddrKey probe;
    if (IS_KEEPLIVE(type) && destaddr != NULL) {
      probe.SetDestAddr(*destaddr);
    }
    if (IS_KEEPLIVE(type) && srcaddr != NULL) {
      probe.SetSrcAddr(*srcaddr);
    }

    ConnectionTPtr conn = NULL;
    if (IS_KEEPLIVE(type)) {
      conn = (ConnectionTPtr)(m_hashlist_.HashFindData(&probe));
    }

    if (conn == NULL) {
      conn = Instance<UtilPtrPool<ConnectionT> >()->AllocPtr();
      conn->SetConnType(type);
      if (destaddr != NULL) {
        conn->SetDestAddr(*destaddr);
      }
      if (srcaddr != NULL) {
        conn->SetAddr(*srcaddr);
      }
      if (IS_KEEPLIVE(type)) {
        /* HashInsert stores the key pointer; must be heap-owned. */
        StNetAddrKey *key = new StNetAddrKey();
        if (destaddr != NULL) {
          key->SetDestAddr(*destaddr);
        }
        if (srcaddr != NULL) {
          key->SetSrcAddr(*srcaddr);
        }
        key->SetDataPtr((void *)conn);
        int32_t r = m_hashlist_.HashInsert(key);
        LOG_ASSERT(r >= 0);
      }
    }

    LOG_ASSERT(conn != NULL);
    return conn;
  }

  void FreePtr(ConnectionTPtr conn) {
    if (conn == NULL) {
      return;
    }
    eConnType type = conn->GetConnType();
    if (IS_KEEPLIVE(type)) {
      StNetAddrKey probe;
      probe.SetDestAddr(conn->GetDestAddr());
      probe.SetSrcAddr(conn->GetAddr());
      StNetAddrKey *dead = m_hashlist_.HashRemove(&probe);
      st_safe_delete(dead);
    }
    UtilPtrPoolFree(conn);
  }

private:
  StHashList<StNetAddrKey> m_hashlist_;
};

#endif