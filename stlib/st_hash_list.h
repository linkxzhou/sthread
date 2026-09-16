/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_HASH_LIST_H_
#define _ST_HASH_LIST_H_

#include "st_netaddr.h"
#include "st_util.h"

namespace stlib {

template <class T> class StHashList;

class StHashKey : public referenceable {
  template <typename T> friend class StHashList;

public:
  StHashKey() : m_next_ptr_(NULL), m_hash_value_(0), m_data_ptr_(NULL) {}

  virtual ~StHashKey() {
    m_next_ptr_ = NULL;
    m_hash_value_ = 0;
  }

  virtual uint32_t HashValue() { return m_hash_value_; }

  virtual int32_t HashCmp(StHashKey *rhs) { return 0; }

  virtual void HashIterate() { return; }

  inline void *GetDataPtr() { return m_data_ptr_; }

  inline void SetDataPtr(void *data) { m_data_ptr_ = data; }

private:
  StHashKey *m_next_ptr_;
  uint32_t m_hash_value_;
  void *m_data_ptr_;
};

// StNetAddrKey根据四元组决定是否相等
class StNetAddrKey : public StHashKey {
public:
  StNetAddrKey() {}

  inline void SetDestAddr(const StNetAddr &dest) { m_destaddr_ = dest; }

  inline void SetSrcAddr(const StNetAddr &src) { m_srcaddr_ = src; }

  virtual uint32_t HashValue() {
    return (m_srcaddr_.Port() << 16) | m_destaddr_.Port();
  }

  virtual int32_t HashCmp(StHashKey *rhs) {
    StNetAddrKey *data = dynamic_cast<StNetAddrKey *>(rhs);
    if (!data) {
      return -1;
    }
    if (!(m_srcaddr_ == data->m_srcaddr_)) {
      return -2;
    }
    if (!(m_destaddr_ == data->m_destaddr_)) {
      return -3;
    }
    return 0;
  }

private:
  StNetAddr m_srcaddr_, m_destaddr_;
};

/* 开链哈希：桶内直接挂首元素，无哑元头（C6/B8）。
 * HashRemove 只摘除并返回节点，所有权归调用方（B10）。 */
template <class T = StHashKey> class StHashList : public referenceable {
public:
  typedef T value_type;
  typedef value_type *pointer;
  typedef pointer *pointer_pointer;

  explicit StHashList(int32_t max = 8192) {
    m_max_ = Util::MaxPrimeNum((max > 2) ? max : 1024);
    m_buckets_ = (pointer *)calloc(m_max_, sizeof(pointer));
    m_count_ = 0;
  }

  virtual ~StHashList() {
    st_safe_free(m_buckets_);
    m_count_ = 0;
  }

  inline int32_t HashSize() { return m_count_; }

  int32_t HashInsert(pointer key) {
    if (!key || !m_buckets_) {
      return ST_ERROR;
    }

    if ((key->m_hash_value_ != 0) || (key->m_next_ptr_ != NULL)) {
      return ST_ERROR;
    }

    key->m_hash_value_ = key->HashValue();
    int32_t idx = (key->m_hash_value_) % m_max_;
    key->m_next_ptr_ = m_buckets_[idx];
    m_buckets_[idx] = key;
    m_count_++;
    if (ST_DEBUG) {
      for (pointer item = m_buckets_[idx]; item != NULL;
           item = any_cast<value_type>(item->m_next_ptr_)) {
        LOG_TRACE("item : %p", item);
      }
    }
    return 0;
  }

  pointer HashFind(pointer key) {
    if (!key || !m_buckets_) {
      return NULL;
    }

    uint32_t hash = key->HashValue();
    int32_t idx = hash % m_max_;
    pointer item = m_buckets_[idx];
    for (; item != NULL; item = any_cast<value_type>(item->m_next_ptr_)) {
      if (item->m_hash_value_ != hash) {
        continue;
      }
      if (item->HashCmp(key) == 0) {
        break;
      }
    }

    return item;
  }

  void *HashFindData(pointer key) {
    pointer item = HashFind(key);
    if (!item) {
      return NULL;
    }

    return item->m_data_ptr_;
  }

  /* 摘除匹配节点并返回；调用方负责 delete。键只用于查找。 */
  pointer HashRemove(pointer key) {
    if (!key || !m_buckets_) {
      return NULL;
    }

    uint32_t hash = key->HashValue();
    int32_t idx = hash % m_max_;
    pointer prev = NULL;
    pointer item = m_buckets_[idx];
    while (item != NULL) {
      if ((item->m_hash_value_ == hash) && (item->HashCmp(key) == 0)) {
        if (prev != NULL) {
          prev->m_next_ptr_ = item->m_next_ptr_;
        } else {
          m_buckets_[idx] = any_cast<value_type>(item->m_next_ptr_);
        }
        item->m_next_ptr_ = NULL;
        item->m_hash_value_ = 0;
        m_count_--;
        return item;
      }
      prev = item;
      item = any_cast<value_type>(item->m_next_ptr_);
    }
    return NULL;
  }

  void HashForeach() {
    if (!m_buckets_) {
      return;
    }

    for (int32_t i = 0; i < m_max_; i++) {
      pointer item = m_buckets_[i];
      for (; item != NULL; item = any_cast<value_type>(item->m_next_ptr_)) {
        item->HashIterate();
      }
    }
  }

  pointer HashGetFirst() {
    if (!m_buckets_) {
      return NULL;
    }

    for (int32_t i = 0; i < m_max_; i++) {
      if (m_buckets_[i] != NULL) {
        return m_buckets_[i];
      }
    }

    return NULL;
  }

private:
  pointer_pointer m_buckets_;
  int32_t m_count_, m_max_;
};

} // namespace stlib

#endif
