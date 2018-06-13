/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_ACTION_H_INCLUDED_
#define _MT_ACTION_H_INCLUDED_

#include "mt_utils.h"
#include "mt_buffer.h"
#include "mt_event_proxyer.h"
#include "mt_connection.h"

MTHREAD_NAMESPACE_USING

class IMessage
{
public:
    // 虚函数，子类继承
    virtual int HandleProcess() 
    { 
        return -1; 
    }
    IMessage() { }
    virtual ~IMessage() { }
};

// IMtAction继承ISession
class IMtAction : public ISession, public IMtActionBase
{
public:
    IMtAction();
    virtual ~IMtAction();

    inline void SetMsgBufferSize(int buff_size)
    {
        m_buff_size_ = buff_size;
    }
    int GetMsgBufferSize()
    {
        return (m_buff_size_ > 0) ? m_buff_size_ : 65535;
    }
    void SetSessionName(int name)
    {
        m_session_name_ = name;
    }
    int GetSessionName()
    {
        return m_session_name_;
    }
    void SetProtoType(eMultiProto proto)
    {
        m_proto_ = proto;
    }
    eMultiProto GetProtoType()
    {
        return m_proto_;
    }
    void SetConnType(eConnType type)
    {
        m_conn_type_ = type;
    }
    eConnType GetConnType()
    {
        return m_conn_type_;
    }
    void SetErrno(eMultiError err)
    {
        m_errno_ = err;
    }
    eMultiError GetErrno()
    {
        return m_errno_;
    }
    void SetCost(int cost)
    {
        m_time_cost_ = cost;
    }
    int GetCost()
    {
        return m_time_cost_;
    }
    void SetMsgFlag(eMultiState flag)
    {
        m_flag_ = flag;
    }
    eMultiState GetMsgFlag()
    {
        return m_flag_;
    }
    void SetIMessagePtr(IMessage* msg)
    {
        m_msg_ = msg;
    }
    IMessage* GetIMessagePtr()
    {
        return m_msg_;
    }
    void SetIConnection(IMtConnection* conn)
    {
        m_conn_ = conn;
    }
    IMtConnection* GetIConnection()
    {
        return m_conn_;
    }
    void* GetOwnerThread()
    {
        return m_thread_;
    }
    void SetOwnerThread(ThreadBase* thread)
    {
        m_thread_ = thread;
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

protected:
    eMultiState         m_flag_;
    eMultiProto         m_proto_;
    eConnType           m_conn_type_;
    eMultiError         m_errno_;

    int                 m_time_cost_;
    int                 m_buff_size_;
    int                 m_session_name_;

    IMessage*           m_msg_;
    IMtConnection*      m_conn_;
    ThreadBase*         m_thread_;
};

class IMtActionFrame
{
    typedef std::vector<IMtAction*> IMtActionList;

public:
    inline void Add(IMtAction* action)
    {
        m_action_list_.push_back(action);
    }

    int SendRecv(int timeout);

    int Poll(IMtActionList list, int mask, int timeout);

    int NewSock();

    int Open(int timeout);

    int Sendto(int timeout);

    int Recvfrom(int timeout);

    int RunSendRecv(int timeout);

private:
    IMtActionList m_action_list_;
};

#endif
