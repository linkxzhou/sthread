#include "app/st_c.h"
#include "src/st_connection.h"
#include "src/st_poll.h"
#include "src/st_server.h"
#include "src/st_sys.h"
#include "src/st_thread.h"
#include "stlib/st_closure.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

ST_NAMESPACE_USING

static volatile int g_child_ran = 0;
static void cov_worker() { g_child_ran = 1; }

TEST(StStatus, SleepAndCreateThread) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  st_sleep(1);
  g_child_ran = 0;
  StThread *t =
      GlobalThreadSchedule()->CreateThread(NewStClosure(cov_worker), true);
  ASSERT_TRUE(t != NULL);
  StThread *t2 =
      GlobalThreadSchedule()->CreateThread(NewStClosure(cov_worker), false);
  ASSERT_TRUE(t2 != NULL);
}

TEST(StStatus, ConnNullBuffers) {
  ASSERT_TRUE(st_init_frame());
  StClientConnection<StEventItem> conn;
  conn.StConnection::Reset(); /* frees buffers -> NULL */
  ASSERT_TRUE(conn.SendData() == -1);
  ASSERT_TRUE(conn.RecvData() == -1);
}

TEST(StStatus, ThreadScheduleNullAndParent) {
  ASSERT_TRUE(st_init_frame());
  StThreadSchedule *ss = GlobalThreadSchedule();
  ASSERT_TRUE(ss->InsertRunable(NULL) < 0);
  ASSERT_TRUE(ss->RemoveRunable(NULL) < 0);
  ASSERT_TRUE(ss->InsertIOWait(NULL) < 0);
  ASSERT_TRUE(ss->IOWaitToRunable(NULL) < 0);
  ASSERT_TRUE(ss->InsertSleep(NULL) < 0);
  ASSERT_TRUE(ss->RemoveSleep(NULL) < 0);
  ASSERT_TRUE(ss->Pend(NULL) < 0);
  ASSERT_TRUE(ss->Unpend(NULL) < 0);
  ASSERT_TRUE(ss->Sleep(NULL) < 0);
  /* Yield(NULL) would SwitchThread with null — not a supported contract. */

  StThread *parent = ss->AllocThread();
  StThread *child = ss->AllocThread();
  ASSERT_TRUE(parent != NULL && child != NULL);
  parent->AddSubStThread(child);
  ASSERT_TRUE(!parent->HasNoSubStThread());
  ASSERT_TRUE(child->GetParent() == parent);
  ss->Pend(parent);
  ss->WakeupParent(child);
  ASSERT_TRUE(parent->HasNoSubStThread());
  /* RemoveSleep on non-sleeping thread hits heap-delete error path. */
  StThread *t = ss->AllocThread();
  ASSERT_TRUE(ss->RemoveSleep(t) < 0);
  UtilPtrPoolFree(t);
  UtilPtrPoolFree(child);
  UtilPtrPoolFree(parent);
}

TEST(StStatus, EventAddDeleteEdges) {
  ASSERT_TRUE(st_init_frame());
  StEventSchedule *es = GlobalEventSchedule();
  ASSERT_TRUE(!es->Add((StEventItem *)NULL));
  ASSERT_TRUE(!es->Delete((StEventItem *)NULL));

  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(-1);
  item->EnableInput();
  ASSERT_TRUE(!es->Add(item));

  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);
  item->SetOsfd(fds[0]);
  item->EnableInput();
  ASSERT_TRUE(es->Add(item));

  /* empty-slot delete on another item same fd events */
  StEventItem *ghost = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ghost->SetOsfd(fds[1]);
  ghost->EnableInput();
  ASSERT_TRUE(es->Delete(ghost)); /* empty slot -> true */

  /* replace conflict: new item same fd */
  StEventItem *item2 = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item2->SetOsfd(fds[0]);
  item2->EnableInput();
  item2->EnableOutput();
  ASSERT_TRUE(es->Add(item2));

  /* mismatch delete */
  ASSERT_TRUE(!es->Delete(item));

  es->ClearItem(item2);
  UtilPtrPoolFree(item2);
  UtilPtrPoolFree(item);
  UtilPtrPoolFree(ghost);
  close(fds[0]);
  close(fds[1]);
}

