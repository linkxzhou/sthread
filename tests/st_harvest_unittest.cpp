#include "app/st_c.h"
#include "src/st_connection.h"
#include "src/st_sys.h"
#include "src/st_thread.h"
#include "stlib/st_buffer.h"
#include "stlib/st_def.h"
#include "stlib/st_hash_list.h"
#include "stlib/st_kqueue.h"
#include "stlib/st_log.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_util.h"
#include "tests/st_test_compat.h"
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

ST_NAMESPACE_USING

class PoolItem {
public:
  PoolItem() : m_v_(0) {}
  void Reset() { m_v_ = 0; }
  int m_v_;
};

class HangItem : public StEventItem {
public:
  virtual int32_t EvHangup() {
    StEventItem::EvHangup();
    return 1;
  }
  virtual int32_t EvInput() {
    StEventItem::EvInput();
    return 1;
  }
  virtual int32_t EvOutput() {
    StEventItem::EvOutput();
    return 1;
  }
};

TEST(StStatus, UtilPrimeAndPoolCap) {
  ASSERT_TRUE(Util::MaxPrimeNum(100) > 0);
  ASSERT_TRUE(Util::MaxPrimeNum(3) > 0);
  UtilPtrPool<PoolItem> pool(1);
  PoolItem *a = pool.AllocPtr();
  PoolItem *b = pool.AllocPtr();
  ASSERT_TRUE(a != NULL && b != NULL);
  pool.FreePtr(a);
  pool.FreePtr(b);
  PoolItem *c = pool.AllocPtr();
  ASSERT_TRUE(c != NULL);
  pool.FreePtr(c);
}

TEST(StStatus, HashManyInsertRemove) {
  StHashList<StNetAddrKey> hl(32);
  for (int i = 0; i < 24; i++) {
    StNetAddr d, s;
    d.SetAddr("10.0.0.1", 1000 + i);
    s.SetAddr("10.0.0.2", 2000 + i);
    StNetAddrKey *k = new StNetAddrKey();
    k->SetDestAddr(d);
    k->SetSrcAddr(s);
    k->SetDataPtr((void *)(uintptr_t)(i + 1));
    ASSERT_TRUE(hl.HashInsert(k) >= 0);
  }
  ASSERT_TRUE(hl.HashSize() == 24);
  (void)hl.HashGetFirst();
  for (int i = 0; i < 24; i++) {
    StNetAddr d, s;
    d.SetAddr("10.0.0.1", 1000 + i);
    s.SetAddr("10.0.0.2", 2000 + i);
    StNetAddrKey probe;
    probe.SetDestAddr(d);
    probe.SetSrcAddr(s);
    hl.HashRemove(&probe);
  }
  ASSERT_TRUE(hl.HashSize() == 0);
}

TEST(StStatus, BufferOversizeAndPool) {
  StBuffer small(8);
  char big[64];
  memset(big, 'a', sizeof(big));
  (void)small.SetBuffer(big, sizeof(big));
  StBufferPool *pool = Instance<StBufferPool>();
  pool->SetMaxFreeNum(1);
  StBuffer *b1 = pool->GetBuffer(16);
  StBuffer *b2 = pool->GetBuffer(16);
  StBuffer *b3 = pool->GetBuffer(32);
  ASSERT_TRUE(b1 && b2 && b3);
  pool->FreeBuffer(b1);
  pool->FreeBuffer(b2);
  pool->FreeBuffer(b3);
}

TEST(StStatus, KqueueLifecycle) {
  StIOState io;
  ASSERT_TRUE(io.Create(32) == ST_OK);
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);
  ASSERT_TRUE(io.AddEvent(fds[0], ST_READABLE) == ST_OK);
  ASSERT_TRUE(io.AddEvent(fds[0], ST_READABLE | ST_WRITEABLE) == ST_OK ||
              io.AddEvent(fds[0], ST_READABLE) == ST_OK);
  (void)io.DelEvent(fds[0], ST_WRITEABLE);
  (void)io.DelEvent(fds[0], ST_READABLE);
  close(fds[0]);
  close(fds[1]);
  io.Free();
}

TEST(StStatus, EventCallbacksAndQueue) {
  HangItem a;
  HangItem b;
  a.SetOsfd(100);
  b.SetOsfd(101);
  a.EnableInput();
  b.EnableOutput();
  a.DisableInput();
  b.DisableOutput();
  a.EvHangup();
  a.EvInput();
  a.EvOutput();
  /* Queue concat without registering live fds into the global scheduler
   * (avoids fd slot conflicts with later socket tests). */
  StEventItemQueue q;
  CPP_TAILQ_INIT(&q);
  CPP_TAILQ_INSERT_TAIL(&q, &a, m_next_);
  CPP_TAILQ_INSERT_TAIL(&q, &b, m_next_);
  ASSERT_TRUE(!CPP_TAILQ_EMPTY(&q));
}

TEST(StStatus, PeerClosePaths) {
  int port = 19201;
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd, 1);
    int c = accept(fd, NULL, NULL);
    if (c >= 0) {
      close(c);
    }
    close(fd);
    _exit(0);
  }
  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  StClientConnection<StEventItem> *conn = new StClientConnection<StEventItem>();
  conn->SetConnType(eTCP_CONN);
  conn->SetTimeout(1000);
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", port);
  int fd = conn->Create(addr);
  ASSERT_TRUE(fd >= 0);
  char buf[16];
  int n = st_recv(fd, buf, sizeof(buf), 0, 1000);
  ASSERT_TRUE(n == 0 || n < 0);
  (void)st_read(fd, buf, sizeof(buf), 200);
  (void)st_write(fd, "x", 1, 200);
  (void)st_send(fd, "x", 1, 0, 200);
  conn->Close();
  delete conn;
  int st = 0;
  waitpid(pid, &st, 0);
}

TEST(StStatus, LogInitNull) {
  StLogger::Instance().Init(LLOG_ERR, NULL);
  LOG_ERROR("harvest-error");
  StLogger::Instance().SetLevel(LLOG_PVERB);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
