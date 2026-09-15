/*
 * Copyright (C) zhoulv2000@163.com
 *
 * Thin Frame facade over StSysSchedule (plan/04 app compatibility).
 */

#ifndef _ST_FRAME_H_
#define _ST_FRAME_H_

#include "app/st_c.h"
#include "src/st_sys.h"
#include "stlib/st_closure.h"
#include "stlib/st_singleton.h"

using namespace sthread;

class Frame {
public:
  Frame() : m_wait_num_(0) {}

  static StThread *CreateThread(void (*entry)(void *), void *args,
                                bool runable = true) {
    StSysSchedule *sched = Instance<StSysSchedule>();
    return sched->CreateThread(NewStClosure(entry, args), runable);
  }

  /* Historical Loop(true): drive daemon. On platforms without real
   * context switch (arm64 stub) this will not schedule user threads. */
  static void Loop(bool /*exit_when_idle*/ = false) {
    StSysSchedule *sched = Instance<StSysSchedule>();
    StSysSchedule::StartUp(sched);
  }

  int m_wait_num_;
};

#endif
