#include "src/st_thread.h"
#include "src/st_sys.h"
#include "app/st_c.h"
#include "stlib/st_closure.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

static volatile int g_ran = 0;

static void worker(int n) { g_ran = n; }

TEST(StStatus, SchedulerQueues) {
  StThreadSchedule *ss = GlobalThreadSchedule();
  StThread *t = ss->AllocThread();
  ASSERT_TRUE(t != NULL);
  ASSERT_TRUE(ss->InsertRunable(t) >= 0);
  ASSERT_TRUE(ss->RemoveRunable(t) >= 0);
  ASSERT_TRUE(ss->InsertIOWait(t) >= 0);
  ASSERT_TRUE(ss->IOWaitToRunable(t) >= 0);
  ASSERT_TRUE(ss->RemoveRunable(t) >= 0);
  ASSERT_TRUE(ss->Pend(t) >= 0);
  ASSERT_TRUE(ss->Unpend(t) >= 0);
  t->SetWakeupTime(Util::TimeMs() + 100);
  ASSERT_TRUE(ss->InsertSleep(t) >= 0);
  ss->Wakeup(Util::TimeMs() + 1000);
  ss->RemoveSleep(t);
  UtilPtrPoolFree(t);
}

TEST(StStatus, CreateThreadAndEvent) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  g_ran = 0;
  StSysSchedule *sched = Instance<StSysSchedule>();
  StThread *t = sched->CreateThread(NewStClosure(worker, 7), true);
  ASSERT_TRUE(t != NULL);

  StEventSchedule *es = GlobalEventSchedule();
  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ASSERT_TRUE(item != NULL);
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);
  item->SetOsfd(fds[0]);
  item->EnableInput();
  item->DisableOutput();
  ASSERT_TRUE(es->Add(item));
  es->AddFd(fds[0], ST_READABLE);
  es->Wait(5);
  ASSERT_TRUE(es->Delete(item));
  es->DeleteFd(fds[0], ST_READABLE);
  close(fds[0]);
  close(fds[1]);
  UtilPtrPoolFree(item);

  sched->CheckExpired();
  (void)sched->GetTimeout();
  sched->UpdateLastClock();
}

TEST(StStatus, EventItemApis) {
  StEventItem item;
  item.SetOsfd(3);
  ASSERT_TRUE(item.GetOsfd() == 3);
  item.EnableInput();
  item.EnableOutput();
  item.DisableInput();
  item.DisableOutput();
  item.SetOwnerThread(NULL);
  ASSERT_TRUE(item.GetOwnerStThread() == NULL);
  item.SetRecvEvents(ST_READABLE);
  ASSERT_TRUE(item.GetRecvEvents() != 0);
  item.SetEvents(ST_WRITEABLE);
  ASSERT_TRUE(item.GetEvents() != 0);
  item.Reset();
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
