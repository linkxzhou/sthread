/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_HEAP_TIMER_H_
#define _ST_HEAP_TIMER_H_

#include "st_heap.h"
#include "st_util.h"

namespace stlib {

class StHeapTimer;

class StTimer : public StHeap {
public:
  StTimer() : m_time_expired_(-1) {}

  virtual ~StTimer() { m_time_expired_ = -1; }

  virtual void Timeout() { return; }

  virtual int64_t HeapValue() { return m_time_expired_; }

  inline void SetExpiredTime(int64_t expired) { m_time_expired_ = expired; }

  inline uint64_t GetExpiredTime() { return m_time_expired_; }

  inline bool IsExpired() {
    if (m_time_expired_ < Util::TimeMs()) {
      return true;
    }
    return false;
  }

private:
  uint64_t m_time_expired_;
};

// 时间控制器
class StHeapTimer {
public:
  explicit StHeapTimer(uint32_t max_item = 1024) {
    m_heap_ = new StHeapList<StTimer>(max_item);
  }

  ~StHeapTimer() { st_safe_delete(m_heap_); }

  bool Startup(StTimer *timerable, int32_t interval) {
    if (!m_heap_ || !timerable) {
      return false;
    }

    int64_t now_ms = Util::TimeMs();
    LOG_TRACE("now_ms + interval = %llu", now_ms + interval);
    timerable->SetExpiredTime(now_ms + interval);

    int32_t ret = m_heap_->HeapPush(timerable);
    if (ret < 0) {
      LOG_ERROR("HeapPush ret = %d", ret);
      return false;
    }

    if (ST_DEBUG) {
      m_heap_->HeapForeach();
    }

    return true;
  }

  void Stop(StTimer *timerable) {
    if (!m_heap_ || !timerable) {
      return;
    }

    m_heap_->HeapDelete(timerable);
    return;
  }

  // 检查是否过期
  int32_t CheckExpired() {
    if (!m_heap_) {
      return 0;
    }
    int64_t now = Util::TimeMs();
    LOG_TRACE("[1]now: now_ms = %llu, size = %d", now, m_heap_->HeapSize());

    int32_t count = 0;
    StTimer *timer = any_cast<StTimer>(m_heap_->HeapTop());
    while (timer && (timer->GetExpiredTime() <= now)) {
      m_heap_->HeapDelete(timer); // 删除对应的过期时间
      timer->Timeout();           // 传递超时事件
      timer = any_cast<StTimer>(m_heap_->HeapTop());
      count++;
    }
    LOG_TRACE("[2]now: size = %d", m_heap_->HeapSize());
    return count;
  }

private:
  StHeapList<StTimer> *m_heap_;
};

} // namespace stlib

#endif