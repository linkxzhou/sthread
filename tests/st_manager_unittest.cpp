#include "src/st_sys.h"
#include "app/st_c.h"
#include "stlib/st_closure.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

static void idle_callback(StSysSchedule *sched, int i) {
  (void)sched;
  LOG_TRACE("idle callback i=%d", i);
}

TEST(StStatus, schedule_init) {
  bool ok = st_init_frame();
  ASSERT_TRUE(ok);
  st_set_hook_flag();
  StSysSchedule *sched = Instance<StSysSchedule>();
  LOG_ASSERT(sched != NULL);
  /* CreateThread inserts runable; without arm64 context switch we only
   * verify the API returns non-NULL. */
  StThread *t = sched->CreateThread(NewStClosure(idle_callback, sched, 1), true);
  LOG_TRACE("created thread: %p", t);
  ASSERT_TRUE(t != NULL);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
