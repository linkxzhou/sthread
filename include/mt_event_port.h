/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_EVENT_H_INCLUDED_
#define _MT_EVENT_H_INCLUDED_

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <port.h>
#include <poll.h>

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
        m_events_ = (struct port_event_t *)malloc(sizeof(struct port_event_t)*size);
        m_file_events_ = (IMtFileEvent *)malloc(sizeof(IMtFileEvent) * size);
        m_fired_ = (IMtFiredEvent *)malloc(sizeof(IMtFiredEvent) * size);

        if (NULL == m_events_ || NULL == m_file_events_ ||NULL == m_fired_) 
        {
            return -1;
        }

        m_portfd_ = port_create();
        if (m_portfd_ == -1)
        {
            safe_free(m_events_);
            return -1;
        }

        m_size_ = size;

        memset(m_file_events_, 0, sizeof(IMtFileEvent) * size);
        memset(m_fired_, 0, sizeof(IMtFiredEvent) * size);

        fcntl(m_portfd_, F_SETFD, FD_CLOEXEC);

        return 0;
    }

    int ApiResize(int setsize) 
    {
        m_events_ = (struct port_event_t *)realloc(m_events_, sizeof(struct port_event_t) * setsize);
        m_file_events_ = (IMtFileEvent *)malloc(sizeof(IMtFileEvent) * setsize);
        m_fired_ = (IMtFiredEvent *)malloc(sizeof(IMtFiredEvent) * setsize);

        m_size_ = setsize;

        return 0;
    }

    void ApiFree() 
    {
        ::close(m_portfd_);
        safe_free(m_events_);
        safe_free(m_file_events_);
        safe_free(m_fired_);
    }

    int ApiAddEvent(int fd, int mask) 
    {

        return 0;
    }

    int ApiDelEvent(int fd, int mask) 
    {

        return 0;
    }

    int ApiPoll(struct timeval *tvp) 
    {
        int retval, numevents = 0;

        return numevents;
    }

    IMtFiredEvent* EventList()
    {
        return m_fired_;
    }

    const char *ApiName(void) 
    {
        return "port";
    }

public:
    int m_portfd_, m_size_;
    struct port_event_t *m_events_;
    IMtFileEvent *m_file_events_; // 保存状态
    IMtFiredEvent *m_fired_; // 对外使用
};

#endif