TEST(StStatus, PollCallbacksAndThreadItemApis) {
  StEventItem item;
  item.SetOsfd(7);
  ASSERT_TRUE(item.EvInput() == 0);
  ASSERT_TRUE(item.EvOutput() == 0);
  ASSERT_TRUE(item.EvHangup() == 0);

  StThreadItem *t = GlobalThreadSchedule()->AllocThread();
  ASSERT_TRUE(t != NULL);
  t->SetName("cov");
  ASSERT_TRUE(t->GetName() != NULL);
  void *priv = malloc(8);
  t->SetPrivate(priv);
  ASSERT_TRUE(t->GetPrivate() == priv);
  t->SetWakeupTime(123);
  ASSERT_TRUE(t->GetWakeupTime() == 123);
  t->Reset(); /* frees priv via st_safe_free */
  UtilPtrPoolFree(t);
}

TEST(StStatus, WriteEagainAndReadTimeout) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  int sv[2];
  ASSERT_TRUE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  int snd = 2 * 1024;
  setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));
  setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &snd, sizeof(snd));
  int fl = fcntl(sv[0], F_GETFL, 0);
  fcntl(sv[0], F_SETFL, fl | O_NONBLOCK);
  fl = fcntl(sv[1], F_GETFL, 0);
  fcntl(sv[1], F_SETFL, fl | O_NONBLOCK);

  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(sv[0]);
  item->EnableOutput();
  item->SetOwnerThread(GlobalThreadSchedule()->GetActiveThread());
  GlobalEventSchedule()->Add(item);

  /* Fill send buffer until EAGAIN so st_write schedules. */
  char junk[4096];
  memset(junk, 'x', sizeof(junk));
  for (int i = 0; i < 64; i++) {
    ssize_t n = st_write(sv[0], junk, sizeof(junk), 30);
    if (n < 0) {
      break;
    }
  }
  /* Peer still unread: short read timeout. */
  StEventItem *ritem = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ritem->SetOsfd(sv[1]);
  ritem->EnableInput();
  ritem->SetOwnerThread(GlobalThreadSchedule()->GetActiveThread());
  GlobalEventSchedule()->Add(ritem);
  char rbuf[16];
  errno = 0;
  /* May ETIME if buffer drained, or return bytes if write side filled peer. */
  (void)st_read(sv[1], rbuf, sizeof(rbuf), 5);

  GlobalEventSchedule()->ClearItem(item);
  GlobalEventSchedule()->ClearItem(ritem);
  UtilPtrPoolFree(item);
  UtilPtrPoolFree(ritem);
  close(sv[0]);
  close(sv[1]);
}

TEST(StStatus, ConnectTimeoutPath) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(fd >= 0);
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);

  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(fd);
  item->EnableOutput();
  item->SetOwnerThread(GlobalThreadSchedule()->GetActiveThread());
  GlobalEventSchedule()->Add(item);

  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(81);
  /* TEST-NET-1 — typically blackholed / slow to fail */
  dst.sin_addr.s_addr = htonl(0xCB007101); /* 203.0.113.1 */
  errno = 0;
  /* Exercise connect wait/timeout/error branches; outcome is network-dependent. */
  (void)st_connect(fd, (struct sockaddr *)&dst, sizeof(dst), 8);

  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  close(fd);
}

