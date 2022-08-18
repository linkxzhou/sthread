/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SERVER_H__
#define _ST_SERVER_H__

#include "st_connection.h"
#include "st_manager.h"
#include "st_netaddr.h"
#include "st_sys.h"
#include "st_util.h"

template <class ConnectionT> class StServerConnection : public StConnection {
public:
  StServerConnection() : StConnection() {}
};

template <class ConnetionT, int ServerT = eTCP_CONN> class StServer {
public:
  StServer() : m_osfd_(-1), m_item_(NULL) {
    m_osfd_ = -1;
    m_manager_ = Instance<Manager>();
  }

  ~StServer() {
    m_osfd_ = -1;
    UtilPtrPoolFree(m_item_);
  }

  inline void SetHookFlag() { m_manager_->SetHookFlag(); }

  int32_t CreateSocket(const StNetAddress &addr) {
    m_addr_ = addr;

    int protocol = SOCK_STREAM;
    if (IS_UDP_CONN(ServerT)) {
      protocol = SOCK_DGRAM;
    }
    m_osfd_ = st_socket(addr.IsIPV6() ? AF_INET6 : AF_INET, protocol, 0);
    LOG_TRACE("m_osfd_: %d", m_osfd_);
    if (m_osfd_ < 0) {
      LOG_ERROR("create socket failed, ret[%d]", m_osfd_);
      return -1;
    }

    m_item_ = Instance<UtilPtrPool<typename ConnetionT::ServerStEventSuperT>>()
                  ->AllocPtr();

    ASSERT(m_item_ != NULL);
    m_item_->SetOsfd(m_osfd_);
    m_item_->EnableOutput();
    m_item_->DisableInput();
    GlobalEventScheduler()->Add(m_item_);

    struct sockaddr *servaddr;
    m_addr_.GetSockAddr(servaddr);
    if (::bind(m_osfd_, (struct sockaddr *)servaddr, sizeof(struct sockaddr)) <
        0) {
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
    ASSERT(m_manager_ != NULL);

    int connfd = -1;
    while (true) {
      struct sockaddr clientaddr;
      socklen_t addrlen = sizeof(struct sockaddr);
      if ((connfd = ::_accept(m_osfd_, (struct sockaddr *)&clientaddr,
                              &addrlen)) <= 0) {
        LOG_TRACE("connfd: %d, errno: %d, errmsg: %s", connfd, errno,
                  strerror(errno));
        continue;
      }

      StNetAddress addr(*((struct sockaddr_in *)&clientaddr));
      StConnection *conn =
          (StConnection *)(Instance<StConnectionManager<ConnetionT>>()
                               ->AllocPtr((eConnType)ServerT, &addr));
      conn->SetOsfd(connfd);
      conn->SetDestAddr(addr);

      LOG_TRACE("connfd: %d", connfd);
      m_manager_->CreateThread(NewStClosure(CallBack, conn, this));
    }
  }

  static void CallBack(StConnection *conn,
                       StServer<ConnetionT, ServerT> *server) {
    Manager *manager = server->m_manager_;
    ASSERT(manager != NULL);

    StEventSuper *item =
        Instance<UtilPtrPool<typename ConnetionT::ServerStEventSuperT>>()
            ->AllocPtr();
    ASSERT(item != NULL);

    item->SetOsfd(conn->GetOsfd());
    StThreadSuper *thread = GlobalThreadScheduler()->GetActiveThread();

    int32_t ret = 0;
    do {
      item->EnableInput();
      item->DisableOutput();
      GlobalEventScheduler()->Add(item);
      LOG_TRACE("CallBack ==========[name:%s]========== %p", thread->GetName(),
                item);

      // 收数据
      if ((ret = conn->RecvData()) < 0) {
        conn->HandleError(ret);
        goto CALLBACK_EXIT1;
      }

      // 处理数据
      if ((ret = conn->HandleProcess()) < 0) {
        conn->HandleError(ret);
        goto CALLBACK_EXIT1;
      }

      // 发数据
      if ((ret = conn->SendData()) < 0) {
        conn->HandleError(ret);
        goto CALLBACK_EXIT1;
      }
    } while (conn->Keeplive());

  CALLBACK_EXIT1:
    // 清理句柄数据
    GlobalEventScheduler()->Close(item);
    conn->CloseSocket();
    UtilPtrPoolFree(item);
  }

private:
  Manager *m_manager_;
  int m_osfd_;
  StNetAddress m_addr_;
  StEventSuper *m_item_;
};

#endif