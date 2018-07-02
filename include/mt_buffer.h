/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_BUFFER_H_INCLUDED_
#define _MT_BUFFER_H_INCLUDED_

#include "mt_utils.h"
#include "mt_hash_list.h"

#define MT_BUFFER_MAP_SIZE  1000

MTHREAD_NAMESPACE_BEGIN

class IMtMsgBuffer;

typedef CPP_TAILQ_ENTRY<IMtMsgBuffer>  IMsgBufferLink;
typedef CPP_TAILQ_HEAD<IMtMsgBuffer>   IMsgBufferQueue;

class IMtMsgBuffer
{
public:
    IMtMsgBuffer(int max_len) : m_max_len_(max_len), 
        m_msg_len_(0), m_buf_type_(eBUFF_UNDEF), 
        m_recv_len_(0), m_send_len_(0)
    {
        m_msg_buff_ = malloc(max_len);
    }
    ~IMtMsgBuffer()
    {
        safe_free(m_msg_buff_);
    }
    inline void SetBufferType(eBuffType type)
    {
        m_buf_type_ = type;
    }
    inline eBuffType GetBufferType()
    {
        return m_buf_type_;
    }
    inline void Reset()
    {
        m_msg_len_  = 0;
        m_recv_len_ = 0;
        m_send_len_ = 0;
        m_buf_type_ = eBUFF_UNDEF;
    }
    inline void SetMsgLen(int msg_len)
    {
        m_msg_len_ = msg_len;
    }
    inline int GetMsgLen()
    {
        return m_msg_len_;
    }
    inline int GetMaxLen()
    {
        return m_max_len_;
    }
    inline void* GetMsgBuffer()
    {
        return m_msg_buff_;
    }
    inline int GetHaveSendLen()
    {
        return m_send_len_;
    }
    inline void SetHaveSendLen(int send_len)
    {
        m_send_len_ = send_len;
    }
    inline int GetHaveRecvLen()
    {
        return m_recv_len_;
    }
    inline void SetHaveRecvLen(int recv_len)
    {
        m_recv_len_ = recv_len;
    }
    
private:
    int   m_max_len_;
    void* m_msg_buff_;
    
    int   m_msg_len_;
    eBuffType   m_buf_type_;
    int   m_recv_len_;
    int   m_send_len_;
    

public:
    IMsgBufferLink m_entry_;
};

class IMsgBufferMap : public HashKey
{
public:
    IMsgBufferMap(int buff_size, int max_free = 300) : 
        m_max_buf_size_(buff_size), m_max_free_(max_free), m_queue_num_(0)
    {
        SetDataPtr(this);
        // 兼容内核的TAILQ_INIT
        CPP_TAILQ_INIT(&m_msg_queue_);
    }
    ~IMsgBufferMap()
    {
        // 定义两个指针变量
        IMtMsgBuffer* ptr = NULL;
        IMtMsgBuffer* tmp = NULL;

        CPP_TAILQ_FOREACH_SAFE(ptr, &m_msg_queue_, m_entry_, tmp)
        {
            CPP_TAILQ_REMOVE(&m_msg_queue_, ptr, m_entry_); // 删除对应的数据
            safe_delete(ptr);
            m_queue_num_--;
        }

        CPP_TAILQ_INIT(&m_msg_queue_);
    }
    IMtMsgBuffer* GetMsgBuffer()
    {
        IMtMsgBuffer* ptr = NULL;

        if (!CPP_TAILQ_EMPTY(&m_msg_queue_))
        {
            ptr = CPP_TAILQ_FIRST(&m_msg_queue_);
            CPP_TAILQ_REMOVE(&m_msg_queue_, ptr, m_entry_);
            m_queue_num_--;
        }
        else
        {
            ptr = new IMtMsgBuffer(m_max_buf_size_);
        }

        return ptr;
    }
    void FreeMsgBuffer(IMtMsgBuffer* ptr)
    {
        if (m_queue_num_ >= m_max_free_)
        {
            safe_delete(ptr);
        }
        else
        {
            ptr->Reset();
            CPP_TAILQ_INSERT_TAIL(&m_msg_queue_, ptr, m_entry_);
            m_queue_num_++;
        }
    }
    virtual uint32_t HashValue()
    {
        return m_max_buf_size_;
    }
    virtual int HashCmp(HashKey* rhs)
    {
        return m_max_buf_size_ - (int)rhs->HashValue();
    }

private:
    int m_max_free_;
    int m_max_buf_size_;
    int m_queue_num_;
    IMsgBufferQueue m_msg_queue_;
};

class IMsgBufferPool
{
public:
    void SetMaxFreeNum(int max_free)
    {
        m_max_free_ = max_free;
    }
    IMtMsgBuffer* GetMsgBuffer(int max_size);
    void FreeMsgBuffer(IMtMsgBuffer* msg_buf);
    ~IMsgBufferPool();
    
    explicit IMsgBufferPool(int max_free = 300);

private:
    int m_max_free_;
    HashList<IMsgBufferMap> *m_hash_map_;
};

MTHREAD_NAMESPACE_END

#endif
