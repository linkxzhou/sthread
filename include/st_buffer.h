/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_BUFFER_H_INCLUDED_
#define _ST_BUFFER_H_INCLUDED_

#include "st_util.h"
#include "st_hash_list.h"

#define ST_BUFFER_BUCKET_SIZE  128

ST_NAMESPACE_BEGIN

class StBuffer;

typedef CPP_TAILQ_ENTRY<StBuffer>  StBufferNext;
typedef CPP_TAILQ_HEAD<StBuffer>   StBufferQueue;

class StBuffer : public referenceable
{
public:
    StBuffer(uint32_t max_len) : 
        m_max_len_(max_len), 
        m_msg_len_(0), 
        m_buf_type_(eBUFF_UNDEF), 
        m_recv_len_(0), 
        m_send_len_(0)
    {
        m_msg_buf_ = malloc(max_len);
    }

    ~StBuffer()
    {
        st_safe_free(m_msg_buf_);
    }

    inline void SetBufferType(eBuffType type)
    {
        m_buf_type_ = type;
    }

    inline eBuffType GetBufferType()
    {
        return m_buf_type_;
    }

    virtual void Reset()
    {
        m_msg_len_  = 0;
        m_recv_len_ = 0;
        m_send_len_ = 0;
        m_buf_type_ = eBUFF_UNDEF;
        CPP_TAILQ_REMOVE_SELF(this, m_next_);
    }

    inline void SetMsgLen(uint32_t msg_len)
    {
        m_msg_len_ = msg_len;
    }

    inline uint32_t GetMsgLen()
    {
        return m_msg_len_;
    }

    inline uint32_t GetMaxLen()
    {
        return m_max_len_;
    }

    inline void* GetBuffer()
    {
        return m_msg_buf_;
    }

    inline int SetBuffer(void *buf, uint32_t len)
    {
        if (buf == NULL || len >= m_max_len_)
        {
            return -1;
        }

        ASSERT(m_msg_buf_ != NULL);
        memcpy(m_msg_buf_, buf, len);
        m_msg_len_ = len;

        return m_msg_len_;
    }

    inline uint32_t GetHaveSendLen()
    {
        return m_send_len_;
    }

    inline void SetHaveSendLen(uint32_t send_len)
    {
        m_send_len_ = send_len;
    }

    inline uint32_t GetHaveRecvLen()
    {
        return m_recv_len_;
    }

    inline void SetHaveRecvLen(uint32_t recv_len)
    {
        m_recv_len_ = recv_len;
    }
    
private:
    uint32_t    m_max_len_, m_msg_len_;
    void        *m_msg_buf_;

    eBuffType   m_buf_type_;
    uint32_t    m_recv_len_;
    uint32_t    m_send_len_;
    
public:
    StBufferNext    m_next_;
};

class StBufferBucket : public HashKey
{
public:
    StBufferBucket(uint32_t buff_size, uint32_t max_free = 512) : 
        m_max_buf_size_(buff_size), 
        m_max_free_(max_free), 
        m_queue_num_(0)
    {
        SetDataPtr(this);
        // 兼容内核的TAILQ_INIT
        CPP_TAILQ_INIT(&m_msg_queue_);
    }
    
    ~StBufferBucket()
    {
        // 定义两个指针变量
        StBuffer *ptr = NULL;
        StBuffer *temp = NULL;

        CPP_TAILQ_FOREACH_SAFE(ptr, &m_msg_queue_, m_next_, temp)
        {
            CPP_TAILQ_REMOVE(&m_msg_queue_, ptr, m_next_); // 删除对应的数据
            st_safe_delete(ptr);
            m_queue_num_--;
        }

        // 重新初始化
        CPP_TAILQ_INIT(&m_msg_queue_);
    }

    StBuffer* GetBuffer()
    {
        StBuffer *ptr = NULL;

        if (!CPP_TAILQ_EMPTY(&m_msg_queue_))
        {
            ptr = CPP_TAILQ_FIRST(&m_msg_queue_);
            CPP_TAILQ_REMOVE(&m_msg_queue_, ptr, m_next_);
            m_queue_num_--;
        }
        else
        {
            ptr = new StBuffer(m_max_buf_size_);
        }

        return ptr;
    }

