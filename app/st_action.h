/*
 * Copyright (C) zhoulv2000@163.com
 *
 * Slim client-side action layer for sample apps (plan/04).
 * Implements IMessage / IMtAction / IMtActionClient on top of
 * udp_sendrecv / tcp_sendrecv so apps keep their protocol handlers.
 */

#ifndef _ST_ACTION_H_
#define _ST_ACTION_H_

#include "app/st_c.h"
#include "src/st_public.h"
#include "stlib/st_def.h"
#include "stlib/st_log.h"
#include "stlib/st_singleton.h"
#include <netinet/in.h>
#include <string.h>
#include <vector>

/* Historical name used by sample apps; maps to short TCP. */
#ifndef eTCP_SHORT_CONN
#define eTCP_SHORT_CONN eTCP_CONN
#endif

#ifndef safe_free
#define safe_free(p) st_safe_free(p)
#endif
#ifndef safe_delete
#define safe_delete(p) st_safe_delete(p)
#endif

class IMessage {
public:
  virtual int32_t HandleProcess() { return -1; }
  IMessage() : m_data_(NULL) {}
  virtual ~IMessage() {}
  inline void SetDataPtr(void *data) { m_data_ = data; }
  inline void *GetDataPtr() { return m_data_; }

private:
  void *m_data_;
};

class IMtAction {
public:
  IMtAction() { Reset(); }
  virtual ~IMtAction() { Reset(true); }

  inline void SetMsgBufferSize(int buff_size) { m_buff_size_ = buff_size; }
  inline int GetMsgBufferSize() {
    return (m_buff_size_ > 0) ? m_buff_size_ : 65535;
  }
  inline void SetConnType(eConnType type) { m_conn_type_ = type; }
  inline eConnType GetConnType() { return m_conn_type_; }
  inline void SetErrno(int err) { m_errno_ = err; }
  inline int GetErrno() { return m_errno_; }
  inline void SetIMessagePtr(IMessage *msg) { m_msg_ = msg; }
  inline IMessage *GetIMessagePtr() { return m_msg_; }
  inline void SetMsgDstAddr(struct sockaddr_in *dst) {
    if (dst != NULL) {
      memcpy(&m_addr_, dst, sizeof(m_addr_));
      m_has_addr_ = true;
    }
  }
  inline struct sockaddr_in *GetMsgDstAddr() {
    return m_has_addr_ ? &m_addr_ : NULL;
  }

  virtual int HandleEncode(void *buf, int &len, IMessage *msg) {
    (void)buf;
    (void)len;
    (void)msg;
    return 0;
  }
  virtual int HandleInput(void *buf, int len, IMessage *msg) {
    (void)buf;
    (void)len;
    (void)msg;
    return 0;
  }
  virtual int HandleProcess(void *buf, int len, IMessage *msg) {
    (void)buf;
    (void)len;
    (void)msg;
    return 0;
  }
  virtual int HandleError(int err, IMessage *msg) {
    (void)err;
    (void)msg;
    return 0;
  }

  void Reset(bool /*reset_all*/ = false) {
    m_conn_type_ = eTCP_CONN;
    m_errno_ = eERR_NONE;
    m_buff_size_ = 0;
    m_msg_ = NULL;
    m_has_addr_ = false;
    memset(&m_addr_, 0, sizeof(m_addr_));
  }

protected:
  eConnType m_conn_type_;
  int m_errno_;
  int m_buff_size_;
  IMessage *m_msg_;
  bool m_has_addr_;
  struct sockaddr_in m_addr_;
};

typedef std::vector<IMtAction *> IMtActionList;

class IMtActionClient {
public:
  inline void Add(IMtAction *action) { m_action_list_.push_back(action); }
  int SendRecv(int timeout);

protected:
  IMtActionList m_action_list_;
};

#endif
