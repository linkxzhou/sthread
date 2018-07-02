/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_UTILS_H_INCLUDED_
#define _MT_UTILS_H_INCLUDED_

#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <setjmp.h>
#include <sys/mman.h> 
#include <arpa/inet.h>
#include <typeinfo>
#include <sys/resource.h>
#include "mt_public.h"
#include "mt_enum.h"
#include "mt_singleton.h"

MTHREAD_NAMESPACE_BEGIN

/**
* @brief add more detail for linux <sys/queue.h>, freebsd and University of California
* @info  queue.h version 8.3 (suse)  diff version 8.5 (tlinux)
*/
#ifndef TAILQ_CONCAT

#define TAILQ_EMPTY(head)   ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head)   ((head)->tqh_first)
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

#define TAILQ_FOREACH(var, head, field)                                     \
        for ((var) = TAILQ_FIRST((head));                                   \
            (var);                                                         \
            (var) = TAILQ_NEXT((var), field))

#define TAILQ_CONCAT(head1, head2, field)                                   \
do {                                                                        \
    if (!TAILQ_EMPTY(head2)) {                                              \
        *(head1)->tqh_last = (head2)->tqh_first;                            \
        (head2)->tqh_first->field.tqe_prev = (head1)->tqh_last;             \
        (head1)->tqh_last = (head2)->tqh_last;                              \
        TAILQ_INIT((head2));                                                \
    }                                                                       \
} while (0)

#endif

#ifndef TAILQ_FOREACH_SAFE      // tlinux no this define
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                          \
        for ((var) = TAILQ_FIRST((head));                                   \
            (var) && ((tvar) = TAILQ_NEXT((var), field), 1);               \
            (var) = (tvar))
#endif

typedef unsigned long long utime64_t;

// 使用c++封装的linux的TAILQ_ENTRY
template<class T>
class CPP_TAILQ_ENTRY 
{
public:
    CPP_TAILQ_ENTRY() : tqe_next(NULL), tqe_prev(NULL)
    { }

public:
    T *tqe_next;  /* next element */
    T **tqe_prev; /* address of previous next element */
};

template<class T>
class CPP_TAILQ_HEAD
{
public:
    CPP_TAILQ_HEAD() : tqh_first(NULL), tqh_last(NULL), tqh_size(0)
    { }
    
public:
    T *tqh_first; /* first element */
    T **tqh_last; /* addr of last next element */
    uint32_t tqh_size;
};

#define CPP_TAILQ_INCR(head)        ((head)->tqh_size++)
#define CPP_TAILQ_DECR(head)        (((head)->tqh_size<=0)?0:((head)->tqh_size--))
#define CPP_TAILQ_SIZE(head)        ((head)->tqh_size)
#define CPP_TAILQ_FIRST(head)       ((head)->tqh_first)
#define CPP_TAILQ_NEXT(elm, field)  ((elm)->field.tqe_next)
#define CPP_TAILQ_EMPTY(head)       ((head)->tqh_first == NULL) // 判断队列是否为空
#define CPP_TAILQ_INIT(head) do {                   \
    CPP_TAILQ_FIRST((head)) = NULL;                 \
    (head)->tqh_last = &CPP_TAILQ_FIRST((head));    \
} while (0)

#define CPP_TAILQ_CONCAT(head1, head2, field)      \
do {                                                \
    if (!CPP_TAILQ_EMPTY(head2)) {                  \
        *(head1)->tqh_last = (head2)->tqh_first;    \
        (head2)->tqh_first->field.tqe_prev = (head1)->tqh_last; \
        (head1)->tqh_last = (head2)->tqh_last;      \
        (head1)->tqh_size = (head1)->tqh_size + (head2)->tqh_size; \
        CPP_TAILQ_INIT((head2));                    \
    }                                               \
} while (0)

#define CPP_TAILQ_FOREACH(var, head, field)         \
    for ((var) = CPP_TAILQ_FIRST((head));           \
        (var);                                      \
        (var) = CPP_TAILQ_NEXT((var), field))

#define CPP_TAILQ_FOREACH_SAFE(var, head, field, tvar)              \
         for ((var) = CPP_TAILQ_FIRST((head));                      \ 
             (var) && ((tvar) = CPP_TAILQ_NEXT((var), field), 1);   \ 
             (var) = (tvar)) 

#define CPP_TAILQ_REMOVE(head, elm, field) do {             \
    if ((CPP_TAILQ_NEXT((elm), field)) != NULL)             \
        CPP_TAILQ_NEXT((elm), field)->field.tqe_prev =      \
            (elm)->field.tqe_prev;                          \
    else                                                    \
        (head)->tqh_last = (elm)->field.tqe_prev;           \
    CPP_TAILQ_DECR(head);                                   \
    *(elm)->field.tqe_prev = CPP_TAILQ_NEXT((elm), field);  \
} while (0)

#define CPP_TAILQ_INSERT_TAIL(head, elm, field) do {        \
    CPP_TAILQ_NEXT((elm), field) = NULL;                    \
    (elm)->field.tqe_prev = (head)->tqh_last;               \
    *(head)->tqh_last = (elm);                              \
    (head)->tqh_last = &CPP_TAILQ_NEXT((elm), field);       \
    CPP_TAILQ_INCR(head);                                   \
} while (0)

