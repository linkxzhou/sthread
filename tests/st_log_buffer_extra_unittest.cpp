#include "app/st_c.h"
#include "src/st_sys.h"
#include "src/st_thread.h"
#include "stlib/st_buffer.h"
#include "stlib/st_log.h"
#include "stlib/st_util.h"
#include "tests/st_test_compat.h"
#include <stdio.h>
#include <unistd.h>

ST_NAMESPACE_USING

TEST(StStatus, LogInitFileAndLevels) {
  char path[256];
  snprintf(path, sizeof(path), "/tmp/sthread_cov_log_%d.txt", (int)getpid());
  ASSERT_TRUE(StLogger::Instance().Init(LLOG_DEBUG, path) == 0);
  LOG_DEBUG("file-debug");
  LOG_WARN("file-warn");
  LOG_ERROR("file-error");
  StLogger::Instance().SetLevel(LLOG_EMERG);
  LOG_DEBUG("should-skip");
  StLogger::Instance().SetLevel(LLOG_PVERB);
  unlink(path);
}

TEST(StStatus, BufferBucketPoolEdges) {
  StBufferPool *pool = Instance<StBufferPool>();
  pool->SetMaxFreeNum(2);
  StBuffer *a = pool->GetBuffer(32);
  StBuffer *b = pool->GetBuffer(32);
  StBuffer *c = pool->GetBuffer(64);
  ASSERT_TRUE(a && b && c);
  a->Reset();
  char big[40];
  memset(big, 'x', sizeof(big));
  /* SetBuffer longer than maxlen should fail or truncate depending on impl */
  (void)a->SetBuffer(big, sizeof(big));
  pool->FreeBuffer(a);
  pool->FreeBuffer(b);
  pool->FreeBuffer(c);
  StBuffer *d = pool->GetBuffer(32);
  ASSERT_TRUE(d != NULL);
  pool->FreeBuffer(d);

  StBuffer stack(16);
  stack.Reset();
  ASSERT_TRUE(stack.GetBuffer() != NULL);
}

TEST(StStatus, StReadSmoke) {
  /* Replaces WaitEvents smoke (A2/D2): exercise st_read path instead. */
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);
  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(fds[0]);
  item->EnableInput();
  GlobalEventSchedule()->Add(item);
  ASSERT_TRUE(write(fds[1], "1", 1) == 1);
  char buf[8];
  ssize_t n = st_read(fds[0], buf, sizeof(buf), 200);
  ASSERT_TRUE(n == 1);
  ASSERT_TRUE(buf[0] == '1');
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  close(fds[0]);
  close(fds[1]);
}

TEST(StStatus, LogReopenAndHelpers) {
  char path[256];
  snprintf(path, sizeof(path), "/tmp/sthread_log_reopen_%d.txt", (int)getpid());
  ASSERT_TRUE(StLogger::Instance().Init(LLOG_DEBUG, path) == 0);
  LOG_DEBUG("before-reopen");
  StLogger::Instance().Reopen();
  LOG_WARN("after-reopen");
  ASSERT_TRUE(StLogger::Instance().LogAble(LLOG_DEBUG) == 1);
  StLogger::Instance().SetLevel(LLOG_EMERG);
  ASSERT_TRUE(StLogger::Instance().LogAble(LLOG_DEBUG) == 0);
  StLogger::Instance().SetLevel(LLOG_PVERB);
  ASSERT_TRUE(StLogger::Instance().StringLastOf("a/b/c", '/') == 3);
  ASSERT_TRUE(StLogger::Instance().StringLastOf(NULL, 'x') == -1);
  unlink(path);
  /* empty name -> stderr */
  ASSERT_TRUE(StLogger::Instance().Init(LLOG_INFO, (char *)"") == 0);
}

TEST(StStatus, AnyCastAndReferenceable) {
  int x = 42;
  int *px = any_cast<int>(&x);
  ASSERT_TRUE(px != NULL && *px == 42);
  void *vp = &x;
  ASSERT_TRUE(any_cast<int>(vp) == px);

  class R : public referenceable {};
  R r;
  r.incrref();
  ASSERT_TRUE(r.getref() >= 1);
  r.decref();
  r.Reset();
  ASSERT_TRUE(r.getref() == 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
