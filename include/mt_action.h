/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_ACTION_H_INCLUDED_
#define _MT_ACTION_H_INCLUDED_

#include "mt_utils.h"
#include "mt_buffer.h"
#include "mt_core.h"
#include "mt_connection.h"
#include "mt_c.h"
#include "mt_frame.h"

ST_NAMESPACE_USING

class IMtAction : public IMtActionBase
{
public:
    IMtAction()
    {
        Reset();
    }

    virtual ~IMtAction()
    {
        Reset(true);
    }

    inline void SetMsgBufferSize(int buff_size)
    {
        m_buff_size_ = buff_size;
    }

    inline int GetMsgBufferSize()
    {
        return (m_buff_size_ > 0) ? m_buff_size_ : 65535;
    }

    inline void SetConnType(eConnType type)
    {
        m_conn_type_ = type;
    }

    inline eConnType GetConnType()
    {
        return m_conn_type_;
    }

    inline void SetErrno(eActionError err)
    {
        m_errno_ = err;
    }

    inline eActionError GetErrno()
    {
        return m_errno_;
    }

    inline void SetCost(int cost)
    {
        m_time_cost_ = cost;
    }

    inline int GetCost()
    {
        return m_time_cost_;
    }

    inline void SetMsgFlag(eActionState flag)
    {
        m_flag_ = flag;
    }

    inline eActionState GetMsgFlag()
    {
        return m_flag_;
    }

    inline void SetIMessagePtr(IMessage* msg)
    {
        m_msg_ = msg;
    }

    inline IMessage* GetIMessagePtr()
    {
        return m_msg_;
    }

    inline void SetIConnection(IMtConnection* conn)
    {
        m_conn_ = conn;
    }

    inline IMtConnection* GetIConnection()
    {
        return m_conn_;
    }

    inline ThreadBase* GetOwnerThread()
    {
        return m_thread_;
    }

    Eventer* GetEventer();

    int InitConnection();

    void Reset(bool reset_all = false);

public:
    virtual int HandleEncode(void* buf, int& len, IMessage* msg)
    {
        return 0;
    }
    
    virtual int HandleInput(void* buf, int len, IMessage* msg)
    {
        return 0;
    }

    virtual int HandleProcess(void* buf, int len, IMessage* msg)
    {
        return 0;
    }

    virtual int HandleError(int err, IMessage* msg)
    {
        return 0;
    }

    // 四个处理阶段
    virtual int DoEncode();

    virtual int DoInput();

    virtual int DoProcess();

    virtual int DoError();

    // server模式的特有
    inline void SetAcceptFd(int fd)
    {
        m_acceptfd_ = fd;
    }

    inline int GetAcceptFd()
    {
        return m_acceptfd_;
    }

    inline void SetTimeout(int timeout)
    {
        m_timeout_ = timeout;
    }

    inline int GetTimeout()
    {
        return m_timeout_;
    }

protected:
    eActionState        m_flag_;
    eConnType           m_conn_type_;
    eActionError        m_errno_;

    int                 m_time_cost_;
    int                 m_buff_size_;

    IMessage            *m_msg_;
    IMtConnection       *m_conn_;
    ThreadBase          *m_thread_;

    // server模式的特有
    int                 m_acceptfd_;
    int                 m_timeout_;
};

class IMtActionServer;
typedef std::vector<IMtAction*> IMtActionList;

class IMtActionRunable
{
public:
    int Poll(IMtActionList list, int mask, int timeout);

    int Sendto(IMtActionList list, int timeout);

    int Recvfrom(IMtActionList list, int timeout);

protected:
    IMtActionList m_action_list_;
};

class IMtActionClient : public IMtActionRunable
{
public:
    inline void Add(IMtAction* action)
    {
        m_action_list_.push_back(action);
        LOG_TRACE("m_action_list_ size : %d", m_action_list_.size());
    }
    
    int SendRecv(int timeout);

    int RunSendRecv(int timeout);

    int Open(int timeout);

    int NewSock();
};

#define TRANSFORM_MAX_SIZE 10

class NewThreadTransform
{
public:
    NewThreadTransform() : m_top_(0), m_timeout_(0), m_server_(NULL)
    { }

    inline void Add(IMtAction* action, int fd, struct sockaddr *addr)
    {
        if (m_top_ > TRANSFORM_MAX_SIZE)
        {
            LOG_ERROR("over size");
            return ;
        }

        m_action_arr_[m_top_] = action;
        m_fd_arr_[m_top_] = fd;
        memcpy(&(m_client_address_arr_[m_top_]), addr, sizeof(struct sockaddr)); 
        m_top_++;
    }

    inline int Size()
    {
        return m_top_;
    }

    inline void SetTimeout(time64_t t)
    {
        m_timeout_ = t;
    }

    inline time64_t GetTimeout()
    {
        return m_timeout_;
    }

    inline void SetServer(IMtActionServer *server)
    {
        m_server_ = server;
    }

    inline IMtActionServer* GetServer()
    {
        return m_server_;
    }

public:
    IMtAction* m_action_arr_[TRANSFORM_MAX_SIZE+1];
    int m_fd_arr_[TRANSFORM_MAX_SIZE+1];
    struct sockaddr m_client_address_arr_[TRANSFORM_MAX_SIZE+1];

private:
    int m_top_;
    time64_t m_timeout_;
    IMtActionServer *m_server_;
};

typedef IMtAction* (*_IMtAction_construct)();
typedef void (*_IMtAction_destruct)(IMtAction*);

class IMtActionServer : public IMtActionRunable
{
public:
    IMtActionServer(struct sockaddr_in *servaddr, eConnType type) : 
        m_accept_action_(new IMtAction()), m_listen_fd_(-1)
    { 
        if (NULL != servaddr) 
        {
            memcpy(&m_addr_, servaddr, sizeof(struct sockaddr_in));
            m_accept_action_->SetConnType(type);
            m_accept_action_->SetMsgDstAddr(servaddr);
        }
        else
        {
            LOG_ERROR("servaddr is NULL");
        }
    }
    
    int Loop(int timeout);

    inline void SetCallBack(_IMtAction_construct c = NULL, _IMtAction_destruct d = NULL)
    {
        m_construct_callback_ = c;
        m_destruct_callback_ = d;
    } 

    // 使用静态方法
    static void NewThread(void *args);

private:
    // 连接的action
    IMtAction *m_accept_action_;
    int m_listen_fd_;
    struct sockaddr_in m_addr_;

    static _IMtAction_construct m_construct_callback_;
    static _IMtAction_destruct  m_destruct_callback_;
};

#endif