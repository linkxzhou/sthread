/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_EVENT_H_
#define _ST_EVENT_H_

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include "st_def.h"

stlib_namespace_begin

#define DATA_SIZE   1

typedef struct  
{
    int32_t fd;
    int32_t mask;
    void *data[DATA_SIZE];
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
        m_file_ = (StFileEvent *)malloc(sizeof(StFileEvent) * size);
        m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * size);

        if (NULL == m_events_ || 
            NULL == m_file_ || 
            NULL == m_fired_) 
        {
            return ST_ERROR;
        }

        m_kqfd_ = kqueue();
        if (m_kqfd_ == -1)
        {
            ApiFree();
            return ST_ERROR;
        }

        m_size_ = size;

        memset(m_file_, 0, sizeof(StFileEvent) * size);
        memset(m_fired_, 0, sizeof(StFiredEvent) * size);
        memset(m_file_.data, 0, sizeof(void*) * DATA_SIZE);

        fcntl(m_kqfd_, F_SETFD, FD_CLOEXEC);

        return ST_OK;
    }

    int32_t ApiResize(int32_t setsize) 
    {
        m_events_ = (struct kevent *)realloc(m_events_, sizeof(struct kevent) * setsize);
        m_file_ = (StFileEvent *)malloc(sizeof(StFileEvent) * setsize);
        m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * setsize);

        m_size_ = setsize;

        return ST_OK;
    }

    void ApiFree() 
    {
        if (m_kqfd_ > 0)
        {
            ::close(m_kqfd_);
        }
        
        st_safe_free(m_events_);
        st_safe_free(m_file_);
        st_safe_free(m_fired_);
    }

    int32_t ApiAddEvent(int32_t fd, int32_t mask) 
    {
        // 不需要处理
        if (m_file_[fd].mask == mask)
        {
            return ST_OK;
        }

        struct kevent ke[1];
        if (mask & ST_READABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1) 
            {
                return ST_ERROR;
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
                return ST_ERROR;
            }
        }
        else
        {
            EV_SET(&ke[0], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1) 
            { }
        }

        m_file_[fd].mask = mask;

        return ST_OK;
    }

    int32_t ApiDelEvent(int32_t fd, int32_t delmask) 
    {
        struct kevent ke[1];
        int32_t mask = m_file_[fd].mask & (~delmask);

        if (mask & ST_READABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1)
            {
                return ST_ERROR;
            }
        }

        if (mask & ST_WRITEABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1)
            {
                return ST_ERROR;
            }
        }

        m_file_[fd].mask = mask;
        
        return ST_OK;
    }

    int32_t ApiPoll(struct timeval *tvp = NULL) 
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

private:
    int32_t m_kqfd_, m_size_;
    struct kevent *m_events_;
    StFileEvent *m_file_; // 保存状态

public:
    StFiredEvent *m_fired_; // 对外使用
};

stlib_namespace_end

#endif