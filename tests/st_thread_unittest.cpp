#include "src/st_thread.h"
#include "src/st_sys.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

TEST(StStatus, ThreadScheduler) {
  StThreadSchedule *s2 = GlobalThreadSchedule();
  LOG_ASSERT(s2 != NULL);
  StThreadItem *daemon = s2->DaemonThread();
  StThreadItem *primo = s2->PrimoThread();
  LOG_TRACE("daemon: %p, primo: %p", daemon, primo);
  LOG_ASSERT(daemon != NULL);
  LOG_ASSERT(primo != NULL);
}

TEST(StStatus, EventScheduler) {
  StEventSchedule *s3 = GlobalEventSchedule();
  LOG_ASSERT(s3 != NULL);
  s3->Wait(10);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
