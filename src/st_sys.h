/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_SYS_H_
#define _ST_SYS_H_

#include "st_public.h"
#include "st_thread.h"
#include "stlib/st_heap_timer.h"
#include "stlib/st_util.h"

namespace sthread {

/* 用途：系统级调度入口；Init 绑定 daemon 回调，StartUp 为 daemon 事件循环。
 * 线程模型：线程局部；与 GlobalThreadSchedule/GlobalEventSchedule 同 OS 线程。
 * 所有权：持有 StHeapTimer；daemon/primo 仅别名调度器持有对象，勿 delete。 */
class StSysSchedule {
public:
  StSysSchedule()
      : m_daemon_(NULL), m_primo_(NULL), m_heap_timer_(NULL), m_timeout_(1000) {
    Init(); // 初始化
  }

  ~StSysSchedule() {
    // daemon/primo owned by StThreadSchedule; do not delete
    st_safe_delete(m_heap_timer_);
  }

  void Init(int max_num = 1024) {
    int r = GlobalThreadSchedule()->m_sleep_list_.HeapResize(max_num * 2);
    LOG_ASSERT(r >= 0);

    m_heap_timer_ = new StHeapTimer(max_num * 2);
    LOG_ASSERT(m_heap_timer_ != NULL);

    // D3(B): schedule owns daemon/primo via lazy getters; we only alias +
    // rebind callback.
    m_daemon_ =
        dynamic_cast<StThread *>(GlobalThreadSchedule()->DaemonThread());
    LOG_ASSERT(m_daemon_ != NULL);
    m_daemon_->SetCallback(NewStClosure(StartUp, this));

    m_primo_ = dynamic_cast<StThread *>(GlobalThreadSchedule()->PrimoThread());
    LOG_ASSERT(m_primo_ != NULL);

    GlobalThreadSchedule()->SetActiveThread(m_primo_);
    m_last_clock_ = Util::TimeMs();

    LOG_TRACE("m_last_clock_: %d", m_last_clock_);
  }

  inline void UpdateLastClock() { m_last_clock_ = Util::TimeMs(); }

  inline int64_t GetLastClock() { return m_last_clock_; }

  inline StHeapTimer *GetStHeapTimer() { return m_heap_timer_; }

  inline void CheckExpired() {
    int32_t count = 0;
    if (NULL != m_heap_timer_) {
      count = m_heap_timer_->CheckExpired();
    }
    LOG_TRACE("CheckExpired count: %d", count);
  }

  inline int64_t GetTimeout() {
    StThread *thread = dynamic_cast<StThread *>(
        GlobalThreadSchedule()->m_sleep_list_.HeapTop());

    int64_t now = GetLastClock();
    if (!thread) {
      return m_timeout_;
    } else if (thread->GetWakeupTime() < now) {
      return 0;
    } else {
      return (int64_t)(thread->GetWakeupTime() - now);
    }
  }

  /* WaitEvents removed (A2/D2); use st_read/st_write/... instead. */

  StThread *CreateThread(StClosure *closure, bool runable = true) {
    return GlobalThreadSchedule()->CreateThread(closure, runable);
  }

  inline StThread *AllocThread() {
    return GlobalThreadSchedule()->AllocThread();
  }

  static void StartUp(StSysSchedule *schedule) {
    LOG_ASSERT(schedule != NULL);
    StThread *daemon = schedule->m_daemon_;
    StEventSchedule *event_schedule = GlobalEventSchedule();
    StThreadSchedule *thread_schedule = GlobalThreadSchedule();
    if (NULL == daemon || NULL == event_schedule || NULL == thread_schedule) {
      LOG_ERROR("daemon: %p, event_schedule: %p, thread_schedule: %p", daemon,
                event_schedule, thread_schedule);
      return;
    }

    LOG_TRACE("--------daemon: %p", daemon);

    do {
      event_schedule->Wait(schedule->GetTimeout());
      schedule->UpdateLastClock();
      int64_t now = schedule->GetLastClock();
      LOG_TRACE("--------[name:%s]--------- : system ms: %ld",
                GlobalThreadSchedule()->GetActiveThread()->GetName(), now);
      // 判断sleep的thread
      thread_schedule->Wakeup(now);
      schedule->CheckExpired();
      // 让出线程
      thread_schedule->Yield(daemon);
    } while (true);
  }

public:
  StThread *m_daemon_, *m_primo_;
  StHeapTimer *m_heap_timer_;
  int64_t m_last_clock_, m_timeout_;
};

} // namespace sthread

