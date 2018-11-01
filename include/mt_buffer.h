/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_BUFFER_H_INCLUDED_
#define _MT_BUFFER_H_INCLUDED_

#include "mt_utils.h"
#include "mt_hash_list.h"

#define MT_BUFFER_BUCKET_SIZE  1000

MTHREAD_NAMESPACE_BEGIN

class IMtMsgBuffer;

class IMessage
{
public:
    // 虚函数，子类继承
    virtual int HandleProcess() 
    { 
        return -1; 
    }
    IMessage() 
    { }
    virtual ~IMessage() 
    { }
    inline void SetDataPtr(void *data)
    {
        m_data_ = data;
    }
    inline void* GetDataPtr()
    {
        return m_data_;
    }
    
private:
    void *m_data_;
};

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

class IMsgBufferBucket : public HashKey
{
public:
    IMsgBufferBucket(int buff_size, int max_free = 300) : 
        m_max_buf_size_(buff_size), m_max_free_(max_free), m_queue_num_(0)
    {
        SetDataPtr(this);
        // 兼容内核的TAILQ_INIT
        CPP_TAILQ_INIT(&m_msg_queue_);
    }
    
    ~IMsgBufferBucket()
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

    IMtMsgBuffer* GetMsgBuffer(int max_size)
    {
        if (!m_hash_bucket_)
        {
            LOG_ERROR("IMsgBufferPool not init, hash %p", m_hash_bucket_);
            return NULL;
        }

        // 重新字节对齐处理
        max_size = MT_ALGIN(max_size);

        IMsgBufferBucket* msg_bucket = NULL;
        IMsgBufferBucket msg_key(max_size);
        HashKey* hash_item = m_hash_bucket_->HashFind(&msg_key);

        if (hash_item)
        {
            msg_bucket = any_cast<IMsgBufferBucket>(hash_item->GetDataPtr());
            if (msg_bucket)
            {
                return msg_bucket->GetMsgBuffer();
            }
            else
            {
                LOG_ERROR("Hash item: %p, msg_bucket: %p impossible, clean it", hash_item, msg_bucket);
                m_hash_bucket_->HashRemove(any_cast<IMsgBufferBucket>(hash_item));
                safe_delete(hash_item);
                return NULL;
            }
        }
        else
        {
            msg_bucket = new IMsgBufferBucket(max_size, m_max_free_);
            if (!msg_bucket)
            {
                LOG_ERROR("maybe no more memory failed. size: %d", max_size);
                return NULL;
            }
            m_hash_bucket_->HashInsert(msg_bucket);
            return msg_bucket->GetMsgBuffer();
        }
    }

    void FreeMsgBuffer(IMtMsgBuffer* msg_buf)
    {
        if (!m_hash_bucket_ || !msg_buf)
        {
            LOG_ERROR("IMsgBufferPool not init or input error! hash %p, msg_buf: %p", 
                m_hash_bucket_, msg_buf);
            safe_delete(msg_buf);
            return ;
        }

        msg_buf->Reset();
        IMsgBufferBucket* msg_bucket = NULL;
        IMsgBufferBucket msg_key(msg_buf->GetMaxLen());
        HashKey* hash_item = m_hash_bucket_->HashFind(&msg_key);
        
        if (hash_item)
        {
            msg_bucket = any_cast<IMsgBufferBucket>(hash_item->GetDataPtr());
        }

        if (!hash_item || !msg_bucket)
        {
            LOG_ERROR("IMsgBufferPool find no queue, maybe error: %d", msg_buf->GetMaxLen());
            safe_delete(msg_buf);
        }
        else
        {
            msg_bucket->FreeMsgBuffer(msg_buf);
        }
        
        return ;
    }

    ~IMsgBufferPool()
    {
        if (!m_hash_bucket_)
        {
            return ;
        }

        IMsgBufferBucket* msg_bucket = NULL;
        HashKey* hash_item = m_hash_bucket_->HashGetFirst();
        while (hash_item)
        {
            m_hash_bucket_->HashRemove(any_cast<IMsgBufferBucket>(hash_item));
            msg_bucket = any_cast<IMsgBufferBucket>(hash_item);
            if (msg_bucket != NULL)
            {
                safe_delete(msg_bucket);
            }
            hash_item = m_hash_bucket_->HashGetFirst();
        }

        safe_delete(m_hash_bucket_);
    }
    
    explicit IMsgBufferPool(int max_free = 300) : m_max_free_(max_free)
    {
        m_hash_bucket_ = new HashList<IMsgBufferBucket>(MT_BUFFER_BUCKET_SIZE);
    }

private:
    int m_max_free_;
    HashList<IMsgBufferBucket> *m_hash_bucket_;
};

MTHREAD_NAMESPACE_END

#endif
