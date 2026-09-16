/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SERVER_H__
#define _ST_SERVER_H__

#include "app/st_sys.h"

#ifdef __cplusplus
extern "C" {
#endif
void st_set_hook_flag(); /* C4: 避免 include app/st_c.h */
#ifdef __cplusplus
}
#endif
#include "st_connection.h"
#include "st_manager.h"
#include "st_poll.h"
#include "st_public.h"
#include "st_sys.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_util.h"

using namespace sthread; /* C3: 全局连接/服务类需 sthread 类型 */

template <class ConnectionT> class StServerConnection : public StConnection {
public:
  StServerConnection() : StConnection() {}

  /* Server conns are accept()-created; Create() is unused (A5 default). */
  virtual int32_t Create(const stlib::StNetAddr &addr) {
    (void)addr;
    return -1;
  }
};

/* 用途：TCP/UDP 服务端；Listen 后 Loop 每接受一个连接创建一个协程。
 * 线程模型：在调用 Loop 的 OS 线程内运行；依赖 StSysSchedule。
 * 所有权：监听 fd 与 accept 出的连接由本对象/连接池管理；Hook 经
 * st_set_hook_flag。 */
template <class ConnetionT, int ServerT = eTCP_CONN> class StServer {
public:
  StServer() : m_osfd_(-1), m_item_(NULL), m_schedule_(NULL) {
    m_schedule_ = stlib::Instance<StSysSchedule>();
  }

  ~StServer() {
    if (m_item_ != NULL) {
      GlobalEventSchedule()->ClearItem(m_item_);
      stlib::UtilPtrPoolFree(m_item_);
      m_item_ = NULL;
    }
    if (m_osfd_ >= 0) {
      sys_close(m_osfd_);
      m_osfd_ = -1;
    }
  }

  inline void SetHookFlag() { st_set_hook_flag(); }

  int32_t CreateSocket(const stlib::StNetAddr &addr) {
    m_addr_ = addr;

    int protocol = SOCK_STREAM;
    if (IS_UDP_CONN(ServerT)) {
      protocol = SOCK_DGRAM;
    }
    m_osfd_ = sys_socket(addr.IsIPV6() ? AF_INET6 : AF_INET, protocol, 0);
    LOG_TRACE("m_osfd_: %d", m_osfd_);
    if (m_osfd_ < 0) {
      LOG_ERROR("create socket failed, ret[%d]", m_osfd_);
      return -1;
    }
    {
      int yes = 1;
      ::setsockopt(m_osfd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }

    m_item_ = stlib::Instance<stlib::UtilPtrPool<StEventItem> >()->AllocPtr();
    LOG_ASSERT(m_item_ != NULL);
    m_item_->SetOsfd(m_osfd_);
    m_item_->EnableOutput();
    m_item_->DisableInput();
    GlobalEventSchedule()->Add(m_item_);

    socklen_t bindlen = m_addr_.IsIPV6() ? sizeof(struct sockaddr_in6)
                                         : sizeof(struct sockaddr_in);
    struct sockaddr *servaddr = m_addr_.IsIPV6()
                                    ? (struct sockaddr *)m_addr_.GetSock6Addr()
                                    : m_addr_.GetSockAddr();
    if (::bind(m_osfd_, servaddr, bindlen) < 0) {
      LOG_ERROR("bind socket error: %s(errno: %d)", strerror(errno), errno);
      return -2;
    }

    LOG_TRACE("addr: %s", m_addr_.IPPort());
    return m_osfd_;
  }

  bool Listen(int backlog = 128) {
    int r = ::listen(m_osfd_, backlog);
    return (r < 0) ? false : true;
  }

  void Loop() {
    LOG_ASSERT(m_schedule_ != NULL);

    int connfd = -1;
    while (true) {
      struct sockaddr clientaddr;
      socklen_t addrlen = sizeof(struct sockaddr);
      if ((connfd = st_accept(m_osfd_, (struct sockaddr *)&clientaddr,
                              &addrlen)) <= 0) {
        LOG_TRACE("connfd: %d, errno: %d, errmsg: %s", connfd, errno,
                  strerror(errno));
        continue;
      }

      stlib::StNetAddr addr(*((struct sockaddr_in *)&clientaddr));
      StConnection *conn =
          (StConnection *)(stlib::Instance<StConnectionManager<ConnetionT> >()
                               ->AllocPtr((eConnType)ServerT, &addr));
      if (conn == NULL) {
        LOG_ERROR("AllocPtr failed, close connfd: %d", connfd);
        sys_close(connfd);
        continue;
      }
      conn->SetOsfd(connfd);
      conn->SetDestAddr(addr);

      LOG_TRACE("connfd: %d", connfd);
      m_schedule_->CreateThread(NewStClosure(CallBack, conn, this));
    }
  }

  static void CallBack(StConnection *conn,
                       StServer<ConnetionT, ServerT> *server) {
    (void)server;
    StEventItem *item =
        stlib::Instance<stlib::UtilPtrPool<StEventItem> >()->AllocPtr();
    LOG_ASSERT(item != NULL);

    item->SetOsfd(conn->GetOsfd());
    StThreadItem *thread = GlobalThreadSchedule()->GetActiveThread();
    if (thread == NULL) {
      LOG_ERROR("CallBack active thread is NULL");
      GlobalEventSchedule()->ClearItem(item);
      stlib::UtilPtrPoolFree(item);
      if (conn != NULL) {
        /* B20/D5: 错误路径也归还连接池 */
        stlib::Instance<StConnectionManager<ConnetionT> >()->FreePtr(
            (ConnetionT *)conn);
      }
      return;
    }

    int32_t ret = 0;
    do {
      item->EnableInput();
      item->DisableOutput();
      GlobalEventSchedule()->Add(item);
      LOG_TRACE("CallBack ==========[name:%s]========== %p", thread->GetName(),
                item);

      if ((ret = conn->RecvData()) < 0) {
        conn->DoError(ret);
        goto CALLBACK_EXIT1;
      }

      if ((ret = conn->DoProcess()) < 0) {
        conn->DoError(ret);
        goto CALLBACK_EXIT1;
      }

      if ((ret = conn->SendData()) < 0) {
        conn->DoError(ret);
        goto CALLBACK_EXIT1;
      }
    } while (conn->Keeplive());

  CALLBACK_EXIT1:
    GlobalEventSchedule()->ClearItem(item);
    stlib::UtilPtrPoolFree(item);
    /* B10/D5: Close 由 FreePtr→Reset 统一完成，并归还连接池 */
    stlib::Instance<StConnectionManager<ConnetionT> >()->FreePtr(
        (ConnetionT *)conn);
  }

private:
  StSysSchedule *m_schedule_;
  int m_osfd_;
  stlib::StNetAddr m_addr_;
  StEventItem *m_item_;
};

#endif