#ifdef __cplusplus
extern "C" {
#endif

/* 实现：src/st_sys.cc 内 WaitFdReady 统一挂起骨架（plan/07 C1）。
 * st_* 返回值约定（超时与 Schedule 失败分开，不再共用 -3）：
 *  >0 : 成功字节数或 connfd；connect 成功多为 0
 *   0 : 对端关闭或 n==0 历史语义（UDP recvfrom 见 D3）
 *  -1 : 硬错误，或超时（errno=ETIME）。包括等到唤醒但没有 IO 事件
 *  -2 : 无事件 item（errno=EINVAL）
 *  -3 : Schedule / Add 失败（不是超时）
 */
int st_sendto(int fd, const void *msg, int len, int flags,
              const struct sockaddr *to, int tolen, int timeout);

int st_recvfrom(int fd, void *buf, int len, int flags, struct sockaddr *from,
                socklen_t *fromlen, int timeout);

int st_connect(int fd, const struct sockaddr *addr, int addrlen, int timeout);

ssize_t st_read(int fd, void *buf, size_t nbyte, int timeout);

ssize_t st_write(int fd, const void *buf, size_t nbyte, int timeout);

int st_recv(int fd, void *buf, int len, int flags, int timeout);

ssize_t st_send(int fd, const void *buf, size_t nbyte, int flags, int timeout);

void st_sleep(int ms);

int st_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

/* 同一 OS 线程上的协程通知。不占 fd，不进 extern "C"（参数带 StThread*）。
 * 调用写法和 st_sleep 一样，不用加命名空间。
 *
 * ST_WAIT_FD     这次醒来时 fd 上有事件
 * ST_WAIT_NOTIFY 这次醒来是因为 st_notify（含调用前已经记下的粘滞通知）
 */
#define ST_WAIT_FD 0x1
#define ST_WAIT_NOTIFY 0x2

/* 当前协程等到通知，或超时。
 *  0  被 st_notify 叫醒，含调用前已经记下的粘滞通知
 * -1  超时，errno=ETIME；当前没有协程，errno=EINVAL
 * timeout_ms < 0：一直等（内部收成 0x7fffffff，与 NormalizeTimeoutMs 相同）
 * timeout_ms == 0：不挂起；有粘滞则 0，否则 -1 且 errno=ETIME
 */
int st_notify_wait(int timeout_ms);

/* 给 target 记一次通知。只限当前 OS 线程的 StThreadSchedule。
 *  0  已记下。target 正在 st_notify_wait 或 st_wait 中，则离开睡眠堆
 *     或 IO 队列，进入可运行队列。没在等则只留粘滞位，下次 wait 立即成功。
 *     多次 notify 合并成一次。
 * -1  target==NULL，或 target 不属于当前调度器，errno=EINVAL
 * 不调用 Unpend，不跨 OS 线程。
 */
int st_notify(sthread::StThread *target);

/* 当前协程同时等 fd 和通知。
 * want_read 非 0 等可读，0 等可写。
 * fd 必须已经在事件表里，否则 -2 且 errno=EINVAL（与 st_read 的 -2 相同）。
 *  >0  位掩码：ST_WAIT_FD 与 ST_WAIT_NOTIFY 可以同时置位
 *  -1  超时，errno=ETIME；没有协程，errno=EINVAL
 *  -2  没有事件项，errno=EINVAL
 *  -3  Schedule / Add 失败
 * 返回值含 ST_WAIT_FD 时，用一次非阻塞 recv/send 把字节取走。
 * 不要接着再调 st_recv / st_send：那会再进一次 Schedule。
 *
 * 进入时若粘滞位已经是 1：先清掉，再用 poll(timeout=0) 看 fd 是否已经有事件。
 * 不走 Schedule(超时 0)：StEventSchedule::Wait(0) 会 Poll(NULL)，那是一直阻塞。
 */
int st_wait(int fd, int want_read, int timeout_ms);

#endif