/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SINGLETON_H_INCLUDED_
#define _ST_SINGLETON_H_INCLUDED_

#include <new>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "st_public.h"

ST_NAMESPACE_BEGIN

#define DECLARE_UNCOPYABLE(Class) \
private: \
    Class(const Class&); \
    Class& operator=(const Class&);

template <typename T>
class Singleton
{
protected:
    Singleton() { }

    ~Singleton() { }
    
public:
    static T * GetInstance()
    {
#if __cplusplus < 201103L
        if (NULL == m_pinstance_)
        {
            // double check 
            pthread_mutex_lock(&m_mutex_);
   
            if (NULL == m_pinstance_)
            {
                m_pinstance_ = new T();
                m_deleter_.Set(m_pinstance_);
            }
        }

        return m_pinstance_;
#else
        // Use C++ 11 style to implement singleton
        static __thread T t;
        return &t;
#endif
    }

    static void InstanceDestroy(void *ins = NULL)
    {
        if (m_pinstance_ != NULL)
        {
            st_safe_delete(m_pinstance_);
        }
    }
    
    DECLARE_UNCOPYABLE(Singleton);

    class Deleter
    {
    public:
        Deleter()
        {
            pthread_key_create(&m_pkey_, &Singleton<T>::InstanceDestroy);
        }

        ~Deleter()
        {
            pthread_key_delete(m_pkey_);
        }

        void Set(T* o)
        {
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

template<typename T> __thread T * Singleton<T>::m_pinstance_ = NULL;
template<typename T> pthread_mutex_t Singleton<T>::m_mutex_ = PTHREAD_MUTEX_INITIALIZER;
template<typename T> typename Singleton<T>::Deleter Singleton<T>::m_deleter_;

#define DECLARE_SINGLETON(ClassName)                                      \
public:                                                                   \
    ClassName();                                                          \
    ~ClassName();                                                         \
private:                                                                  \
    ClassName(const ClassName&);                                          \
    ClassName& operator = (const ClassName&);                             \

#define DECLARE_VIRTUAL_SINGLETON(ClassName)                              \
public:                                                                   \
    ClassName();                                                          \
    ~ClassName();                                                         \
private:                                                                  \
    ClassName(const ClassName&);                                          \
    ClassName& operator = (const ClassName&);                             \

#define IMPLEMENT_SINGLETON(ClassName) 

template <typename T> T* GetInstance()
{
    return Singleton<T>::GetInstance();
}

ST_NAMESPACE_END

#endif