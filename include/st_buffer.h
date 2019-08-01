/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_BUFFER_H_INCLUDED_
#define _ST_BUFFER_H_INCLUDED_

#include "st_util.h"
#include "st_hash_list.h"

#define ST_BUFFER_BUCKET_SIZE  1024

ST_NAMESPACE_BEGIN

class StMsgBuffer;

typedef CPP_TAILQ_ENTRY<StMsgBuffer>  StMsgBufferNext;
typedef CPP_TAILQ_HEAD<StMsgBuffer>   StMsgBufferQueue;

class StMsgBuffer
{
public:
    StMsgBuffer(int32_t max_len) : 
        m_max_len_(max_len), 
        m_msg_len_(0), 
        m_buf_type_(eBUFF_UNDEF), 
        m_recv_len_(0), 
        m_send_len_(0)
    {
        m_msg_buff_ = malloc(max_len);
    }

    ~StMsgBuffer()
    {
        st_safe_free(m_msg_buff_);
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

    inline void SetMsgLen(int32_t msg_len)
    {
        m_msg_len_ = msg_len;
    }

    inline int32_t GetMsgLen()
    {
        return m_msg_len_;
    }

    inline int32_t GetMaxLen()
    {
        return m_max_len_;
    }

    inline void* GetMsgBuffer()
    {
        return m_msg_buff_;
    }

    inline int32_t GetHaveSendLen()
    {
        return m_send_len_;
    }

    inline void SetHaveSendLen(int32_t send_len)
    {
        m_send_len_ = send_len;
    }

    inline int32_t GetHaveRecvLen()
    {
        return m_recv_len_;
    }

    inline void SetHaveRecvLen(int32_t recv_len)
    {
        m_recv_len_ = recv_len;
    }
    
private:
    int32_t     m_max_len_, m_msg_len_;
    void*       m_msg_buff_;

    eBuffType   m_buf_type_;
    int32_t     m_recv_len_;
    int32_t     m_send_len_;
    
public:
    StMsgBufferNext m_next_;
};

class StMsgBufferBucket : public HashKey
{
public:
    StMsgBufferBucket(int32_t buff_size, int32_t max_free = 1024) : 
        m_max_buf_size_(buff_size), 
        m_max_free_(max_free), 
        m_queue_num_(0)
    {
        SetDataPtr(this);
        // 兼容内核的TAILQ_INIT
        CPP_TAILQ_INIT(&m_msg_queue_);
    }
    
    ~StMsgBufferBucket()
    {
        // 定义两个指针变量
        StMsgBuffer* ptr = NULL;
        StMsgBuffer* temp = NULL;

        CPP_TAILQ_FOREACH_SAFE(ptr, &m_msg_queue_, m_next_, temp)
        {
            CPP_TAILQ_REMOVE(&m_msg_queue_, ptr, m_next_); // 删除对应的数据
            st_safe_delete(ptr);
            m_queue_num_--;
        }

        // 重新初始化
        CPP_TAILQ_INIT(&m_msg_queue_);
    }

    StMsgBuffer* GetMsgBuffer()
    {
        StMsgBuffer* ptr = NULL;

        if (!CPP_TAILQ_EMPTY(&m_msg_queue_))
        {
            ptr = CPP_TAILQ_FIRST(&m_msg_queue_);
            CPP_TAILQ_REMOVE(&m_msg_queue_, ptr, m_next_);
            m_queue_num_--;
        }
        else
        {
            ptr = new StMsgBuffer(m_max_buf_size_);
        }

        return ptr;
    }

    void FreeMsgBuffer(StMsgBuffer* ptr)
    {
        if (m_queue_num_ >= m_max_free_)
        {
            st_safe_delete(ptr);
        }
        else
        {
            ptr->Reset();
            CPP_TAILQ_INSERT_TAIL(&m_msg_queue_, ptr, m_next_);
            m_queue_num_++;
        }
    }

    virtual uint32_t HashValue()
    {
        return m_max_buf_size_;
    }

