/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_CONNECTION_H__
#define _ST_CONNECTION_H__

#include "stlib/st_buffer.h"
#include "stlib/st_heap_timer.h"
#include "stlib/st_util.h"

using namespace stlib;

class StConnection : public referenceable {
public:
  StConnection()
      : m_type_(eUNDEF_CONN), m_osfd_(-1), m_timeout_(30000), m_sendbuf_(NULL),
        m_recvbuf_(NULL), m_item_(NULL) {
    m_recvbuf_ = Instance<StBufferPool<>>()->GetBuffer(ST_RECV_BUFFSIZE);
    m_sendbuf_ = Instance<StBufferPool<>>()->GetBuffer(ST_SEND_BUFFSIZE);
  }

  virtual ~StConnection() {
    this->Close();
    this->Reset();
  }

  virtual int32_t Create(const StNetAddr &addr) { return -1; }

  void Close() {
    if (m_osfd_ > 0) {
      __close(m_osfd_);
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
    Instance<StBufferPool<>>()->FreeBuffer(m_sendbuf_);
    Instance<StBufferPool<>>()->FreeBuffer(m_recvbuf_);

    m_osfd_ = -1;
    m_sendbuf_ = NULL;
    m_recvbuf_ = NULL;
    m_type_ = eUNDEF_CONN;
    m_timeout_ = 30000;
  }

  // 判断是否支持keeplive
  inline bool Keeplive() { return false; }

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
  StEventSuper *m_item_;
};

template <class ConnectionT>
class StClientConnection : public StConnection, public StTimer {
public:
  StClientConnection() : StConnection() {}

  virtual int32_t Create(const StNetAddr &addr) {
    m_destaddr_ = addr;

    int protocol = SOCK_STREAM;
    if (IS_UDP_CONN(m_type_)) {
      protocol = SOCK_DGRAM;
    }

    m_osfd_ = __socket(addr.IsIPV6() ? AF_INET6 : AF_INET, protocol, 0);
    LOG_TRACE("m_osfd_: %d", m_osfd_);
    if (m_osfd_ < 0) {
      LOG_ERROR("create socket failed, ret[%d]", m_osfd_);
      return -1;
    }

    m_item_ = Instance<UtilPtrPool<ConnectionT>>()->AllocPtr();
    ASSERT(m_item_ != NULL);

    m_item_->SetOsfd(m_osfd_);
    m_item_->EnableOutput();
    m_item_->DisableInput();
    GlobalEventScheduler()->Add(m_item_); // TODO:

    if (IS_TCP_CONN(m_type_)) {
      int32_t rc = Connect(addr);
      if (rc < 0) {
        LOG_ERROR("connect error, rc: %d", rc);
        GlobalEventScheduler()->Close(m_item_); // TODO:
        UtilPtrPoolFree(m_item_);
        Close();
        return -2;
      }
    }

    return m_osfd_;
  }

  virtual int32_t Connect(const StNetAddr &addr) {
    struct sockaddr *destaddr;
    addr.GetSockAddr(destaddr);

    int32_t err = 0;
    int32_t ret =
        __connect(m_osfd_, destaddr, sizeof(struct sockaddr_in), m_timeout_);
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

template <class ConnectionT> class StConnectionManager {
public:
  typedef ConnectionT *ConnectionTPtr;

  ConnectionTPtr AllocPtr(eConnType type, const StNetAddr *destaddr = NULL,
                          const StNetAddr *srcaddr = NULL) {
    StNetAddrKey key;
    if (IS_KEEPLIVE(type) && destaddr != NULL) {
      key.SetDestAddr(*destaddr);
    }

    if (IS_KEEPLIVE(type) && srcaddr != NULL) {
      key.SetSrcAddr(*srcaddr);
    }

    ConnectionTPtr conn = NULL;
    // 是否保持状态
    if (IS_KEEPLIVE(type)) {
      conn = (ConnectionTPtr)(m_hashlist_.HashFindData(&key));
    }

    if (conn == NULL) {
      conn = Instance<UtilPtrPool<ConnectionT>>()->AllocPtr();
      conn->SetConnType(type);
      if (destaddr != NULL) {
        conn->SetDestAddr(*destaddr);
      }
      if (srcaddr != NULL) {
        conn->SetAddr(*srcaddr);
      }
      if (IS_KEEPLIVE(type)) {
        key.SetDataPtr((void *)conn);
        int32_t r = m_hashlist_.HashInsert(&key);
        ASSERT(r >= 0);
      }
    }

    ASSERT(conn != NULL);
    return conn;
  }

  void FreePtr(ConnectionTPtr conn) {
    eConnType type = conn->GetConnType();
    if (IS_KEEPLIVE(type)) {
      StNetAddrKey key;
      key.SetDestAddr(conn->GetDestAddr());
      key.SetSrcAddr(conn->GetAddr());
      m_hashlist_.HashRemove(&key);
    }
    UtilPtrPoolFree(conn);
  }

private:
  StHashList<StNetAddrKey> m_hashlist_;
};

#endif