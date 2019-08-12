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

class StServer : public referenceable
{
public:
    StServer(Manager *manager, uint32_t size = 1024) : 
        m_manager_(manager),
        m_osfd_(-1),
        m_buff_(NULL),
        m_item_(NULL)
    {
        m_buff_ = GetInstance<StBufferPool>()->GetBuffer(size);
    }

    // 默认使用TCP
    virtual int CreateSocket(const char *ip, uint16_t port, bool is_ipv6 = false)
    {
        m_addr_.SetAddr(ip, port, is_ipv6);
        m_osfd_ = st_socket(is_ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
        LOG_TRACE("m_osfd_: %d", m_osfd_);
        if (m_osfd_ < 0)
        {
            LOG_ERROR("create socket failed, ret[%d]", m_osfd_);
            return -1;
        }

        m_item_ = m_manager_->m_item_pool_.AllocPtr();
        ASSERT(m_item_ != NULL);
        m_item_->SetOsfd(m_osfd_);
        m_item_->EnableInput();
        m_manager_->m_event_scheduler_->Add(m_item_);

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
            if ((connfd = ::_accept(m_osfd_, (struct sockaddr*)NULL, NULL, 3000)) <= 0)
            {
                LOG_TRACE("connfd: %d, errno: %d, errmsg: %s", 
                    connfd, errno, strerror(errno));
                continue;
            }

            LOG_TRACE("connfd: %d", connfd);
            m_manager_->CreateThread(NewClosure(CallBack, this, connfd));
        }
    }

    virtual int32_t RecvData()
    {
        return 0;
    }

    virtual int32_t SendData()
    {
        return 0;
    }

    inline void CloseSocket()
    {
        if (m_osfd_ > 0)
        {
            st_close(m_osfd_);
            m_osfd_ = -1;
        }
    }

    static void CallBack(StServer *svr, int fd)
    {
        LOG_TRACE("svr: %p, fd: %d", svr, fd);
        char buf1[10240] = {'\0'};
        int ret = ::_recv(fd, buf1, sizeof(buf1), 0, 3000);

        char buf2[10240];
        sprintf(buf2, "GET / HTTP/1.1\r\n"
            "Host: www.baidu.com\r\n"
            "User-Agent: curl/7.54.0\r\n"
            "Accept: */*\r\n\r\n");
        ret = ::_send(fd, buf2, strlen(buf2), 0, 3000);
        LOG_TRACE("fd : %d, ret : %d, buf : %s", fd, ret, buf2);
        st_showdown(fd);
    }

private:
    int                 m_osfd_;
    StBuffer*           m_buff_;
    StNetAddress        m_addr_;
    Manager*            m_manager_;
    StEventItem*        m_item_;
};

ST_NAMESPACE_END

#endif