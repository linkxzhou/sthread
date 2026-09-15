/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SINGLETON_H__
#define _ST_SINGLETON_H__

#include "st_def.h"
#include <assert.h>
#include <new>
#include <pthread.h>
#include <stdlib.h>

namespace stlib {

#define DECLARE_UNCOPYABLE(Class)                                              \
private:                                                                       \
  Class(const Class &);                                                        \
  Class &operator=(const Class &);

/* 用途：线程局部单例；Instance<T>() 是其入口。
 * 线程模型：每 OS 线程一份（pthread_key 析构）；这是「协程不可跨线程」的根源。
 * 所有权：进程内按线程创建；InstanceDestroy 可显式销毁当前线程实例。 */
template <class T> class Singleton {

protected:
  Singleton() {}

  ~Singleton() {}

public:
  static T *Instance() {
#if __cplusplus < 201103L
    if (NULL == m_pinstance_) {
      pthread_mutex_lock(&m_mutex_);

      if (NULL == m_pinstance_) {
        m_pinstance_ = new T();
        m_deleter_.Set(m_pinstance_);
      }

      pthread_mutex_unlock(&m_mutex_);
    }

    return m_pinstance_;
#else
    // Use C++ 11 style to implement singleton
    static __thread T t;
    return &t;
#endif
  }

  static void InstanceDestroy(void *ins = NULL) {
    if (m_pinstance_ != NULL) {
      st_safe_delete(m_pinstance_);
    }
  }

  DECLARE_UNCOPYABLE(Singleton);

  class Deleter {
  public:
    Deleter() { pthread_key_create(&m_pkey_, &Singleton<T>::InstanceDestroy); }

    ~Deleter() { pthread_key_delete(m_pkey_); }

    void Set(T *o) {
      assert(pthread_getspecific(m_pkey_) == NULL);
      pthread_setspecific(m_pkey_, o);
    }

  private:
    pthread_key_t m_pkey_;
  };

private:
  static __thread T *m_pinstance_;
  static pthread_mutex_t m_mutex_;
  static Deleter m_deleter_;
};

template <typename T> __thread T *Singleton<T>::m_pinstance_ = NULL;
template <typename T>
pthread_mutex_t Singleton<T>::m_mutex_ = PTHREAD_MUTEX_INITIALIZER;
template <typename T> typename Singleton<T>::Deleter Singleton<T>::m_deleter_;

#define DECLARE_SINGLETON(ClassName)                                           \
public:                                                                        \
  ClassName();                                                                 \
  ~ClassName();                                                                \
                                                                               \
private:                                                                       \
  ClassName(const ClassName &);                                                \
  ClassName &operator=(const ClassName &);

#define DECLARE_VIRTUAL_SINGLETON(ClassName)                                   \
public:                                                                        \
  ClassName();                                                                 \
  ~ClassName();                                                                \
                                                                               \
private:                                                                       \
  ClassName(const ClassName &);                                                \
  ClassName &operator=(const ClassName &);

#define IMPLEMENT_SINGLETON(ClassName)

template <typename T> T *Instance() { return Singleton<T>::Instance(); }

} // namespace stlib

#endif