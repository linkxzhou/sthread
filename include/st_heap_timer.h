/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_HEAP_TIMER_H_INCLUDED_
#define _ST_HEAP_TIMER_H_INCLUDED_

#include "st_util.h"
#include "st_heap.h"

ST_NAMESPACE_BEGIN

class HeapTimer;
class TimerEntry;

class TimerEntry : public HeapEntry
{
public:
    TimerEntry() : 
        m_time_expired_(-1)
    { }

    virtual ~TimerEntry()
    { 
        m_time_expired_ = -1;
    }

    virtual void TimerNotify(eEventType type) 
    { 
        return ;
    }

    virtual int64_t HeapValue()
    {
        return m_time_expired_;
    }

    inline void SetExpiredTime(int64_t expired)
    {
        m_time_expired_ = expired;
    }

    inline uint64_t GetExpiredTime()
    {
        return m_time_expired_;
    }

private:
    uint64_t m_time_expired_;
};

// 时间控制器
class HeapTimer
{
public:
    explicit HeapTimer(uint32_t max_item = 1024)
    {
        m_heap_ = new HeapList<TimerEntry>(max_item);
    }
    
    ~HeapTimer()
    {
        st_safe_delete(m_heap_);
    }

    bool Start(TimerEntry *timerable, int32_t interval)
    {
        if (!m_heap_ || !timerable)
        {
            return false;
        }

        int64_t now_ms = Util::SysMs();

        LOG_TRACE("now_ms + interval = %llu", now_ms + interval);
        
        timerable->SetExpiredTime(now_ms + interval);
        int32_t ret = m_heap_->HeapPush(timerable);
        if (ret < 0)
        {
            LOG_ERROR("HeapPush ret = %d", ret);
            return false;
        }

        if (ST_DEBUG)
        {
            m_heap_->HeapForeach();
        } 

        return true;
    }

    void Stop(TimerEntry* timerable)
    {
        if (!m_heap_ || !timerable)
        {
            return;
        }

        m_heap_->HeapDelete(timerable);
        return;
    }

    // 检查是否过期
    int32_t CheckExpired()
    {
        if (!m_heap_)
        {
            return 0;
        }

        int64_t now = Util::SysMs();

        LOG_TRACE("before: now_ms = %llu, size = %d", now, m_heap_->HeapSize());

        int32_t count = 0;
        TimerEntry *timer = any_cast<TimerEntry>(m_heap_->HeapTop());
        while (timer && (timer->GetExpiredTime() <= now))
        {
            m_heap_->HeapDelete(timer);          // 删除对应的过期时间
            timer->TimerNotify(eEVENT_TIMEOUT);  // 传递超时事件
            timer = any_cast<TimerEntry>(m_heap_->HeapTop());
            count++;
        }

        LOG_TRACE("after: size = %d", m_heap_->HeapSize());
        return count;
    }

private:
    HeapList<TimerEntry> *m_heap_;
};

ST_NAMESPACE_END

#endif