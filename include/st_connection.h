/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_CONNECTION_H_INCLUDED_
#define _ST_CONNECTION_H_INCLUDED_

#include "st_util.h"
#include "st_base.h"
#include "st_buffer.h"
#include "st_heap_timer.h"
#include "st_manager.h" 

ST_NAMESPACE_BEGIN

class StConnection : public referenceable
{
public:
    StConnection() :
        m_type_(eUNDEF_CONN),
        m_osfd_(-1),
        m_timeout_(30000),
        m_sendbuf_(NULL),
        m_recvbuf_(NULL),
        m_item_(NULL)
    { 
        m_recvbuf_ = GetInstance< StBufferPool<> >()->GetBuffer(ST_SENDRECV_BUFFSIZE);
        m_sendbuf_ = GetInstance< StBufferPool<> >()->GetBuffer(ST_SENDRECV_BUFFSIZE);
    }

    virtual ~StConnection()
    {
        this->Reset();
    }

    virtual int32_t CreateSocket(const StNetAddress &addr)
    { 
        return -1;
    }

    inline void CloseSocket()
    {
        if (m_osfd_ > 0)
        {
            st_close(m_osfd_);
            m_osfd_ = -1;
        }
    }

    inline void SetAddr(const StNetAddress &addr)
    {
        m_addr_ = addr;
    }

    inline StNetAddress& GetAddr()
    {
        return m_addr_;
    }

    inline void SetDestAddr(const StNetAddress &destaddr)
    {
        m_destaddr_ = destaddr;
    }

    inline StNetAddress& GetDestAddr()
    {
        return m_destaddr_;
    }

    inline void SetOsfd(int fd)
    {
        m_osfd_ = fd;
    }

    inline int GetOsfd()
    {
        return m_osfd_;
    }

    inline eConnType GetConnType()
    {
        return m_type_;
    }

    inline void SetConnType(eConnType type)
    {
        m_type_ = type;
    }

    // 设置超时时间
    inline void SetTimeout(int32_t timeout)
    {
        m_timeout_ = timeout;
    }

    inline int32_t GetTimeout()
    {
        return m_timeout_;
    }

    virtual void Reset()
    {
        GetInstance< StBufferPool<> >()->FreeBuffer(m_sendbuf_);
        GetInstance< StBufferPool<> >()->FreeBuffer(m_recvbuf_);

        m_osfd_ = -1;
        m_sendbuf_ = NULL;
        m_recvbuf_ = NULL;
        m_type_ = eUNDEF_CONN;
        m_timeout_ = 30000;
    }

    // 判断是否支持keeplive
    inline bool Keeplive()
    {
        return false;
    }

    inline StBuffer* GetSendBuffer()
    {
        return m_sendbuf_;
    }

    inline StBuffer* GetRecvBuffer()
    {
        return m_recvbuf_;
    }

    int32_t SendData();

    int32_t RecvData();

    // 处理操作
    virtual int32_t HandleOutput(void *buf, int32_t &len)
    {
        return 0;
    }

    virtual int32_t HandleInput(void *buf, int32_t len)
    {
        return 0;
    }

    virtual int32_t HandleProcess()
    {
        return 0;
    }

    virtual int32_t HandleError(int32_t err)
    {
        return 0;
    }

protected:
    int             m_osfd_;
    StBuffer        *m_sendbuf_, *m_recvbuf_;
    StNetAddress    m_addr_, m_destaddr_;
    eConnType       m_type_;
    int32_t         m_timeout_;
    StEventBase     *m_item_;
};

template<class StEventBaseT>
class StServerConnection : public StConnection
{ 
public:
    typedef StEventBaseT    ServerStEventBaseT;

    StServerConnection() :
        StConnection()
    { }
}; 

template<class StEventBaseT>
class StClientConnection : public StConnection, public TimerEntry
{
public:
    typedef StEventBaseT    ClientStEventBaseT;

    StClientConnection() : 
        StConnection()
    { }

    virtual int32_t CreateSocket(const StNetAddress &addr)
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

        m_item_ = GetInstance< 
                UtilPtrPool<ClientStEventBaseT> 
            >()->AllocPtr();
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

    virtual int32_t OpenConnect(const StNetAddress &addr)
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
};

template<class ConnectionT>
class StConnectionManager
{
public:
    typedef ConnectionT* ConnectionTPtr;

    ConnectionTPtr AllocPtr(eConnType type, 
        const StNetAddress *destaddr = NULL, 
        const StNetAddress *srcaddr = NULL)
    {
        NetAddressKey key;
        if (IS_KEEPLIVE(type) && destaddr != NULL)
        {
            key.SetDestAddr(*destaddr);
        }

        if (IS_KEEPLIVE(type) && srcaddr != NULL)
        {
            key.SetSrcAddr(*srcaddr);
        }

        ConnectionTPtr conn = NULL;
        // 是否保持状态
        if (IS_KEEPLIVE(type))
        {
            conn = (ConnectionTPtr)(m_hashlist_.HashFindData(&key));
        }

        if (conn == NULL)
        {
            conn = GetInstance< UtilPtrPool<ConnectionT> >()->AllocPtr();
            conn->SetConnType(type);
            if (destaddr != NULL)
            {
                conn->SetDestAddr(*destaddr);
            }

            if (srcaddr != NULL)
            {
                conn->SetAddr(*srcaddr);
            }

            if (IS_KEEPLIVE(type))
            {
                key.SetDataPtr((void*)conn);
                int32_t r = m_hashlist_.HashInsert(&key);
                ASSERT(r >= 0);
            }
        }

        ASSERT(conn != NULL);
        return conn;
    }

    void FreePtr(ConnectionTPtr conn)
    {
        eConnType type = conn->GetConnType();
        if (IS_KEEPLIVE(type))
        {
            NetAddressKey key;
            key.SetDestAddr(conn->GetDestAddr());
            key.SetSrcAddr(conn->GetAddr());
            m_hashlist_.HashRemove(&key);
        }

        UtilPtrPoolFree(conn);
    }

private:
    HashList<NetAddressKey>   m_hashlist_;
};

ST_NAMESPACE_END

#endif