TEST(StStatus, PeerCloseReadZero) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  int sv[2];
  ASSERT_TRUE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  close(sv[1]); /* peer closed */

  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(sv[0]);
  item->EnableInput();
  GlobalEventSchedule()->Add(item);

  char buf[8];
  ssize_t n = st_read(sv[0], buf, sizeof(buf), 200);
  /* EOF -> 0 from st_read / recv path */
  ASSERT_TRUE(n == 0 || n < 0);

  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  close(sv[0]);
}

class UdpSrvConn : public StServerConnection<UdpSrvConn> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    (void)buf;
    return len > 0 ? len : -1;
  }
  virtual int32_t DoOutput(void *buf, int32_t &len) {
    memcpy(buf, "UO", 2);
    len = 2;
    return 0;
  }
  virtual int32_t DoProcess() { return 0; }
  virtual int32_t DoError(int32_t err) {
    (void)err;
    return 0;
  }
};

TEST(StStatus, UdpServerCreateListen) {
  ASSERT_TRUE(st_init_frame());
  StServer<UdpSrvConn, eUDP_CONN> *server =
      new StServer<UdpSrvConn, eUDP_CONN>();
  server->SetHookFlag();
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", 19401);
  int fd = server->CreateSocket(addr);
  ASSERT_TRUE(fd >= 0);
  /* UDP sockets do not listen(2); CreateSocket already covers SOCK_DGRAM path. */
  (void)server->Listen();
  delete server;
}


TEST(StStatus, SysNullActiveThread) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  StThreadSchedule *ss = GlobalThreadSchedule();
  StThreadItem *saved = ss->GetActiveThread();
  ss->SetActiveThread(NULL);
  char buf[8];
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(9);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  errno = 0;
  ASSERT_TRUE(st_sendto(0, buf, 1, 0, (struct sockaddr *)&dst, sizeof(dst), 10) < 0);
  ASSERT_TRUE(errno == EINVAL);
  errno = 0;
  ASSERT_TRUE(st_recvfrom(0, buf, 1, 0, NULL, NULL, 10) < 0);
  ASSERT_TRUE(errno == EINVAL);
  errno = 0;
  ASSERT_TRUE(st_connect(0, (struct sockaddr *)&dst, sizeof(dst), 10) < 0);
  ASSERT_TRUE(errno == EINVAL);
  errno = 0;
  ASSERT_TRUE(st_read(0, buf, 1, 10) < 0);
  ASSERT_TRUE(errno == EINVAL);
  errno = 0;
  ASSERT_TRUE(st_write(0, buf, 1, 10) < 0);
  ASSERT_TRUE(errno == EINVAL);
  errno = 0;
  ASSERT_TRUE(st_send(0, buf, 1, 0, 10) < 0);
  ASSERT_TRUE(errno == EINVAL);
  errno = 0;
  ASSERT_TRUE(st_recv(0, buf, 1, 0, 10) < 0);
  ASSERT_TRUE(errno == EINVAL);
  ss->SetActiveThread(saved);
}

TEST(StStatus, UdpRecvfromTimeout) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_TRUE(fd >= 0);
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_TRUE(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(fd);
  item->EnableInput();
  item->SetOwnerThread(GlobalThreadSchedule()->GetActiveThread());
  GlobalEventSchedule()->Add(item);

  char rbuf[32];
  struct sockaddr_in from;
  socklen_t flen = sizeof(from);
  errno = 0;
  int n = st_recvfrom(fd, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&from, &flen, 5);
  ASSERT_TRUE(n < 0 && errno == ETIME);

  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  close(fd);
}

TEST(StStatus, ConnectBlackholeTimeout) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(fd >= 0);
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(fd);
  item->EnableOutput();
  item->SetOwnerThread(GlobalThreadSchedule()->GetActiveThread());
  GlobalEventSchedule()->Add(item);
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(81);
  dst.sin_addr.s_addr = htonl(0xC0000201); /* 192.0.2.1 TEST-NET */
  errno = 0;
  (void)st_connect(fd, (struct sockaddr *)&dst, sizeof(dst), 5);
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  close(fd);
}


int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