class Utils
{
public:
    // 获取系统时间
    static utime64_t system_ms()
    {
        struct timeval tv;
        ::gettimeofday(&tv, NULL);
        return (tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL);
    }
    static unsigned int system_sleep(unsigned int seconds)
    {
        return ::sleep(seconds);
    }
    static void system_usleep(unsigned long u_seconds)
    {
        ::usleep(u_seconds);
        return ;
    }
    static int max_prime_num(int num)
    {
        int sqrt_value = (int)sqrt(num);
        for (int i = num; i > 0; i--)
        {
            int flag = 1;
            for (int k = 2; k <= sqrt_value; k++)
            {
                if (i % k == 0)
                {
                    flag = 0;
                    break;
                }
            }
            if (flag == 1)
            {
                return i;
            }
        }

        return 0;
    }
    static uint generate_uniqid()
    {
        static uint id = 1;
        return ++id;
    }
};

// 对象池
template<typename ValueType>
class UtilsPtrPool
{
public:
    typedef typename std::queue<ValueType*> PtrQueue;

public:
    explicit UtilsPtrPool(int max = 500) : m_max_free_(max), m_total_(0)
    { }
    ~UtilsPtrPool()
    {
        ValueType* ptr = NULL;
        while (!m_ptr_list_.empty())
        {
            ptr = m_ptr_list_.front();
            m_ptr_list_.pop();
            safe_delete(ptr);
        }
    }
    int Size()
    {
        return m_total_;
    }
    ValueType* AllocPtr()
    {
        ValueType* ptr = NULL;
        if (!m_ptr_list_.empty())
        {
            ptr = m_ptr_list_.front();
            m_ptr_list_.pop();
        }
        else
        {
            ptr = new ValueType;
            m_total_++;
        }

        return ptr;
    }
    void FreePtr(ValueType* ptr)
    {
        if ((int)m_ptr_list_.size() >= m_max_free_)
        {
            safe_delete(ptr);
            m_total_--;
        }
        else
        {
            m_ptr_list_.push(ptr);
        }
    }

protected:
    PtrQueue  m_ptr_list_;
    int       m_max_free_;
    int       m_total_;
};

class referenceable
{
protected:
    referenceable() : m_ref_count_(0)
    { }

    void incrref()
    {
        ++m_ref_count_;
    }

    int getref()
    {
        return m_ref_count_;
    }

protected:
    int m_ref_count_;
};

class Any 
{
public:
    Any() : m_content_(NULL) {}
    ~Any() 
    {
        safe_delete(m_content_);
    }

    template<typename ValueType>
    explicit Any(const ValueType& value)
        : m_content_(new Holder<ValueType>(value)) {}

    Any(const Any& rhs)
        : m_content_(rhs.m_content_ ? rhs.m_content_->Clone() : NULL) {}

public:
    Any& swap(Any& rhs) 
    {
        std::swap(m_content_, rhs.m_content_);
        return *this;
    }

    template<typename ValueType>
    Any& operator=(const ValueType& rhs) 
    {
        Any(rhs).swap(*this);
        return *this;
    }

    Any& operator=(const Any& rhs) 
    {
        Any(rhs).swap(*this);
        return *this;
    }

    bool IsEmpty() const 
    {
        return !m_content_;
    }

    const std::type_info& GetType() const 
    {
        return m_content_ ? m_content_->GetType() : typeid(void);
    }

    template<typename ValueType>
    ValueType operator()() const 
    {
        if (GetType() == typeid(ValueType)) 
        {
            return static_cast<Any::Holder<ValueType>*>(m_content_)->m_held_;
        } 
        else 
        {
            return ValueType();
        }
    }

protected:
    class PlaceHolder 
    {
    public:
        virtual ~PlaceHolder() {}
    public:
        virtual const std::type_info& GetType() const = 0;
        virtual PlaceHolder* Clone() const = 0;
    };

    template<typename ValueType>
    class Holder : public PlaceHolder 
    {
    public:
        Holder(const ValueType& value) : m_held_(value) {}

        virtual const std::type_info& GetType() const 
        {
            return typeid(ValueType);
        }

        virtual PlaceHolder* Clone() const 
        {
            return new Holder(m_held_);
        }

    public:
        ValueType m_held_;
    };

protected:
    PlaceHolder* m_content_;

    template<typename ValueType> friend ValueType* any_cast(Any*);
};

template<typename ValueType>
ValueType* any_cast(Any* any) 
{
    if (any && any->GetType() == typeid(ValueType)) 
    {
        return &(static_cast<Any::Holder<ValueType>*>(any->m_content_)->m_held_);
    }
    
    // 如果any为NULL或者其他，则用原始的static_cast方法
    return static_cast<ValueType*>(any);
}

template<typename ValueType>
ValueType* any_cast(void* any) 
{
    return static_cast<ValueType*>(any);
}

template<typename ValueType>
const ValueType* any_cast(const Any* any) 
{
    return any_cast<ValueType>(const_cast<Any*>(any));
}

template<typename ValueType>
ValueType any_cast(const Any& any) 
{
    const ValueType* result = any_cast<ValueType>(&any);

    if (!result) 
    {
        return ValueType();
    }
 
    return *result;
}

MTHREAD_NAMESPACE_END

extern "C" unsigned long mt_get_threadid(void);

extern "C" void mt_set_threadid(unsigned long threadid);

#endif