    virtual int32_t HashCmp(HashKey* rhs)
    {
        return m_max_buf_size_ - (int32_t)rhs->HashValue();
    }

private:
    int32_t m_max_free_;
    int32_t m_max_buf_size_;
    int32_t m_queue_num_;
    StMsgBufferQueue m_msg_queue_;
};

class StMsgBufferPool
{
public:
    void SetMaxFreeNum(int32_t max_free)
    {
        m_max_free_ = max_free;
    }

    StMsgBuffer* GetMsgBuffer(int32_t max_size)
    {
        if (!m_hash_bucket_)
        {
            LOG_ERROR("pool isn't init, hash %p", m_hash_bucket_);
            return NULL;
        }

        // 重新字节对齐处理
        max_size = ST_ALGIN(max_size);

        StMsgBufferBucket* msg_bucket = NULL;
        StMsgBufferBucket msg_key(max_size);
        HashKey* hash_item = m_hash_bucket_->HashFind(&msg_key);

        if (hash_item)
        {
            msg_bucket = any_cast<StMsgBufferBucket>(hash_item->GetDataPtr());
            if (msg_bucket)
            {
                return msg_bucket->GetMsgBuffer();
            }
            else
            {
                m_hash_bucket_->HashRemove(any_cast<StMsgBufferBucket>(hash_item));
                st_safe_delete(hash_item);

                LOG_ERROR("hash item: %p, msg_bucket: %p impossible, clean it", 
                    hash_item, msg_bucket);
                return NULL;
            }
        }
        else
        {
            msg_bucket = new StMsgBufferBucket(max_size, m_max_free_);
            if (!msg_bucket)
            {
                LOG_ERROR("maybe no more memory failed. size: %d", max_size);
                return NULL;
            }

            m_hash_bucket_->HashInsert(msg_bucket);
            return msg_bucket->GetMsgBuffer();
        }
    }

    void FreeMsgBuffer(StMsgBuffer* msg_buf)
    {
        if (!m_hash_bucket_ || !msg_buf)
        {
            st_safe_delete(msg_buf);
            LOG_ERROR("pool isn't init or input error! hash %p, msg_buf: %p", 
                m_hash_bucket_, msg_buf);
            return ;
        }

        msg_buf->Reset();
        StMsgBufferBucket* msg_bucket = NULL;
        StMsgBufferBucket msg_key(msg_buf->GetMaxLen());
        HashKey* hash_item = m_hash_bucket_->HashFind(&msg_key);
        
        if (hash_item)
        {
            msg_bucket = any_cast<StMsgBufferBucket>(hash_item->GetDataPtr());
        }

        if (!hash_item || !msg_bucket)
        {
            LOG_ERROR("pool find no queue, maybe error: %d", msg_buf->GetMaxLen());
            st_safe_delete(msg_buf);
        }
        else
        {
            msg_bucket->FreeMsgBuffer(msg_buf);
        }
    }

    ~StMsgBufferPool()
    {
        if (!m_hash_bucket_)
        {
            return ;
        }

        StMsgBufferBucket* msg_bucket = NULL;
        HashKey* hash_item = m_hash_bucket_->HashGetFirst();
        while (hash_item)
        {
            m_hash_bucket_->HashRemove(any_cast<StMsgBufferBucket>(hash_item));
            msg_bucket = any_cast<StMsgBufferBucket>(hash_item);
            if (msg_bucket != NULL)
            {
                st_safe_delete(msg_bucket);
            }
            hash_item = m_hash_bucket_->HashGetFirst();
        }

        st_safe_delete(m_hash_bucket_);
    }
    
    explicit StMsgBufferPool(int32_t max_free = 1024) : m_max_free_(max_free)
    {
        m_hash_bucket_ = new HashList<StMsgBufferBucket>(ST_BUFFER_BUCKET_SIZE);
    }

private:
    int32_t m_max_free_;
    HashList<StMsgBufferBucket> *m_hash_bucket_;
};

ST_NAMESPACE_END

#endif
