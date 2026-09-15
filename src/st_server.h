/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SERVER_H__
#define _ST_SERVER_H__

#include "st_connection.h"
#include "st_manager.h"
#include "st_poll.h"
#include "st_public.h"
#include "st_sys.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_util.h"
#include "app/st_c.h"
#include "app/st_sys.h"

using namespace sthread;
using namespace stlib;

template <class ConnectionT> class StServerConnection : public StConnection {
public:
  StServerConnection() : StConnection() {}
};

/* 用途：TCP/UDP 服务端；Listen 后 Loop 每接受一个连接创建一个协程。
 * 线程模型：在调用 Loop 的 OS 线程内运行；依赖 StSysSchedule。
 * 所有权：监听 fd 与 accept 出的连接由本对象/连接池管理；Hook 经 st_set_hook_flag。 */
template <class ConnetionT, int ServerT = eTCP_CONN> class StServer {
public:
  StServer() : m_osfd_(-1), m_item_(NULL), m_schedule_(NULL) {
    m_schedule_ = Instance<StSysSchedule>();
  }

  ~StServer() {
    m_osfd_ = -1;
    UtilPtrPoolFree(m_item_);
  }

  inline void SetHookFlag() { st_set_hook_flag(); }

  int32_t CreateSocket(const StNetAddr &addr) {
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

    m_item_ = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
    LOG_ASSERT(m_item_ != NULL);
    m_item_->SetOsfd(m_osfd_);
    m_item_->EnableOutput();
    m_item_->DisableInput();
    GlobalEventSchedule()->Add(m_item_);

    struct sockaddr *servaddr = m_addr_.GetSockAddr();
    if (::bind(m_osfd_, servaddr, sizeof(struct sockaddr)) < 0) {
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

      StNetAddr addr(*((struct sockaddr_in *)&clientaddr));
      StConnection *conn =
          (StConnection *)(Instance<StConnectionManager<ConnetionT> >()
                               ->AllocPtr((eConnType)ServerT, &addr));
      conn->SetOsfd(connfd);
      conn->SetDestAddr(addr);

      LOG_TRACE("connfd: %d", connfd);
      m_schedule_->CreateThread(NewStClosure(CallBack, conn, this));
    }
  }

  static void CallBack(StConnection *conn,
                       StServer<ConnetionT, ServerT> *server) {
    (void)server;
    StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
    LOG_ASSERT(item != NULL);

    item->SetOsfd(conn->GetOsfd());
    StThreadItem *thread = GlobalThreadSchedule()->GetActiveThread();

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
    conn->Close();
    UtilPtrPoolFree(item);
  }

private:
  StSysSchedule *m_schedule_;
  int m_osfd_;
  StNetAddr m_addr_;
  StEventItem *m_item_;
};

#endif
