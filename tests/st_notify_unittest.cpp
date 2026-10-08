/*
 * Copyright (C) zhoulv2000@163.com
 *
 * st_notify / st_notify_wait / st_wait。同一 OS 线程，不占 fd。
 */

#include "app/st_frame.h"
#include "src/st_sys.h"
#include "stlib/st_util.h"
#include "tests/st_test_compat.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace sthread;
using namespace stlib;

struct NotifyCtx {
  StThread *thread;
  volatile int go;
  volatile int in_wait;
  volatile int done;
  volatile int rc;
  volatile int rc2;
  volatile int elapsed;
  volatile int mask;
  int fd;
  int timeout_ms;
};

static void wait_then_done(void *arg) {
  NotifyCtx *ctx = (NotifyCtx *)arg;
  uint64_t t0;
  ctx->in_wait = 1;
  t0 = Util::TimeMs();
  ctx->rc = st_notify_wait(ctx->timeout_ms);
  ctx->elapsed = (int)(Util::TimeMs() - t0);
  ctx->done = 1;
}

static void notify_when_waiting(void *arg) {
  NotifyCtx *ctx = (NotifyCtx *)arg;
  while (!ctx->in_wait) {
    st_sleep(1);
  }
  st_notify(ctx->thread);
}

static void notify_twice_then_go(void *arg) {
  NotifyCtx *ctx = (NotifyCtx *)arg;
  st_notify(ctx->thread);
  st_notify(ctx->thread);
  ctx->go = 1;
}

static void wait_after_go(void *arg) {
  NotifyCtx *ctx = (NotifyCtx *)arg;
  uint64_t t0;
  while (!ctx->go) {
    st_sleep(1);
  }
  t0 = Util::TimeMs();
  ctx->rc = st_notify_wait(ctx->timeout_ms);
  ctx->elapsed = (int)(Util::TimeMs() - t0);
  ctx->rc2 = st_notify_wait(40);
  ctx->done = 1;
}

static void drive(NotifyCtx *ctx, int budget_ms) {
  int64_t deadline = (int64_t)Util::TimeMs() + budget_ms;
  while (!ctx->done && (int64_t)Util::TimeMs() < deadline) {
    st_sleep(5);
  }
}

TEST(StStatus, NotifyWakesBeforeTimeout) {
  NotifyCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.timeout_ms = 500;
  ASSERT_TRUE(st_init_frame());
  ctx.thread = Frame::CreateThread(wait_then_done, &ctx);
  ASSERT_TRUE(ctx.thread != NULL);
  ASSERT_TRUE(Frame::CreateThread(notify_when_waiting, &ctx) != NULL);
  drive(&ctx, 2000);
  ASSERT_TRUE(ctx.done == 1);
  ASSERT_TRUE(ctx.rc == 0);
  ASSERT_TRUE(ctx.elapsed < 300);
}

TEST(StStatus, NotifyWaitTimesOut) {
  NotifyCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.timeout_ms = 50;
  ASSERT_TRUE(st_init_frame());
  ctx.thread = Frame::CreateThread(wait_then_done, &ctx);
  ASSERT_TRUE(ctx.thread != NULL);
  drive(&ctx, 2000);
  ASSERT_TRUE(ctx.done == 1);
  ASSERT_TRUE(ctx.rc == -1);
  ASSERT_TRUE(errno == ETIME);
  ASSERT_TRUE(ctx.elapsed >= 30);
}

TEST(StStatus, NotifyStickyConsumedOnce) {
  NotifyCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.timeout_ms = 1000;
  ASSERT_TRUE(st_init_frame());
  ctx.thread = Frame::CreateThread(wait_after_go, &ctx);
  ASSERT_TRUE(ctx.thread != NULL);
  ASSERT_TRUE(Frame::CreateThread(notify_twice_then_go, &ctx) != NULL);
  drive(&ctx, 2000);
  ASSERT_TRUE(ctx.done == 1);
  ASSERT_TRUE(ctx.rc == 0);
  ASSERT_TRUE(ctx.elapsed < 200);
  ASSERT_TRUE(ctx.rc2 == -1);
}

