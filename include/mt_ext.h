/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_EXT_H_INCLUDED_
#define _MT_EXT_H_INCLUDED_

#include "mt_utils.h"
#include "mt_core.h"
#include "mt_buffer.h"
#include "mt_connection.h"

MTHREAD_NAMESPACE_BEGIN

class TcpLongEventer: public Eventer
{
public:
    TcpLongEventer()
    { }

    virtual int InputNotify()
    {
        KeepaliveClose();
        return -1;
    }

    virtual int OutputNotify()
    {
        KeepaliveClose();
        return -1;
    }

    virtual int HangupNotify()
    {
        KeepaliveClose();
        return -1;
    }
    
    // 关闭连接
    inline void KeepaliveClose()
    {
        if (m_conn_)
        {
            LOG_DEBUG("remote close, fd [%d] close connection", m_fd_);
            GetInstance<ConnectionPool>()->CloseIdleTcpLong(m_conn_);
        }
        else
        {
            LOG_WARN("m_conn_ is NULL");
        }
    }

protected:
	IMtConnection* m_conn_;
	ThreadBase *m_thread_;

    struct sockaddr_in m_local_addr_;
};

class EventerPool
{
public:
	typedef UtilsPtrPool<Eventer>			IEventerPool;
    typedef UtilsPtrPool<TcpLongEventer>    TcpLongEventerPool;

	Eventer* GetEventer(int type = eEVENT_THREAD);

	void FreeEventer(Eventer* ev);

private:
	IEventerPool        m_ev_pool_;
    TcpLongEventerPool  m_tcplong_ev_pool_;
};

MTHREAD_NAMESPACE_END

#endif