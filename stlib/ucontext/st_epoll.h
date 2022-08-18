/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_EVENT_H_
#define _ST_EVENT_H_

#include "st_def.h"
#include <sys/epoll.h>

namespace stlib {

#define DATA_SIZE 1
#define EVENT_SIZE 1024

typedef struct {
  int32_t fd;
  int32_t mask;
  void *data[DATA_SIZE];
} StFiredEvent;

typedef struct {
  int32_t mask; /* one of READABLE|WRITABLE */
  void *client_data;
} StFileEvent;

class StIOState {
public:
  int32_t Create(int32_t size) {
    m_events_ = (struct epoll_event *)malloc(sizeof(struct epoll_event) * size);
    m_file_ = (StFileEvent *)malloc(sizeof(StFileEvent) * size);
    m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * size);

    if (NULL == m_events_ || NULL == m_file_ || NULL == m_fired_) {
      return ST_ERROR;
    }

    m_epfd_ = epoll_create(EVENT_SIZE);
    if (m_epfd_ == -1) {
      ApiFree();
      return ST_ERROR;
    }

    m_size_ = size;

    memset(m_file_, 0, sizeof(StFileEvent) * size);
    memset(m_fired_, 0, sizeof(StFiredEvent) * size);
    memset(m_file_.data, 0, sizeof(void *) * DATA_SIZE);

    fcntl(m_epfd_, F_SETFD, FD_CLOEXEC);

    return ST_OK;
  }

  int32_t ApiResize(int32_t setsize) {
    m_events_ = (struct epoll_event *)realloc(
        m_events_, sizeof(struct epoll_event) * setsize);
    m_file_ = (StFileEvent *)malloc(sizeof(StFileEvent) * setsize);
    m_fired_ = (StFiredEvent *)malloc(sizeof(StFiredEvent) * setsize);
    m_size_ = setsize;

    return ST_OK;
  }

  void ApiFree() {
    if (m_epfd_ > 0) {
      ::close(m_epfd_);
    }

    st_safe_free(m_events_);
    st_safe_free(m_file_);
    st_safe_free(m_fired_);

    m_size_ = 0;
  }

  int32_t AddEvent(int32_t fd, int32_t mask) {
    if (m_file_[fd].mask == mask) {
      return ST_OK;
    }

    struct epoll_event ee = {0}; /* avoid valgrind warning */
    int32_t op = (m_file_[fd].mask == ST_NONE) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= m_file_[fd].mask; /* Merge old events */
    if (mask & ST_READABLE)
      ee.events |= EPOLLIN;
    if (mask & ST_WRITEABLE)
      ee.events |= EPOLLOUT;
    ee.data.fd = fd;

    if (epoll_ctl(m_epfd_, op, fd, &ee) == -1) {
      return ST_ERROR;
    }

    m_file_events_[fd].mask = mask;

    return ST_OK;
  }

  int32_t DelEvent(int32_t fd, int32_t delmask) {
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    int32_t mask = m_file_[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & ST_READABLE)
      ee.events |= EPOLLIN;
    if (mask & ST_WRITEABLE)
      ee.events |= EPOLLOUT;
    ee.data.fd = fd;

    if (mask != ST_NONE) {
      if (epoll_ctl(m_epfd_, EPOLL_CTL_MOD, fd, &ee) == -1) {
        return ST_ERROR;
      }
    } else {
      /* Note, Kernel < 2.6.9 requires a non null event pointer even for
       * EPOLL_CTL_DEL. */
      if (epoll_ctl(m_epfd_, EPOLL_CTL_DEL, fd, &ee) == -1) {
        return ST_ERROR;
      }
    }

    m_file_[fd].mask = mask;

    return ST_OK;
  }

  int32_t Poll(struct timeval *tvp = NULL) {
    int32_t retval, numevents = 0;

    retval = epoll_wait(m_epfd_, m_events_, m_size_,
                        tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1);
    if (retval > 0) {
      int32_t j;

      numevents = retval;
      for (j = 0; j < numevents; j++) {
        int32_t mask = 0;
        struct epoll_event *e = m_events_ + j;

        if (e->events & EPOLLIN)
          mask |= ST_READABLE;
        if (e->events & EPOLLOUT)
          mask |= ST_WRITEABLE;
        if (e->events & EPOLLERR)
          mask |= ST_EVERR;
        if (e->events & EPOLLHUP)
          mask |= ST_EVERR;

        m_fired_[j].fd = e->data.fd;
        m_fired_[j].mask = mask;
      }
    }

    return numevents;
  }

  StFiredEvent *EventList() { return m_fired_; }

  const char *ApiName(void) { return "epoll"; }

private:
  int32_t m_epfd_, m_size_;
  struct epoll_event *m_events_;
  StFileEvent *m_file_;

public:
  StFiredEvent *m_fired_;
};

} // namespace stlib

#endif