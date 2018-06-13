/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_EVENT_H_INCLUDED_
#define _MT_EVENT_H_INCLUDED_

#include <sys/epoll.h>

/* A fired event */
typedef struct  
{
    int fd;
    int mask;
} IMtFiredEvent;

typedef struct
{
    int mask; /* one of READABLE|WRITABLE */
    void *client_data;
} IMtFileEvent;

class IMtApiState
{
public:
    int ApiCreate(int size) 
    {
        m_events_ = (struct epoll_event *)malloc(sizeof(struct epoll_event) * size);
        m_file_events_ = (IMtFileEvent *)malloc(sizeof(IMtFileEvent) * size);
        m_fired_ = (IMtFiredEvent *)malloc(sizeof(IMtFiredEvent) * size);

        if (NULL == m_events_ || NULL == m_file_events_ ||NULL == m_fired_) 
        {
            return -1;
        }

        m_epfd_ = epoll_create(1024); /* 1024 is just a hint for the kernel */
        if (m_epfd_ == -1) 
        {
            safe_free(m_events_);
            safe_free(m_file_events_);
            safe_free(m_fired_);
            return -1;
        }

        m_size_ = size;

        memset(m_file_events_, 0, sizeof(IMtFileEvent) * size);
        memset(m_fired_, 0, sizeof(IMtFiredEvent) * size);

        fcntl(m_epfd_, F_SETFD, FD_CLOEXEC);

        return 0;
    }

    int ApiResize(int setsize) 
    {
        m_events_ = (struct epoll_event *)realloc(m_events_, sizeof(struct epoll_event) * setsize);
        m_file_events_ = (IMtFileEvent *)malloc(sizeof(IMtFileEvent) * setsize);
        m_fired_ = (IMtFiredEvent *)malloc(sizeof(IMtFiredEvent) * setsize);
        m_size_ = setsize;

        return 0;
    }

    void ApiFree() 
    {
        close(m_epfd_);
        safe_free(m_events_);
        safe_free(m_file_events_);
        safe_free(m_fired_);

        m_size_ = 0;
    }

    int ApiAddEvent(int fd, int mask) 
    {
        struct epoll_event ee = {0}; /* avoid valgrind warning */
        int op = (m_file_events_[fd].mask == MT_NONE) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

        ee.events = 0;
        mask |= m_file_events_[fd].mask; /* Merge old events */
        if (mask & MT_READABLE) ee.events |= EPOLLIN;
        if (mask & MT_WRITABLE) ee.events |= EPOLLOUT;
        ee.data.fd = fd;

        if (epoll_ctl(m_epfd_, op, fd, &ee) == -1) 
        {
            return -1;
        }

        return 0;
    }

    void ApiDelEvent(int fd, int delmask) 
    {
        struct epoll_event ee = {0}; /* avoid valgrind warning */
        int mask = m_file_events_[fd].mask & (~delmask);

        ee.events = 0;
        if (mask & MT_READABLE) ee.events |= EPOLLIN;
        if (mask & MT_WRITABLE) ee.events |= EPOLLOUT;
        ee.data.fd = fd;

        if (mask != MT_NONE) 
        {
            epoll_ctl(m_epfd_, EPOLL_CTL_MOD, fd, &ee);
        } 
        else 
        {
            /* Note, Kernel < 2.6.9 requires a non null event pointer even for
             * EPOLL_CTL_DEL. */
            epoll_ctl(m_epfd_, EPOLL_CTL_DEL, fd, &ee);
        }
    }

    int ApiPoll(struct timeval *tvp) 
    {
        int retval, numevents = 0;

        retval = epoll_wait(m_epfd_, m_events_, m_size_,
                tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
        if (retval > 0) 
        {
            int j;

            numevents = retval;
            for (j = 0; j < numevents; j++) 
            {
                int mask = 0;
                struct epoll_event *e = m_events_ + j;

                if (e->events & EPOLLIN) mask |= MT_READABLE;
                if (e->events & EPOLLOUT) mask |= MT_WRITABLE;
                if (e->events & EPOLLERR) mask |= MT_EVERR;
                if (e->events & EPOLLHUP) mask |= MT_EVERR;

                m_fired_[j].fd = e->data.fd;
                m_fired_[j].mask = mask;
            }
        }

        return numevents;
    }

    IMtFiredEvent* EventList()
    {
        return m_fired_;
    }

    const char *ApiName(void) 
    {
        return "epoll";
    }

private:
    int m_epfd_, m_size_;
    struct epoll_event *m_events_;
    IMtFileEvent *m_file_events_; // 保存状态

public:
    IMtFiredEvent *m_fired_; // 对外使用
};

#endif