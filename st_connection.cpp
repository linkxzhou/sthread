/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_connection.h"
#include "st_manager.h"

ST_NAMESPACE_USING

int32_t StClientConnection::CreateSocket(const StNetAddress &addr)
{
    m_destaddr_ = addr;

    int protocol = SOCK_STREAM;
    if (IS_UDP_CONN(m_type_))
    {
        protocol = SOCK_DGRAM;
    }
    m_osfd_ = st_socket(addr.IsIPV6() ? AF_INET6 : AF_INET, protocol, 0);
    LOG_TRACE("m_osfd_: %d", m_osfd_);
    if (m_osfd_ < 0)
    {
        LOG_ERROR("create socket failed, ret[%d]", m_osfd_);
        return -1;
    }

    m_item_ = StConnectionItem::AllocStEventItem<StEventItem>();
    ASSERT(m_item_ != NULL);

    m_item_->SetOsfd(m_osfd_);
    m_item_->EnableOutput();
    m_item_->DisableInput();
    GetEventScheduler()->Add(m_item_);

    if (IS_TCP_CONN(m_type_))
    {
        int32_t rc = OpenConnect(addr);
        if (rc < 0)
        {
            LOG_ERROR("connect error, rc: %d", rc);
            CloseSocket();
            // TODO : 异常情况下需要清理item
            
            return -2;
        }
    }

    return m_osfd_;
}

int32_t StClientConnection::OpenConnect(const StNetAddress &addr)
{
    struct sockaddr *destaddr;
    addr.GetSockAddr(destaddr);

    int32_t err = 0;
    int32_t ret = ::_connect(m_osfd_, destaddr, sizeof(struct sockaddr_in), m_timeout_);
    if (ret < 0)
    {
        err = errno;
        if (err == EISCONN)
        {
            return 0;
        }
        else
        {
            if ((err == EINPROGRESS) || (err == EALREADY) || (err == EINTR))
            {
                LOG_ERROR("open connect not ok, sock: %d, errno: %d, strerr: %s", 
                    m_osfd_, err, strerror(err));
                return -1;
            }
            else
            {
                LOG_ERROR("open connect not ok, sock: %d, errno: %d, strerr: %s", 
                    m_osfd_, err, strerror(err));
                return -2;
            }
        }
    }
    
    return 0;
}