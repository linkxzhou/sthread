/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_HASH_LIST_H_INCLUDED_
#define _ST_HASH_LIST_H_INCLUDED_

#include "st_util.h"

ST_NAMESPACE_BEGIN

template <class T> class HashList;

class HashKey : public Any
{
    template <typename T> friend class HashList;

private:
    HashKey*  m_next_entry_;
    uint32_t  m_hash_value_;
    void*     m_data_ptr_;

public:
    HashKey() : 
        m_next_entry_(NULL), 
        m_hash_value_(0), 
        m_data_ptr_(NULL) 
    { }

    virtual ~HashKey()
    { 
        m_next_entry_ = NULL;
        m_hash_value_ = 0;
    }

    virtual uint32_t HashValue()
    { 
        return m_hash_value_;
    }

    virtual int32_t HashCmp(HashKey* rhs)
    {
        return 0;
    }
    
    virtual void HashIterate()
    {
        return ;
    }

    void* GetDataPtr()
    {
        return m_data_ptr_;
    }

    void SetDataPtr(void* data)
    {
        m_data_ptr_ = data;
    }
};

// 实现hashlist
template <class T = HashKey>
class HashList
{
public:
    typedef T value_type;  
    typedef value_type* pointer;
    typedef pointer* pointer_pointer;

    explicit HashList(int32_t max = 4096)
    {
        m_max_ = Util::MaxPrimeNum((max > 2) ? max : 4096);
        m_buckets_ = (pointer*)calloc(m_max_, sizeof(pointer));
        m_count_ = 0;
    }

    virtual ~HashList()
    {
        if (m_buckets_)
        {
            st_safe_free(m_buckets_);
        }
        
        m_count_ = 0;
    }

    inline int32_t HashSize()
    {
        return m_count_;
    }

    int32_t HashInsert(pointer key)
    {
        if (!key || !m_buckets_)
        {
            return ST_ERROR;
        }

        if ((key->m_hash_value_ != 0) || (key->m_next_entry_ != NULL))
        {
            return ST_ERROR;
        }

        key->m_hash_value_ = key->HashValue();
        int32_t idx = (key->m_hash_value_) % m_max_;
        
        // 如果为空，初始化m_buckets_
        if (NULL == m_buckets_[idx])
        {
            m_buckets_[idx] = any_cast<value_type>(malloc(sizeof(value_type)));
            m_buckets_[idx]->m_next_entry_ = NULL;
            m_buckets_[idx]->m_hash_value_ = 0;
        }

        pointer next_item = any_cast<value_type>(m_buckets_[idx]->m_next_entry_);
        LOG_TRACE("m_buckets_ : %p, next_item : %p, idx : %d", m_buckets_, next_item, idx);

        // 第一个元素不存储任何对象，只存储当前开链列表中的list的个数到hash_value_中
        m_buckets_[idx]->m_next_entry_ = key;
        key->m_next_entry_ = next_item;
        (m_buckets_[idx]->m_hash_value_)++;
        m_count_++;

        if (ST_DEBUG)
        {
            for (pointer item = any_cast<value_type>(m_buckets_[idx]->m_next_entry_); 
                item != NULL; item = any_cast<value_type>(item->m_next_entry_))
            {
                LOG_TRACE("item : %p", item);
            }

            LOG_TRACE("item : %p, key : %p", any_cast<value_type>(m_buckets_[idx]->m_next_entry_), key);
        }

        return 0;
    }

    pointer HashFind(pointer key)
    {
        if (!key || !m_buckets_)
        {
            return NULL;
        }

        uint32_t hash = key->HashValue();
        int32_t idx = hash % m_max_;
        if (NULL == m_buckets_[idx])
        {
            return NULL;
        }

        pointer item = any_cast<value_type>(m_buckets_[idx]->m_next_entry_);

        for (; item != NULL; item = any_cast<value_type>(item->m_next_entry_))
        {
            if (item->m_hash_value_ != hash)
            {
                continue;
            }

            if (item->HashCmp(key) == 0)
            {
                break;
            }
        }
        
        return item;
    }

    void* HashFindData(pointer key)
    {
        pointer item = HashFind(key);
        if (!item)
        {
            return NULL;
        }
        else
        {
            return item->m_data_ptr_;
        }
    }

    void HashRemove(pointer key)
    {
        if (!key || !m_buckets_)
        {
            return ;
        }

        uint32_t hash = key->HashValue();
        int32_t idx = hash % m_max_;
        if (NULL == m_buckets_[idx])
        {
            return ;
        }
        pointer prev = m_buckets_[idx];
        pointer item = any_cast<value_type>(prev->m_next_entry_);
        
        while (item != NULL)
        {
            if ((item->m_hash_value_ == hash) && (item->HashCmp(key) == 0))
            {
                prev->m_next_entry_ = item->m_next_entry_;
                pointer tmp = item;
                item = any_cast<value_type>(item->m_next_entry_);
                m_count_--;
                (m_buckets_[idx]->m_hash_value_)--;
                st_safe_delete(tmp); // 释放指针
            }
            else
            {
                prev = item;
                item = any_cast<value_type>(item->m_next_entry_);
            }
        }
    }

    void HashForeach()
    {
        if (!m_buckets_)
        {
            return;
        }

        for (int32_t i = 0; i < m_max_; i++)
        {
            pointer item = m_buckets_[i];
            for (; item != NULL; item = item->m_next_entry_)
            {
                item->HashIterate();
            }
        }
    }

    pointer HashGetFirst()
    {
        if (!m_buckets_)
        {
            return NULL;
        }

        for (int32_t i = 0; i < m_max_; i++)
        {
            if (m_buckets_[i])
            {
                return m_buckets_[i];
            }
        }

        return NULL;
    }

private:
    pointer_pointer m_buckets_;
    int32_t     m_count_, m_max_;
};

ST_NAMESPACE_END

#endif