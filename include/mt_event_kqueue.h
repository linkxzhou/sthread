/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_EVENT_H_INCLUDED_
#define _MT_EVENT_H_INCLUDED_

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include "mt_utils.h"

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
        m_events_ = (struct kevent *)malloc(sizeof(struct kevent)*size);
        m_file_events_ = (IMtFileEvent *)malloc(sizeof(IMtFileEvent) * size);
        m_fired_ = (IMtFiredEvent *)malloc(sizeof(IMtFiredEvent) * size);

        if (NULL == m_events_ || NULL == m_file_events_ ||NULL == m_fired_) 
        {
            return -1;
        }

        m_kqfd_ = kqueue();
        if (m_kqfd_ == -1)
        {
            safe_free(m_events_);
            return -1;
        }

        m_size_ = size;

        memset(m_file_events_, 0, sizeof(IMtFileEvent) * size);
        memset(m_fired_, 0, sizeof(IMtFiredEvent) * size);

        fcntl(m_kqfd_, F_SETFD, FD_CLOEXEC);

        return 0;
    }

    int ApiResize(int setsize) 
    {
        m_events_ = (struct kevent *)realloc(m_events_, sizeof(struct kevent) * setsize);
        m_file_events_ = (IMtFileEvent *)malloc(sizeof(IMtFileEvent) * setsize);
        m_fired_ = (IMtFiredEvent *)malloc(sizeof(IMtFiredEvent) * setsize);

        m_size_ = setsize;

        return 0;
    }

    void ApiFree() 
    {
        ::close(m_kqfd_);
        safe_free(m_events_);
        safe_free(m_file_events_);
        safe_free(m_fired_);
    }

    int ApiAddEvent(int fd, int mask) 
    {
        struct kevent ke[1];

        if (mask & MT_READABLE) 
        {
            LOG_TRACE("fd : %d, mask : MT_READABLE", fd);
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
            {
                // LOG_WARN("delete EVFILT_READ error");
            }
        }

        if (mask & MT_WRITABLE) 
        {
            LOG_TRACE("fd : %d, mask : MT_WRITABLE", fd);
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
            {
                // LOG_WARN("delete EVFILT_READ error");
            }
        }

        return 0;
    }

    int ApiDelEvent(int fd, int mask) 
    {
        struct kevent ke[1];

        if (mask & MT_READABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1)
            {
                return -1;
            }
        }

        if (mask & MT_WRITABLE) 
        {
            EV_SET(&ke[0], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            if (kevent(m_kqfd_, ke, 1, NULL, 0, NULL) == -1)
            {
                return -1;
            }
        }

        return 0;
    }

    int ApiPoll(struct timeval *tvp) 
    {
        int retval, numevents = 0;

        if (tvp != NULL) 
        {
            struct timespec timeout;
            timeout.tv_sec = tvp->tv_sec;
            timeout.tv_nsec = tvp->tv_usec * 1000;
            LOG_TRACE("tvp->tv_sec : %ld, tvp->tv_usec : %ld", tvp->tv_sec, tvp->tv_usec);
            retval = kevent(m_kqfd_, NULL, 0, m_events_, m_size_, &timeout);
        } 
        else 
        {
            retval = kevent(m_kqfd_, NULL, 0, m_events_, m_size_, NULL);
        }

        LOG_TRACE("retval : %d, tvp : %p, m_events_ : %p, m_size_ : %d", 
            retval, tvp, m_events_, m_size_);

        if (retval > 0) 
        {
            int j;

            numevents = retval;
            for(j = 0; j < numevents; j++) 
            {
                int mask = 0;
                struct kevent *e = m_events_ + j;

                if (e->filter == EVFILT_READ) mask |= MT_READABLE;
                if (e->filter == EVFILT_WRITE) mask |= MT_WRITABLE;
                m_fired_[j].fd = e->ident;
                m_fired_[j].mask = mask;
                LOG_TRACE("m_fired_[j].fd : %d", m_fired_[j].fd);
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
        return "kqueue";
    }

public:
    int m_kqfd_, m_size_;
    struct kevent *m_events_;
    IMtFileEvent *m_file_events_; // 保存状态
    IMtFiredEvent *m_fired_; // 对外使用
};

#endif