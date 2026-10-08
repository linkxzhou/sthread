/*
 * 调度器边界：堆上的唤醒顺序不依赖墙钟；一批已就绪协程在一次短 sleep 内跑完。
 */
#include "app/st_c.h"
#include "src/st_sys.h"
#include "src/st_thread.h"
#include "stlib/st_closure.h"
#include "tests/st_test_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static long self_vmsize_kb(void) {
  FILE *fp;
  char line[256];
  long kb = -1;

  fp = fopen("/proc/self/status", "r");
  if (fp == NULL) {
    return -1;
  }
  while (fgets(line, sizeof(line), fp) != NULL) {
    if (strncmp(line, "VmSize:", 7) == 0) {
      kb = atol(line + 7);
      break;
    }
  }
  fclose(fp);
  return kb;
}

/* 完成数远大于批大小时，池里的活对象不能跟着完成数涨。
 * 泄漏一块栈就会让 VmSize 超过下面的上限。 */
TEST(StStatus, StackReuseDoesNotGrowWithCompletions) {
  const int batch = 32;
  const int rounds = 64;
  int i;
  int r;
  uint32_t before;
  uint32_t after;
  long vm0;
  long vm1;

  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  g_ran = 0;
  before = Instance<UtilPtrPool<StThread> >()->Size();
  vm0 = self_vmsize_kb();
  for (r = 0; r < rounds; r++) {
    for (i = 0; i < batch; i++) {
      StThread *t =
          GlobalThreadSchedule()->CreateThread(NewStClosure(bump), true);
      ASSERT_TRUE(t != NULL);
    }
    st_sleep(1);
  }
  ASSERT_TRUE(g_ran == batch * rounds);
  after = Instance<UtilPtrPool<StThread> >()->Size();
  ASSERT_TRUE(after <= before + (uint32_t)batch);
  vm1 = self_vmsize_kb();
  if (vm0 >= 0 && vm1 >= 0) {
    ASSERT_TRUE(vm1 - vm0 < 16 * 1024);
  }
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
