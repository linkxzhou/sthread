/*
 * 调度器边界：堆上的唤醒顺序不依赖墙钟；一批已就绪协程在一次短 sleep 内跑完。
 */
#include "app/st_c.h"
#include "src/st_sys.h"
#include "src/st_thread.h"
#include "stlib/st_closure.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

static volatile int g_ran = 0;

static void bump(void) { g_ran++; }

TEST(StStatus, SleepHeapOrder) {
  StThreadSchedule *ss;
  StThread *early;
  StThread *mid;
  StThread *late;
  StThreadItem *got;

  ASSERT_TRUE(st_init_frame());
  ss = GlobalThreadSchedule();
  early = ss->AllocThread();
  mid = ss->AllocThread();
  late = ss->AllocThread();
  ASSERT_TRUE(early != NULL && mid != NULL && late != NULL);

  early->SetWakeupTime(10);
  mid->SetWakeupTime(20);
  late->SetWakeupTime(30);
  ASSERT_TRUE(ss->InsertSleep(late) >= 0);
  ASSERT_TRUE(ss->InsertSleep(early) >= 0);
  ASSERT_TRUE(ss->InsertSleep(mid) >= 0);

  /* PopRunable 在空队列上会解引用空节点，这里每次只弹出已知的那一个。 */
  ss->Wakeup(15);
  got = ss->PopRunable();
  ASSERT_TRUE(got == early);
  ASSERT_TRUE(mid->HasFlag(eSLEEP_LIST));
  ASSERT_TRUE(late->HasFlag(eSLEEP_LIST));

  ss->Wakeup(20);
  got = ss->PopRunable();
  ASSERT_TRUE(got == mid);
  ASSERT_TRUE(late->HasFlag(eSLEEP_LIST));

  ss->Wakeup(29);
  ASSERT_TRUE(late->HasFlag(eSLEEP_LIST));
  ss->Wakeup(30);
  ASSERT_TRUE(!late->HasFlag(eSLEEP_LIST));
  got = ss->PopRunable();
  ASSERT_TRUE(got == late);
}

TEST(StStatus, ManyReadyCoroutines) {
  const int n = 24;
  int i;
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  g_ran = 0;
  for (i = 0; i < n; i++) {
    StThread *t =
        GlobalThreadSchedule()->CreateThread(NewStClosure(bump), true);
    ASSERT_TRUE(t != NULL);
  }
  /* 让出后 daemon 会先把就绪队列跑完，再等这次 sleep 到期。 */
  st_sleep(30);
  ASSERT_TRUE(g_ran == n);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