TEST(StStatus, NotifyNullIsEinval) {
  ASSERT_TRUE(st_init_frame());
  errno = 0;
  ASSERT_TRUE(st_notify(NULL) == -1);
  ASSERT_TRUE(errno == EINVAL);
  StThread *foreign = new StThread();
  errno = 0;
  ASSERT_TRUE(st_notify(foreign) == -1);
  ASSERT_TRUE(errno == EINVAL);
  delete foreign;
}

static int add_item(int fd) {
  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  if (item == NULL) {
    return -1;
  }
  item->SetOsfd(fd);
  /* 兴趣交给 st_wait。提前打开读兴趣、owner 又是空时，daemon 会忙转。 */
  item->DisableInput();
  item->DisableOutput();
  if (!GlobalEventSchedule()->Add(item)) {
    UtilPtrPoolFree(item);
    return -1;
  }
  return 0;
}

static void drop_item(int fd) {
  StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
  if (item != NULL) {
    GlobalEventSchedule()->ClearItem(item);
    UtilPtrPoolFree(item);
  }
}

struct WaitCtx {
  StThread *thread;
  volatile int in_wait;
  volatile int preloaded;
  volatile int done;
  volatile int rc;
  int wait_fd;
  int poke_fd;
  int timeout_ms;
  int write_byte;
  int do_notify;
};

static void wait_fd(void *arg) {
  WaitCtx *ctx = (WaitCtx *)arg;
  /* 先写入再等：等预载完成，粘滞位和套接字缓冲都已经准备好。 */
  while (ctx->write_byte && !ctx->preloaded) {
    st_sleep(1);
  }
  ctx->in_wait = 1;
  ctx->rc = st_wait(ctx->wait_fd, 1, ctx->timeout_ms);
  ctx->done = 1;
}

static void poke_fd(void *arg) {
  WaitCtx *ctx = (WaitCtx *)arg;
  char b = 'Z';
  /* 不写字节时，等对方进了 st_wait 再通知，覆盖 IO 队列上的 RemoveIOWait。 */
  while (!ctx->write_byte && !ctx->in_wait) {
    st_sleep(1);
  }
  if (ctx->write_byte) {
    (void)::send(ctx->poke_fd, &b, 1, 0);
  }
  if (ctx->do_notify) {
    st_notify(ctx->thread);
  }
  ctx->preloaded = 1;
}

TEST(StStatus, WaitNotifyWithoutFd) {
  int sv[2];
  WaitCtx ctx;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(::fcntl(sv[0], F_SETFL, O_NONBLOCK) == 0);
  ASSERT_TRUE(st_init_frame());
  ASSERT_TRUE(add_item(sv[0]) == 0);
  memset(&ctx, 0, sizeof(ctx));
  ctx.wait_fd = sv[0];
  ctx.poke_fd = sv[1];
  ctx.timeout_ms = 500;
  ctx.write_byte = 0;
  ctx.do_notify = 1;
  ctx.thread = Frame::CreateThread(wait_fd, &ctx);
  ASSERT_TRUE(ctx.thread != NULL);
  ASSERT_TRUE(Frame::CreateThread(poke_fd, &ctx) != NULL);
  {
    int64_t deadline = (int64_t)Util::TimeMs() + 2000;
    while (!ctx.done && (int64_t)Util::TimeMs() < deadline) {
      st_sleep(5);
    }
  }
  ASSERT_TRUE(ctx.done == 1);
  ASSERT_TRUE(ctx.rc == ST_WAIT_NOTIFY);
  ASSERT_TRUE((ctx.rc & ST_WAIT_FD) == 0);
  drop_item(sv[0]);
  ::close(sv[0]);
  ::close(sv[1]);
}

