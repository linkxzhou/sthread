/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_PROEV_H_INCLUDED_
#define _MT_PROEV_H_INCLUDED_

#include "mt_utils.h"
#include "mt_event_proxyer.h"
#include "mt_buffer.h"
#include "mt_connection.h"

MTHREAD_NAMESPACE_BEGIN

// 管理的单例类
class ISessionCtrl
{
public:
    ISessionCtrl()
    {
        m_cur_session_ = 0; // 初始化
        m_hash_map_ = new HashList<ISession>(100000);
    }
    ~ISessionCtrl()
    {
        safe_delete(m_hash_map_);
    }
    inline int GetSessionId(void)
    {
        m_cur_session_++;
        if (!m_cur_session_)
        {
            m_cur_session_++;
        }

        return m_cur_session_;
    }

    int InsertSession(ISession* session)
    {
        if (!m_hash_map_ || !session) {
            LOG_ERROR("map not init(%p), or session(%p)", m_hash_map_, session);
            return -100;
        }

        int flag = session->GetSessionFlag();
        if (flag & eSESSION_INUSE) {
            LOG_ERROR("Session in hash, session(%p), flag(%d)", session, flag);
            return -200;
        }    
        
        session->SetSessionFlag((int)eSESSION_INUSE);
        return m_hash_map_->HashInsert(session);
    }

    ISession* FindSession(int session_id)
    {
        if (!m_hash_map_) {
            LOG_ERROR("map not init(%p)", m_hash_map_);
            return NULL;
        }

        ISession key;
        key.SetSessionId(session_id);    
        return dynamic_cast<ISession*>(m_hash_map_->HashFind(&key));
    }

    void RemoveSession(int session_id)
    {
        if (!m_hash_map_) {
            LOG_ERROR("map not init(%p)", m_hash_map_);
            return;
        }

        ISession key;
        key.SetSessionId(session_id);    
        return m_hash_map_->HashRemove(&key);
    }

private:
    int m_cur_session_;
    HashList<ISession>* m_hash_map_;
};

class ISessionEventer;

typedef CPP_TAILQ_ENTRY<ISessionEventer> 	ISessionEventerEntry;
typedef CPP_TAILQ_HEAD<ISessionEventer>     ISessionEventerList;

// 通知
class ISessionEventer : public Eventer
{
public:
	ISessionEventer() : m_thread_(NULL), m_conn_(NULL)
	{
		m_proto_ = ePROTO_UNKNOWN;
		CPP_TAILQ_INIT(&m_list_);
	}
	virtual ~ISessionEventer() 
	{ }
	virtual int GetSessionId(void* pkg, int len,  int& session) 
	{ 
		return 0;
	}
	inline IMtConnection* GetConn()
	{
	  return m_conn_;
	}
	inline void SetConn(IMtConnection* conn)
	{
		m_conn_ = conn;
	}
	inline void SetProtoType(eMultiProto proto)
	{
		m_proto_ = proto;
	}
	inline eMultiProto GetProtoType()
	{
		return m_proto_;
	}
	inline void SetThread(ThreadBase* thread)
	{
		m_thread_ = thread;
	}
	inline ThreadBase* GetThread()
	{
		return m_thread_;
    }

	void InsertWriteWait(ISessionEventer* sev);

	void RemoveWriteWait(ISessionEventer* sev);

	virtual void NotifyWriteWait()
	{ }

    void SetLocalAddr(struct sockaddr_in* local_addr)
	{
		memcpy(&m_local_addr_, local_addr, sizeof(m_local_addr_));
	}

protected:
	eMultiProto	m_proto_;
	IMtConnection* m_conn_;

	int m_flag_;

	ThreadBase *m_thread_;

    struct sockaddr_in m_local_addr_;

public:
	ISessionEventerList m_list_;
	ISessionEventerEntry m_entry_;
};

class UdpSessionEventer : public ISessionEventer
{
public:
	UdpSessionEventer() : ISessionEventer()
	{
		SetProtoType(ePROTO_UDP_SESSION);
	}

	virtual int GetSessionId(void* pkg, int len,  int& session) 
	{ 
		return 0;
	}

	virtual void NotifyWriteWait();

	virtual int CloseSocket();

	virtual int InputNotify();

	virtual int OutputNotify();

	virtual int HangupNotify();

	virtual int AddRef(void* args);

	virtual int DelRef(void* args);
};

class TcpKeepEventer: public ISessionEventer
{
public:
    TcpKeepEventer() : ISessionEventer()
    { 
    	SetProtoType(ePROTO_TCP_KEEP);
    }

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
            LOG_DEBUG("remote close, fd %d, close connection", m_fd_);
            GetInstance<ConnectionCtrl>()->CloseIdleTcpKeep(m_conn_);
        }
        else
        {
            LOG_ERROR("m_conn_ is NULL, error");
        }
    }
};

// 单例类型
class ISessionEventerCtrl
{
public:
	typedef std::map<int, ISessionEventer*>	ISessionMap;
	typedef UtilsPtrPool<Eventer>			EventerQueue;
	typedef UtilsPtrPool<ISessionEventer> 	ISessionEventerQueue;

	int RegisterSession(int session_name, ISessionEventer* session);
	
	ISessionEventer* GetSessionName(int session_name);

	Eventer* GetEventer(int type, int session_name = 0);

	void FreeEventer(Eventer* ev);

private:
	ISessionMap 			    m_session_map_;
	EventerQueue 			    m_ev_queue_;
	ISessionEventerQueue 		m_sev_queue_;
};

MTHREAD_NAMESPACE_END

#endif