    void FreeBuffer(StBuffer *ptr)
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

    virtual int32_t HashCmp(HashKey *rhs)
    {
        return m_max_buf_size_ - (int32_t)rhs->HashValue();
    }

private:
    uint32_t        m_max_free_, m_max_buf_size_, m_queue_num_;
    StBufferQueue   m_msg_queue_;
};

template<uint32_t MAX_FREE = 128>
class StBufferPool
{
public:
    explicit StBufferPool(uint32_t max_free = MAX_FREE) : 
        m_max_free_(max_free)
    {
        m_hash_bucket_ = new HashList<StBufferBucket>(ST_BUFFER_BUCKET_SIZE);
    }
    
    ~StBufferPool()
    {
        if (!m_hash_bucket_)
        {
            return ;
        }

        StBufferBucket *msg_bucket = NULL;
        HashKey *hash_item = m_hash_bucket_->HashGetFirst();
        while (hash_item)
        {
            m_hash_bucket_->HashRemove(any_cast<StBufferBucket>(hash_item));
            msg_bucket = any_cast<StBufferBucket>(hash_item);
            if (msg_bucket != NULL)
            {
                st_safe_delete(msg_bucket);
            }
            
            hash_item = m_hash_bucket_->HashGetFirst();
        }

        st_safe_delete(m_hash_bucket_);
    }

    void SetMaxFreeNum(uint32_t max_free)
    {
        m_max_free_ = max_free;
    }

    StBuffer* GetBuffer(uint32_t max_size)
    {
        if (!m_hash_bucket_)
        {
            LOG_ERROR("pool isn't init, hash: %p", m_hash_bucket_);
            return NULL;
        }

        // 重新字节对齐处理
        max_size = ST_ALGIN(max_size);

        StBufferBucket *msg_bucket = NULL;
        StBufferBucket msg_key(max_size);
        HashKey *hash_item = m_hash_bucket_->HashFind(&msg_key);

        if (hash_item)
        {
            msg_bucket = any_cast<StBufferBucket>(hash_item->GetDataPtr());
            if (msg_bucket)
            {
                return msg_bucket->GetBuffer();
            }
            else
            {
                m_hash_bucket_->HashRemove(any_cast<StBufferBucket>(hash_item));
                st_safe_delete(hash_item);

                LOG_ERROR("hash item: %p, msg_bucket: %p impossible, clean it", 
                    hash_item, msg_bucket);
                return NULL;
            }
        }
        else
        {
            msg_bucket = new StBufferBucket(max_size, m_max_free_);
            if (!msg_bucket)
            {
                LOG_ERROR("maybe no more memory failed. size: %d", 
                    max_size);
                return NULL;
            }

            m_hash_bucket_->HashInsert(msg_bucket);
            return msg_bucket->GetBuffer();
        }
    }

    void FreeBuffer(StBuffer *msg_buf)
    {
        if (!m_hash_bucket_ || !msg_buf)
        {
            st_safe_delete(msg_buf);
            LOG_ERROR("pool isn't init or input error! hash: %p, msg_buf: %p", 
                m_hash_bucket_, msg_buf);
            return ;
        }

        StBufferBucket *msg_bucket = NULL;
        StBufferBucket msg_key(msg_buf->GetMaxLen());
        HashKey *hash_item = m_hash_bucket_->HashFind(&msg_key);
        
        if (hash_item)
        {
            msg_bucket = any_cast<StBufferBucket>(hash_item->GetDataPtr());
        }

        if (!hash_item || !msg_bucket)
        {
            LOG_ERROR("pool find no queue, maybe error: %d", 
                msg_buf->GetMaxLen());
            st_safe_delete(msg_buf);
        }
        else
        {
            msg_bucket->FreeBuffer(msg_buf);
        }
    }

private:
    uint32_t                    m_max_free_;
    HashList<StBufferBucket>    *m_hash_bucket_;
};

ST_NAMESPACE_END

#endif