TEST(StStatus, WaitFdAndNotify) {
  int sv[2];
  WaitCtx ctx;
  char buf[4];
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(::fcntl(sv[0], F_SETFL, O_NONBLOCK) == 0);
  ASSERT_TRUE(::fcntl(sv[1], F_SETFL, O_NONBLOCK) == 0);
  ASSERT_TRUE(st_init_frame());
  ASSERT_TRUE(add_item(sv[0]) == 0);
  memset(&ctx, 0, sizeof(ctx));
  ctx.wait_fd = sv[0];
  ctx.poke_fd = sv[1];
  ctx.timeout_ms = 500;
  ctx.write_byte = 1;
  ctx.do_notify = 1;
  ctx.thread = Frame::CreateThread(wait_fd, &ctx);
  ASSERT_TRUE(ctx.thread != NULL);
  ASSERT_TRUE(Frame::CreateThread(poke_fd, &ctx) != NULL);
  {
    int64_t deadline = (int64_t)Util::TimeMs() + 2000;
    while (!ctx.done && (int64_t)Util::TimeMs() < deadline) {
      st_sleep(5);
    }
  }
  ASSERT_TRUE(ctx.done == 1);
  ASSERT_TRUE((ctx.rc & ST_WAIT_FD) != 0);
  ASSERT_TRUE((ctx.rc & ST_WAIT_NOTIFY) != 0);
  memset(buf, 0, sizeof(buf));
  ASSERT_TRUE(::recv(sv[0], buf, 1, 0) == 1);
  ASSERT_TRUE(buf[0] == 'Z');
  drop_item(sv[0]);
  ::close(sv[0]);
  ::close(sv[1]);
}

TEST(StStatus, WaitFdOnly) {
  int sv[2];
  WaitCtx ctx;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(::fcntl(sv[0], F_SETFL, O_NONBLOCK) == 0);
  ASSERT_TRUE(::fcntl(sv[1], F_SETFL, O_NONBLOCK) == 0);
  ASSERT_TRUE(st_init_frame());
  ASSERT_TRUE(add_item(sv[0]) == 0);
  memset(&ctx, 0, sizeof(ctx));
  ctx.wait_fd = sv[0];
  ctx.poke_fd = sv[1];
  ctx.timeout_ms = 500;
  ctx.write_byte = 1;
  ctx.do_notify = 0;
  ctx.thread = Frame::CreateThread(wait_fd, &ctx);
  ASSERT_TRUE(ctx.thread != NULL);
  ASSERT_TRUE(Frame::CreateThread(poke_fd, &ctx) != NULL);
  {
    int64_t deadline = (int64_t)Util::TimeMs() + 2000;
    while (!ctx.done && (int64_t)Util::TimeMs() < deadline) {
      st_sleep(5);
    }
  }
  ASSERT_TRUE(ctx.done == 1);
  ASSERT_TRUE(ctx.rc == ST_WAIT_FD);
  drop_item(sv[0]);
  ::close(sv[0]);
  ::close(sv[1]);
}

TEST(StStatus, WaitWithoutItem) {
  int sv[2];
  int rc;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(st_init_frame());
  errno = 0;
  rc = st_wait(sv[0], 1, 50);
  ASSERT_TRUE(rc == -2);
  ASSERT_TRUE(errno == EINVAL);
  ::close(sv[0]);
  ::close(sv[1]);
}

TEST(StStatus, ReadTimeoutUnaffectedByNotify) {
  int sv[2];
  char buf[8];
  ssize_t n;
  StEventItem *item;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(::fcntl(sv[0], F_SETFL, O_NONBLOCK) == 0);
  ASSERT_TRUE(st_init_frame());
  item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ASSERT_TRUE(item != NULL);
  item->SetOsfd(sv[0]);
  item->EnableInput();
  item->DisableOutput();
  ASSERT_TRUE(GlobalEventSchedule()->Add(item));
  errno = 0;
  n = st_read(sv[0], buf, sizeof(buf), 40);
  ASSERT_TRUE(n == -1);
  ASSERT_TRUE(errno == ETIME);
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  ::close(sv[0]);
  ::close(sv[1]);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
