/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_EVENT_H_INCLUDED_
#define _ST_EVENT_H_INCLUDED_

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include "st_util.h"

typedef struct  
{
    int32_t fd;
    int32_t mask;
} StFiredEvent;

typedef struct
{
    int32_t mask; /* one of READABLE|WRITABLE */
    void *client_data;
} StFileEvent;

class StApiState
{
public:
    int32_t ApiCreate(int32_t size)
    {
        m_events_ = (struct kevent *)malloc(sizeof(struct kevent)*size);
        m_file_events_ = (StFileEvent *)malloc(sizeof(StFileEvent) * size);
        m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * size);

        if (NULL == m_events_ || 
            NULL == m_file_events_ || 
            NULL == m_fired_) 
        {
            return -1;
        }

        m_kqfd_ = kqueue();
        if (m_kqfd_ == -1)
        {
            ApiFree();
            return -1;
        }

        m_size_ = size;

        memset(m_file_events_, 0, sizeof(StFileEvent) * size);
        memset(m_fired_, 0, sizeof(StFiredEvent) * size);

        fcntl(m_kqfd_, F_SETFD, FD_CLOEXEC);

        return 0;
    }

    int32_t ApiResize(int32_t setsize) 
    {
        m_events_ = (struct kevent *)realloc(m_events_, sizeof(struct kevent) * setsize);
        m_file_events_ = (StFileEvent *)malloc(sizeof(StFileEvent) * setsize);
        m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * setsize);

        m_size_ = setsize;

        return 0;
    }

    void ApiFree() 
    {
        if (m_kqfd_ > 0) ::close(m_kqfd_);
        st_safe_free(m_events_);
        st_safe_free(m_file_events_);
        st_safe_free(m_fired_);
    }

    int32_t ApiAddEvent(int32_t fd, int32_t mask) 
    {
        // 不需要处理
        if (m_file_events_[fd].mask == mask)
        {
            return 0;
        }

        struct kevent ke[1];
        if (mask & ST_READABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1) 
            {
                return -1;
            }
        }
        else
        {
            EV_SET(&ke[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1) 
            { }
        }

        if (mask & ST_WRITEABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1)
            {
                return -1;
            }
        }
        else
        {
            EV_SET(&ke[0], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1) 
            { }
        }

        m_file_events_[fd].mask = mask;

        return 0;
    }

    int32_t ApiDelEvent(int32_t fd, int32_t mask) 
    {
        struct kevent ke[1];
        if (mask & ST_READABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1)
            {
                return -1;
            }
        }

        if (mask & ST_WRITEABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1)
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

        if (tvp != NULL) 
        {
            struct timespec timeout;
            timeout.tv_sec = tvp->tv_sec;
            timeout.tv_nsec = tvp->tv_usec * 1000;
            retval = kevent(m_kqfd_, NULL, 0, m_events_, m_size_, &timeout);
        } 
        else 
        {
            retval = kevent(m_kqfd_, NULL, 0, m_events_, m_size_, NULL);
        }

        if (retval > 0) 
        {
            numevents = retval;
            for (int32_t j = 0; j < numevents; j++) 
            {
                int32_t mask = 0;
                struct kevent *e = m_events_ + j;

                if (e->filter == EVFILT_READ) mask |= ST_READABLE;
                if (e->filter == EVFILT_WRITE) mask |= ST_WRITEABLE;
                m_fired_[j].fd = e->ident;
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
        return "kqueue";
    }

public:
    int32_t m_kqfd_, m_size_;
    struct kevent *m_events_;
    StFileEvent *m_file_events_; // 保存状态
    StFiredEvent *m_fired_; // 对外使用
};

#endif