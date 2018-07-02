/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_SINGLETON_H_INCLUDED_
#define _MT_SINGLETON_H_INCLUDED_

#include <new>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include "mt_public.h"

MTHREAD_NAMESPACE_BEGIN

#define DECLARE_UNCOPYABLE(Class) \
private: \
    Class(const Class&); \
    Class& operator=(const Class&);

template <typename T>
class Singleton
{
protected:
    Singleton() 
    { }
    ~Singleton() 
    { }
    
public:
    static T * GetInstance()
    {
#if __cplusplus < 201103L
        if (NULL == m_pInstance)
        {
            //double check 
            // pthread_mutex_lock(&m_mutex);
   
            // if (NULL == m_pInstance)
            // {
                m_pInstance = new T();
                m_deleter.set(m_pInstance);
            // }
        }

        return m_pInstance;
#else
        // Use C++ 11 style to implement singleton
        static __thread T t;
        return &t;
#endif
    }

    static void InstanceDestroy(void* obj = NULL)
    {
        if (m_pInstance != NULL)
        {
            safe_delete(m_pInstance);
        }
    }
    
    DECLARE_UNCOPYABLE(Singleton);

    class Deleter
    {
    public:
        Deleter()
        {
            pthread_key_create(&m_pkey, &Singleton<T>::InstanceDestroy);
        }
        ~Deleter()
        {
            pthread_key_delete(m_pkey);
        }
        void set(T* newObj)
        {
            assert(pthread_getspecific(m_pkey) == NULL);
            pthread_setspecific(m_pkey, newObj);
        }

    private:
        pthread_key_t m_pkey;
    };
    
private:
    static __thread T *m_pInstance;
    static pthread_mutex_t m_mutex;
    static Deleter m_deleter;
};

template<typename T> __thread T * Singleton<T>::m_pInstance = NULL;
template<typename T> pthread_mutex_t Singleton<T>::m_mutex = PTHREAD_MUTEX_INITIALIZER;
template<typename T> typename Singleton<T>::Deleter Singleton<T>::m_deleter;

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

MTHREAD_NAMESPACE_END

#endif