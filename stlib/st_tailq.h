/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_TAILQ_H_
#define _ST_TAILQ_H_

#define TAILQ_EMPTY(head)   ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head)   ((head)->tqh_first)
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

#define TAILQ_FOREACH(var, head, field)                                     \
        for ((var) = TAILQ_FIRST((head));                                   \
            (var);                                                          \
            (var) = TAILQ_NEXT((var), field))

#define TAILQ_CONCAT(head1, head2, field)                                   \
    do {                                                                    \
        if (!TAILQ_EMPTY(head2)) {                                          \
            *(head1)->tqh_last = (head2)->tqh_first;                        \
            (head2)->tqh_first->field.tqe_prev = (head1)->tqh_last;         \
            (head1)->tqh_last = (head2)->tqh_last;                          \
            TAILQ_INIT((head2));                                            \
        }                                                                   \
    } while (0)

#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                          \
        for ((var) = TAILQ_FIRST((head));                                   \
            (var) && ((tvar) = TAILQ_NEXT((var), field), 1);                \
            (var) = (tvar))
#endif

typedef long long time64_t;

// 使用c++封装的linux的TAILQ_ENTRY
template<class T>
class CPP_TAILQ_HEAD
{
public:
    CPP_TAILQ_HEAD() : 
        tqh_first(NULL), 
        tqh_last(NULL), 
        tqh_size(0)
    { }
    
public:
    T *tqh_first; /* first element */
    T **tqh_last; /* addr of last next element */
    unsigned int tqh_size;
};

template<class T>
class CPP_TAILQ_ENTRY 
{
public:
    CPP_TAILQ_ENTRY() : 
        tqe_next(NULL), 
        tqe_prev(NULL),
        parent(NULL),
        remove_tag(true)
    { }

public:
    T *tqe_next;  /* next element */
    T **tqe_prev; /* address of previous next element */
    CPP_TAILQ_HEAD<T> *parent;
    bool remove_tag;
};

#define CPP_TAILQ_INCR(head)        ((head)->tqh_size++)
#define CPP_TAILQ_DECR(head)        (((head)->tqh_size <= 0) ? 0 : ((head)->tqh_size--))
#define CPP_TAILQ_SIZE(head)        ((head)->tqh_size)
#define CPP_TAILQ_FIRST(head)       ((head)->tqh_first)
#define CPP_TAILQ_NEXT(elm, field)  ((elm)->field.tqe_next)
#define CPP_TAILQ_EMPTY(head)       ((head)->tqh_first == NULL) // 判断队列是否为空
#define CPP_TAILQ_INIT(head)        do                  \
    {                                                   \
        CPP_TAILQ_FIRST(head) = NULL;                   \
        (head)->tqh_last = &CPP_TAILQ_FIRST(head);      \
    } while (0)

#define CPP_TAILQ_FOREACH(var, head, field)         \
    for ((var) = CPP_TAILQ_FIRST(head);             \
        (var);                                      \
        (var) = CPP_TAILQ_NEXT((var), field))

#define CPP_TAILQ_FOREACH_SAFE(var, head, field, _var)          \
    for ((var) = CPP_TAILQ_FIRST((head));                       \ 
        (var) && ((_var) = CPP_TAILQ_NEXT((var), field), 1);    \ 
        (var) = (_var)) 

#define CPP_TAILQ_REMOVE(head, elm, field)          do          \
    {                                                           \
        if ((elm)->field.remove_tag) break;                     \
        if ((CPP_TAILQ_NEXT((elm), field)) != NULL)             \
            CPP_TAILQ_NEXT((elm), field)->field.tqe_prev =      \
                (elm)->field.tqe_prev;                          \
        else                                                    \
            (head)->tqh_last = (elm)->field.tqe_prev;           \
        CPP_TAILQ_DECR(head);                                   \
        (elm)->field.remove_tag = true;                         \
        (elm)->field.parent = NULL;                             \
        *(elm)->field.tqe_prev = CPP_TAILQ_NEXT((elm), field);  \
    } while (0)

#define CPP_TAILQ_INSERT_TAIL(head, elm, field)     do          \
    {                                                           \
        CPP_TAILQ_REMOVE(head, elm, field);                     \
        CPP_TAILQ_NEXT((elm), field) = NULL;                    \
        (elm)->field.tqe_prev = (head)->tqh_last;               \
        *(head)->tqh_last = (elm);                              \
        (head)->tqh_last = &CPP_TAILQ_NEXT((elm), field);       \
        CPP_TAILQ_INCR(head);                                   \
        (elm)->field.remove_tag = false;                        \
        (elm)->field.parent = head;                             \
    } while (0)

#define CPP_TAILQ_CONCAT(head1, head2, field)       do      \
    {                                                       \
        if (!CPP_TAILQ_EMPTY(head2)) {                      \
            *(head1)->tqh_last = (head2)->tqh_first;        \
            (head2)->tqh_first->field.tqe_prev = (head1)->tqh_last;     \
            (head1)->tqh_last = (head2)->tqh_last;                      \
            (head1)->tqh_size = (head1)->tqh_size + (head2)->tqh_size;  \
            CPP_TAILQ_INIT((head2));                    \
        }                                               \
    } while (0)

#define CPP_TAILQ_POP(head, elm, field)             do      \
    {                                                       \
        elm = CPP_TAILQ_FIRST(head);                        \
        CPP_TAILQ_REMOVE(head, elm, field);                 \
    } while (0)

#define CPP_TAILQ_REMOVE_SELF(elm, field)           do      \
    {                                                       \
        if ((elm)->field.parent != NULL) {                  \
            CPP_TAILQ_REMOVE((elm)->field.parent, elm, field);   \
        }                                                   \
    } while (0)

#endif