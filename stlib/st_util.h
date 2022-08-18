/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_UTIL_H_
#define _ST_UTIL_H_

#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <typeinfo>
#include <unistd.h>

#include "st_closure.h"
#include "st_log.h"
#include "st_singleton.h"
#include "st_tailq.h"
#include "ucontext/st_def.h"

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
    static pthread_mutex_t mutex;

    uint64_t rid = id;
    pthread_mutex_lock(&mutex);
    if (unlikely(id >= 0xFFFFFFFF)) {
      id = 1;
    }

    rid = ++id;
    pthread_mutex_unlock(&mutex);
    return rid;
  }
};

// 对象池
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

  inline void incrref() { ++m_ref_count_; }

  inline void decref() { --m_ref_count_; }

  inline uint32_t getref() { return m_ref_count_; }

  virtual void Reset() { m_ref_count_ = 0; }

public:
  uint32_t m_ref_count_;
};

class Any {
public:
  Any() : m_content_(NULL) {}

  ~Any() { st_safe_delete(m_content_); }

  template <typename ValueType>
  explicit Any(const ValueType &value)
      : m_content_(new Holder<ValueType>(value)) {}

  Any(const Any &rhs)
      : m_content_(rhs.m_content_ ? rhs.m_content_->Clone() : NULL) {}

public:
  Any &swap(Any &rhs) {
    std::swap(m_content_, rhs.m_content_);
    return *this;
  }

  template <typename ValueType> Any &operator=(const ValueType &rhs) {
    Any(rhs).swap(*this);
    return *this;
  }

  Any &operator=(const Any &rhs) {
    Any(rhs).swap(*this);
    return *this;
  }

  bool IsEmpty() const { return !m_content_; }

  const std::type_info &GetType() const {
    return m_content_ ? m_content_->GetType() : typeid(void);
  }

  template <typename ValueType> ValueType operator()() const {
    if (GetType() == typeid(ValueType)) {
      return static_cast<Any::Holder<ValueType> *>(m_content_)->m_held_;
    } else {
      return ValueType();
    }
  }

protected:
  class PlaceHolder {
  public:
    virtual ~PlaceHolder() {}

  public:
    virtual const std::type_info &GetType() const = 0;
    virtual PlaceHolder *Clone() const = 0;
  };

  template <typename ValueType> class Holder : public PlaceHolder {
  public:
    Holder(const ValueType &value) : m_held_(value) {}

    virtual const std::type_info &GetType() const { return typeid(ValueType); }

    virtual PlaceHolder *Clone() const { return new Holder(m_held_); }

  public:
    ValueType m_held_;
  };

protected:
  PlaceHolder *m_content_;
  template <typename ValueType> friend ValueType *any_cast(Any *);
};

template <typename ValueType> ValueType *any_cast(Any *any) {
  if (any && any->GetType() == typeid(ValueType)) {
    return &(static_cast<Any::Holder<ValueType> *>(any->m_content_)->m_held_);
  }
  // 如果any为NULL或者其他，则用原始的static_cast方法
  return static_cast<ValueType *>(any);
}

template <typename ValueType> ValueType *any_cast(void *any) {
  return static_cast<ValueType *>(any);
}

template <typename ValueType> const ValueType *any_cast(const Any *any) {
  return any_cast<ValueType>(const_cast<Any *>(any));
}

template <typename ValueType> ValueType any_cast(const Any &any) {
  const ValueType *result = any_cast<ValueType>(&any);
  if (!result) {
    return ValueType();
  }
  return *result;
}

} // namespace stlib

#endif // _ST_UTIL_H_