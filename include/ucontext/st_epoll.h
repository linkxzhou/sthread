/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_EVENT_H_INCLUDED_
#define _ST_EVENT_H_INCLUDED_

#include <sys/epoll.h>
#include "st_util.h"

typedef struct 
{
    int32_t fd;
    int32_t mask;
} StFiredEvent;

typedef struct
{
    int32_t mask; /* one of READABLE|WRITABLE */
    void    *client_data;
} StFileEvent;

class StApiState
{
public:
    int32_t ApiCreate(int32_t size) 
    {
        m_events_ = (struct epoll_event *)malloc(sizeof(struct epoll_event) * size);
        m_file_events_ = (StFileEvent *)malloc(sizeof(StFileEvent) * size);
        m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * size);

        if (NULL == m_events_ || 
            NULL == m_file_events_ || 
            NULL == m_fired_) 
        {
            return -1;
        }

        m_epfd_ = epoll_create(1024); /* 1024 is just a hint for the kernel */
        if (m_epfd_ == -1) 
        {
            ApiFree();
            return -1;
        }

        m_size_ = size;

        memset(m_file_events_, 0, sizeof(StFileEvent) * size);
        memset(m_fired_, 0, sizeof(StFiredEvent) * size);

        fcntl(m_epfd_, F_SETFD, FD_CLOEXEC);

        return 0;
    }

    int32_t ApiResize(int32_t setsize) 
    {
        m_events_ = (struct epoll_event *)realloc(m_events_, sizeof(struct epoll_event) * setsize);
        m_file_events_ = (StFileEvent *)malloc(sizeof(StFileEvent) * setsize);
        m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * setsize);
        m_size_ = setsize;

        return 0;
    }

    void ApiFree() 
    {
        if (m_epfd_ > 0) ::close(m_epfd_);
        st_safe_free(m_events_);
        st_safe_free(m_file_events_);
        st_safe_free(m_fired_);

        m_size_ = 0;
    }

    int32_t ApiAddEvent(int32_t fd, int32_t mask) 
    {
        // 不需要处理
        if (m_file_events_[fd].mask == mask)
        {
            return 0;
        }

        struct epoll_event ee = {0}; /* avoid valgrind warning */
        int32_t op = (m_file_events_[fd].mask == ST_NONE) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

        ee.events = 0;
        mask |= m_file_events_[fd].mask; /* Merge old events */ 
        if (mask & ST_READABLE) ee.events |= EPOLLIN;
        if (mask & ST_WRITEABLE) ee.events |= EPOLLOUT;
        ee.data.fd = fd;

        if (epoll_ctl(m_epfd_, op, fd, &ee) == -1) 
        {
            return -1;
        }

        m_file_events_[fd].mask = mask;

        return 0;
    }

    int32_t ApiDelEvent(int32_t fd, int32_t delmask) 
    {
        struct epoll_event ee = {0}; /* avoid valgrind warning */
        int32_t mask = m_file_events_[fd].mask & (~delmask);

        ee.events = 0;
        if (mask & ST_READABLE) ee.events |= EPOLLIN;
        if (mask & ST_WRITEABLE) ee.events |= EPOLLOUT;
        ee.data.fd = fd;

        if (mask != ST_NONE) 
        {
            if (epoll_ctl(m_epfd_, EPOLL_CTL_MOD, fd, &ee) == -1)
            {
                return -1;
            }
        } 
        else 
        {
            /* Note, Kernel < 2.6.9 requires a non null event pointer even for
             * EPOLL_CTL_DEL. */
            if (epoll_ctl(m_epfd_, EPOLL_CTL_DEL, fd, &ee) == -1)
            {
                return -1;
            }
        }

        m_file_events_[fd].mask = m_file_events_[fd].mask & (~mask); 

        return 0;
    }

    int32_t ApiPoll(struct timeval *tvp) 
    {
        int32_t retval, numevents = 0;

        retval = epoll_wait(m_epfd_, m_events_, m_size_,
                tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
        if (retval > 0) 
        {
            int32_t j;

            numevents = retval;
            for (j = 0; j < numevents; j++) 
            {
                int32_t mask = 0;
                struct epoll_event *e = m_events_ + j;

                if (e->events & EPOLLIN) mask |= ST_READABLE;
                if (e->events & EPOLLOUT) mask |= ST_WRITEABLE;
                if (e->events & EPOLLERR) mask |= ST_EVERR;
                if (e->events & EPOLLHUP) mask |= ST_EVERR;

                m_fired_[j].fd = e->data.fd;
                m_fired_[j].mask = mask;
            }
        }

        return numevents;
    }

    StFiredEvent* EventList()
    {
        return m_fired_;
    }

    const char* ApiName(void) 
    {
        return "epoll";
    }

private:
    int32_t m_epfd_, m_size_;
    struct epoll_event *m_events_;
    StFileEvent *m_file_events_; // 保存状态

public:
    StFiredEvent *m_fired_; // 对外使用
};

#endif