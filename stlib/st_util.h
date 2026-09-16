/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_UTIL_H_
#define _ST_UTIL_H_

#include <arpa/inet.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <queue>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include "st_closure.h"
#include "st_def.h"
#include "st_log.h"
#include "st_singleton.h"
#include "st_tailq.h"

namespace stlib {

class Util {
public:
  // 获取系统时间
  inline static uint64_t TimeMs() {
    struct timeval tv;
    ::gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL);
  }

  inline static uint32_t Sleep(uint32_t seconds) { return ::sleep(seconds); }

  inline static void USleep(uint64_t u_seconds) { ::usleep(u_seconds); }

  // 获取最大的素数
  static uint32_t MaxPrimeNum(uint32_t num) {
    uint32_t sqrt_value = (uint32_t)sqrt(num);
    for (uint32_t i = ((num % 2 == 0) ? (num - 1) : num); i > 0; i -= 2) {
      uint32_t flag = 1;
      for (uint32_t k = 2; k <= sqrt_value; k++) {
        if (i % k == 0) {
          flag = 0;
          break;
        }
      }
      if (flag == 1) {
        return i;
      }
    }

    return 0;
  }

  // 解决多线程情况，生成唯一的uniqid
  static uint64_t GetUniqid() {
    static uint64_t id = 0;
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&mutex);
    if (unlikely(id >= 0xFFFFFFFFULL)) {
      id = 1;
    }
    uint64_t rid = ++id;
    pthread_mutex_unlock(&mutex);
    return rid;
  }
};

// 对象池
/* 用途：简单对象池；AllocPtr / FreePtr（或 UtilPtrPoolFree）配对。
 * 线程模型：通常配合 Instance<UtilPtrPool<T>> 做线程局部池。
 * 所有权：池持有空闲对象；调用方持有已分配对象直至 Free。 */
template <typename ValueType> class UtilPtrPool {
public:
  typedef typename std::queue<ValueType *> QueuePtr;
  typedef ValueType *ValueTypePtr;

public:
  explicit UtilPtrPool(uint32_t max = 256) : m_max_free_(max), m_total_(0) {}

  ~UtilPtrPool() {
    ValueType *ptr = NULL;
    while (!m_ptr_list_.empty()) {
      ptr = m_ptr_list_.front();
      m_ptr_list_.pop();
      st_safe_delete(ptr);
    }
  }

  uint32_t Size() { return m_total_; }

  ValueType *AllocPtr() {
    ValueType *ptr = NULL;
    if (!m_ptr_list_.empty()) {
      ptr = m_ptr_list_.front();
      m_ptr_list_.pop();
    } else {
      ptr = new ValueType;
      m_total_++;
    }
    return ptr;
  }

  void FreePtr(ValueType *ptr) {
    if (ptr != NULL) {
      if ((uint32_t)m_ptr_list_.size() >= m_max_free_) {
        st_safe_delete(ptr);
        m_total_--;
      } else {
        ptr->Reset();
        m_ptr_list_.push(ptr);
      }
    }
  }

protected:
  QueuePtr m_ptr_list_;
  uint32_t m_max_free_;
  uint32_t m_total_;
};

template <typename T> void UtilPtrPoolFree(T *ptr) {
  // clang-format off
  Instance< UtilPtrPool<T> >()->FreePtr(ptr);
  // clang-format on
}

class referenceable {
public:
  referenceable() : m_ref_count_(0) {}

  virtual ~referenceable() {}

  inline void incrref() { ++m_ref_count_; }

  inline void decref() { --m_ref_count_; }

  inline uint32_t getref() { return m_ref_count_; }

  virtual void Reset() { m_ref_count_ = 0; }

private:
  uint32_t m_ref_count_;
};

/* D3/C3：any_cast 退化为 static_cast 别名；Holder/Any 机制已移除。 */
template <typename ValueType> inline ValueType *any_cast(void *any) {
  return static_cast<ValueType *>(any);
}

template <typename ValueType>
inline const ValueType *any_cast(const void *any) {
  return static_cast<const ValueType *>(any);
}

} // namespace stlib

#endif // _ST_UTIL_H_
