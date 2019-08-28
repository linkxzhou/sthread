/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SERVER_H_INCLUDED_
#define _ST_SERVER_H_INCLUDED_

#include "st_util.h"
#include "st_manager.h"
#include "st_sys.h"
#include "st_netaddr.h"

ST_NAMESPACE_BEGIN

class StServerConnection : public StConnectionItem
{
}; 

template<typename ManagerT, typename ConnetionT>
class StServer
{
public:
    StServer(ManagerT *manager) : 
        m_osfd_(-1), 
        m_manager_(manager),
        m_item_(NULL)
    {
        m_osfd_ = -1;
    }

    ~StServer()
    {
        FreePtrStEventItem(m_item_);
    }

    int32_t CreateSocket(const StNetAddress &addr)
    {
        m_addr_ = addr;

        m_osfd_ = st_socket(is_ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
        LOG_TRACE("m_osfd_: %d", m_osfd_);
        if (m_osfd_ < 0)
        {
            LOG_ERROR("create socket failed, ret[%d]", m_osfd_);
            return -1;
        }

        m_item_ = m_manager_->AllocStEventItem();
        ASSERT(m_item_ != NULL);

        m_item_->SetOsfd(m_osfd_);
        m_item_->EnableOutput();
        m_item_->DisableInput();
        GetEventScheduler()->Add(m_item_);

        struct sockaddr *servaddr;
        m_addr_.GetSockAddr(servaddr);
        if (::bind(m_osfd_, (struct sockaddr*)servaddr, sizeof(struct sockaddr)) < 0)
        {
            LOG_ERROR("bind socket error: %s(errno: %d)", strerror(errno), errno);
            return -2;
        }

        LOG_TRACE("addr: %s", m_addr_.IPPort());
        
        return m_osfd_;
    }

    bool Listen(int backlog = 100)
    {
        int r = ::listen(m_osfd_, backlog);
        return (r < 0) ? false : true;
    }

    void Loop()
    {
        ASSERT(m_manager_ != NULL);

        int connfd = -1;
        while (true)
        {
            if ((connfd = ::_accept(m_osfd_, (struct sockaddr*)NULL, NULL)) <= 0)
            {
                LOG_TRACE("connfd: %d, errno: %d, errmsg: %s", 
                    connfd, errno, strerror(errno));
                continue;
            }

            
            LOG_TRACE("connfd: %d", connfd);
            m_manager_->CreateThread(NewClosure(this, CallBack, connfd));
        }
    }

    virtual void CallBack(StServerConnection *conn)
    {
        StEventItem* item = svr->m_manager_->AllocStEventItem();
        ASSERT(item != NULL);

        item->SetOsfd(conn->GetOsfd());
        item->EnableInput();
        item->DisableOutput();
        GetEventScheduler()->Add(item);

        while (true)
        {
            conn->RecvData();
            conn->SendData();

            // LOG_TRACE("svr: %p, fd: %d", svr, fd);
            // char buf1[10240] = {'\0'};
            // int n = ::_recv(fd, buf1, sizeof(buf1), 0, 3000);

            // char buf2[10240];
            // sprintf(buf2, "HTTP/1.1 200 OK\r\n"
            //     "Content-Length: 1\r\n"
            //     "Content-Type: text/html\r\n"
            //     "Date: Mon, 12 Aug 2019 15:48:13 GMT\r\n"
            //     "Server: sthread/1.0.1\r\n\r\n1");
            // n = ::_send(fd, buf2, strlen(buf2), 0, 3000);
            // LOG_TRACE("fd: %d, n: %d, buf: %s", fd, n, buf2);
            // // st_close(fd);
        }

        FreePtrStEventItem(item);
    }

private:
    ManagerT*       m_manager_;
    int             m_osfd_;
    StNetAddress    m_addr_;
    StEventItem*    m_item_;
};

ST_NAMESPACE_END

#endif