/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_buffer.h"

MTHREAD_NAMESPACE_USING
using namespace std;

IMsgBufferPool::IMsgBufferPool(int max_free) : m_max_free_(max_free)
{ 
    m_hash_map_ = new HashList<IMsgBufferMap>(MT_BUFFER_MAP_SIZE);
}

IMsgBufferPool::~IMsgBufferPool()
{
    if (!m_hash_map_)
    {
        return ;
    }

    IMsgBufferMap* msg_map = NULL;
    HashKey* hash_item = m_hash_map_->HashGetFirst();
    while (hash_item)
    {
        m_hash_map_->HashRemove(any_cast<IMsgBufferMap>(hash_item));
        msg_map = any_cast<IMsgBufferMap>(hash_item);
        if (msg_map != NULL)
        {
            safe_delete(msg_map);
        }
        hash_item = m_hash_map_->HashGetFirst();
    }

    safe_delete(m_hash_map_);
}

IMtMsgBuffer* IMsgBufferPool::GetMsgBuffer(int max_size)
{
    if (!m_hash_map_)
    {
        LOG_ERROR("IMsgBufferPool not init, hash %p", m_hash_map_);
        return NULL;
    }

    // 重新字节对齐处理
    max_size = MT_ALGIN(max_size);

    IMsgBufferMap* msg_map = NULL;
    IMsgBufferMap msg_key(max_size);
    HashKey* hash_item = m_hash_map_->HashFind(&msg_key);

    if (hash_item)
    {
        msg_map = any_cast<IMsgBufferMap>(hash_item->GetDataPtr());
        if (msg_map)
        {
            return msg_map->GetMsgBuffer();
        }
        else
        {
            LOG_ERROR("Hash item: %p, msg_map: %p impossible, clean it", hash_item, msg_map);
            m_hash_map_->HashRemove(any_cast<IMsgBufferMap>(hash_item));
            safe_delete(hash_item);
            return NULL;
        }
    }
    else
    {
        msg_map = new IMsgBufferMap(max_size, m_max_free_);
        if (!msg_map)
        {
            LOG_ERROR("maybe no more memory failed. size: %d", max_size);
            return NULL;
        }
        m_hash_map_->HashInsert(msg_map);
        return msg_map->GetMsgBuffer();
    }
}

// 释放msg的buffer
void IMsgBufferPool::FreeMsgBuffer(IMtMsgBuffer* msg_buf)
{
    if (!m_hash_map_ || !msg_buf)
    {
        LOG_ERROR("IMsgBufferPool not init or input error! hash %p, msg_buf %p", m_hash_map_, msg_buf);
        safe_delete(msg_buf);
        return ;
    }

    msg_buf->Reset();
    IMsgBufferMap* msg_map = NULL;
    IMsgBufferMap msg_key(msg_buf->GetMaxLen());
    HashKey* hash_item = m_hash_map_->HashFind(&msg_key);
    
    if (hash_item)
    {
        msg_map = any_cast<IMsgBufferMap>(hash_item->GetDataPtr());
    }

    if (!hash_item || !msg_map)
    {
        LOG_ERROR("IMsgBufferPool find no queue, maybe error: %d", msg_buf->GetMaxLen());
        safe_delete(msg_buf);
        return ;
    }

    msg_map->FreeMsgBuffer(msg_buf);
    return ;
}