/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_HEAP_H_
#define _ST_HEAP_H_

#include "st_util.h"

namespace stlib {

/* 最小堆节点：HeapValue 越小越靠前。GetIndex/SetIndex 由堆维护，0
 * 表示不在堆中。 */
class StHeap : public referenceable {
public:
  StHeap() : m_index_(0) {}

  virtual ~StHeap() {}

  virtual int64_t HeapValue() = 0;

  virtual void HeapIterate() { return; }

  inline int32_t GetIndex() { return m_index_; }

  inline void SetIndex(int32_t index) { m_index_ = index; }

  inline int32_t HeapValueCmp(StHeap *rhs) {
    if (this->HeapValue() == rhs->HeapValue()) {
      return 0;
    } else if (this->HeapValue() > rhs->HeapValue()) {
      return 1;
    } else {
      return -1;
    }
  }

private:
  int32_t m_index_;
};

typedef StHeap *StHeapNode;

/* 1-based 最小堆：Push/Pop/Delete 均为 O(log n) sift（C1）。 */
template <class T = StHeapNode> class StHeapList {
public:
  typedef T value_type;
  typedef value_type *pointer;
  typedef pointer *pointer_pointer;

  explicit StHeapList(int32_t max = 1024) {
    m_max_ = ST_MAX(max, 512);
    m_list_ = (pointer_pointer)calloc(m_max_ + 1, sizeof(pointer));
    memset(m_list_, 0, (m_max_ + 1) * sizeof(pointer));
    m_count_ = 0;
  }

  virtual ~StHeapList() {
    st_safe_free(m_list_);
    m_max_ = 0;
    m_count_ = 0;
  }

  inline int32_t HeapResize(int32_t size) {
    if (m_max_ >= size) {
      return 0;
    }

    LOG_TRACE("size: %d, m_list_: %p", size, m_list_);

    pointer_pointer vptr =
        (pointer_pointer)realloc(m_list_, sizeof(pointer) * (size + 1));
    if (NULL == vptr) {
      return -1;
    }

    m_list_ = vptr;
    m_max_ = size;

    return 0;
  }

  inline int32_t HeapPush(pointer entry) {
    if (this->HeapFull()) {
      return -1;
    }

    if (entry->GetIndex() != 0) {
      return -2;
    }

    m_list_[++m_count_] = entry;
    entry->SetIndex(m_count_);
    this->SiftUp(m_count_);

    return 0;
  }

  inline pointer HeapPop() {
    if (this->HeapEmpty()) {
      return NULL;
    }

    pointer top = m_list_[1];
    this->Swap(1, m_count_);
    m_list_[m_count_] = NULL;
    m_count_--;
    top->SetIndex(0);
    if (m_count_ > 0) {
      this->SiftDown(1);
    }

    return top;
  }

  inline int32_t HeapDelete(pointer entry) {
    if (this->HeapEmpty()) {
      return -1;
    }

    int32_t pos = entry->GetIndex();
    if ((pos > m_count_) || (pos <= 0)) {
      return -2;
    }

    pointer ptr = m_list_[pos];
    if (pos == m_count_) {
      m_list_[pos] = NULL;
      m_count_--;
      ptr->SetIndex(0);
      return 0;
    }

    m_list_[pos] = m_list_[m_count_];
    m_list_[pos]->SetIndex(pos);
    m_list_[m_count_] = NULL;
    m_count_--;
    ptr->SetIndex(0);

    if (pos > 1 && m_list_[pos]->HeapValueCmp(m_list_[pos / 2]) < 0) {
      this->SiftUp(pos);
    } else {
      this->SiftDown(pos);
    }

    return 0;
  }

  void HeapForeach() {
    for (int32_t i = 1; i <= m_count_; i++) {
      m_list_[i]->HeapIterate();
    }
  }

  inline int32_t HeapSize() { return m_count_; }

  inline pointer HeapTop() { return (m_count_ > 0) ? m_list_[1] : NULL; }

  inline bool HeapFull() { return (m_count_ >= m_max_); }

  inline bool HeapEmpty() { return (m_count_ == 0); }

private:
  void SiftUp(int32_t pos) {
    while (pos > 1) {
      int32_t parent = pos / 2;
      if (m_list_[pos]->HeapValueCmp(m_list_[parent]) >= 0) {
        break;
      }
      this->Swap(pos, parent);
      pos = parent;
    }
  }

  void SiftDown(int32_t pos) {
    for (;;) {
      int32_t lchild = 2 * pos;
      int32_t rchild = 2 * pos + 1;
      int32_t best = pos;

      if (lchild <= m_count_ &&
          m_list_[lchild]->HeapValueCmp(m_list_[best]) < 0) {
        best = lchild;
      }
      if (rchild <= m_count_ &&
          m_list_[rchild]->HeapValueCmp(m_list_[best]) < 0) {
        best = rchild;
      }
      if (best == pos) {
        break;
      }
      this->Swap(pos, best);
      pos = best;
    }
  }

  inline void Swap(int32_t i, int32_t j) {
    pointer tmp = m_list_[i];
    m_list_[i] = m_list_[j];
    m_list_[j] = tmp;
    m_list_[i]->SetIndex(i);
    m_list_[j]->SetIndex(j);
  }

private:
  pointer_pointer m_list_;
  int32_t m_max_;
  int32_t m_count_;
};

} // namespace stlib

#endif
