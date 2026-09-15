/*
 * plan/04: map historical test names onto current St* APIs.
 */
#ifndef _ST_TEST_COMPAT_H_
#define _ST_TEST_COMPAT_H_

#include "stlib/st_test.h"
#include "stlib/st_singleton.h"
#include "stlib/st_log.h"
#include "stlib/st_def.h"
#include "stlib/st_netaddr.h"
#include "src/st_public.h"
#include "src/st_thread.h"
#include "src/st_sys.h"

#ifndef ST_NAMESPACE_USING
#define ST_NAMESPACE_USING using namespace sthread; using namespace stlib;
#endif

typedef sthread::StEventItem StEventSuper;
typedef sthread::StThread Thread;
typedef sthread::StThreadSchedule ThreadScheduler;
typedef sthread::StEventSchedule EventScheduler;
typedef sthread::StSysSchedule Manager;

#define GetThreadScheduler() GlobalThreadSchedule()
#define GetEventScheduler() GlobalEventSchedule()

typedef stlib::StNetAddr StNetAddress;

#endif
