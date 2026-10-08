/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 单房间聊天室。不用 StServer::Loop：CallBack 只会
 * RecvData，空闲连接收不到别人的消息。 广播先把一行拷进对方邮箱，再
 * st_notify。不在别人的 socket 上 st_send，也不建 pipe。
 */

#include "app/st_c.h"
#include "app/st_frame.h"
#include "src/st_server.h"
#include "src/st_sys.h"
#include "stlib/st_log.h"
#include "stlib/st_util.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace sthread;
using namespace stlib;

struct Mail {
  char text[8][512];
  int head;
  int n;
};

struct Session {
  int used;
  int fd;
  StThread *self;
  char nick[32];
  Mail mail;
  StEventItem *item;
};

static Session g_sess[64];
static const int kIdleMs = 1000;

static int mail_push(Mail *mail, const char *line) {
  int len;
  int tail;
  if (mail == NULL || line == NULL) {
    return -1;
  }
  len = (int)strlen(line);
  if (len <= 0 || len >= 512 || mail->n >= 8) {
    return -1;
  }
  tail = (mail->head + mail->n) % 8;
  memcpy(mail->text[tail], line, (size_t)len + 1);
  mail->n++;
  return 0;
}

static int mail_pop(Mail *mail, char *out, int cap) {
  int len;
  if (mail == NULL || mail->n <= 0 || cap <= 1) {
    return 0;
  }
  len = (int)strlen(mail->text[mail->head]);
  if (len >= cap) {
    len = cap - 1;
  }
  memcpy(out, mail->text[mail->head], (size_t)len);
  out[len] = '\0';
  mail->head = (mail->head + 1) % 8;
  mail->n--;
  return 1;
}

static int send_all(int fd, const char *buf, int len) {
  int off = 0;
  while (off < len) {
    ssize_t n = st_send(fd, buf + off, (size_t)(len - off), 0, 3000);
    if (n < 0) {
      return -1;
    }
    if (n == 0) {
      errno = EPIPE;
      return -1;
    }
    off += (int)n;
  }
  return 0;
}

/* 只改邮箱和通知位。调用方保证这段不 Yield。 */
static int broadcast(Session *from, const char *line) {
  int i;
  int dropped = 0;
  for (i = 0; i < 64; i++) {
    Session *peer = &g_sess[i];
    if (!peer->used || peer == from || peer->self == NULL) {
      continue;
    }
    if (mail_push(&peer->mail, line) != 0) {
      dropped = 1;
    } else if (st_notify(peer->self) != 0) {
      dropped = 1;
    }
  }
  return dropped;
}

static void drain_mail(Session *s) {
  char line[512];
  while (mail_pop(&s->mail, line, (int)sizeof(line))) {
    int n = (int)strlen(line);
    if (send_all(s->fd, line, n) != 0) {
      return;
    }
  }
}

static void release_session(Session *s) {
  if (s->item != NULL) {
    GlobalEventSchedule()->ClearItem(s->item);
    UtilPtrPoolFree(s->item);
    s->item = NULL;
  }
  if (s->fd >= 0) {
    ::close(s->fd);
    s->fd = -1;
  }
  s->self = NULL;
  s->used = 0;
  s->mail.n = 0;
  s->mail.head = 0;
}

static Session *alloc_session(int fd, StEventItem *item) {
  int i;
  for (i = 0; i < 64; i++) {
    if (!g_sess[i].used) {
      memset(&g_sess[i], 0, sizeof(g_sess[i]));
      g_sess[i].used = 1;
      g_sess[i].fd = fd;
      g_sess[i].item = item;
      g_sess[i].self = NULL;
      snprintf(g_sess[i].nick, sizeof(g_sess[i].nick), "anon");
      return &g_sess[i];
    }
  }
  return NULL;
}

static void set_nick(Session *s, char *acc, int len) {
  if (len > 0 && acc[len - 1] == '\r') {
    len--;
  }
  if (len > 31) {
    len = 31;
  }
  acc[len] = '\0';
  if (len <= 0) {
    snprintf(s->nick, sizeof(s->nick), "anon");
  } else {
    snprintf(s->nick, sizeof(s->nick), "%s", acc);
  }
}

/* 返回 1 表示对端发了 quit。nick 之后的粘包也走这里。 */
static int on_line(Session *s, const char *text) {
  char msg[540];
  if (strcmp(text, "quit") == 0) {
    return 1;
  }
  snprintf(msg, sizeof(msg), "%s: %s\n", s->nick, text);
  if (strlen(msg) >= 512 || broadcast(s, msg)) {
    send_all(s->fd, "* dropped\n", 10);
  }
  return 0;
}

static int feed(Session *s, char *acc, int *acc_n, const char *buf, int n) {
  int i;
  for (i = 0; i < n; i++) {
    if (buf[i] == '\n') {
      int len = *acc_n;
      if (len > 0 && acc[len - 1] == '\r') {
        len--;
      }
      if (len > 500) {
        len = 500;
      }
      acc[len] = '\0';
      if (on_line(s, acc)) {
        return 1;
      }
      *acc_n = 0;
    } else if (*acc_n < 1023) {
      acc[(*acc_n)++] = buf[i];
    } else {
      *acc_n = 0;
    }
  }
  return 0;
}

static int read_nick(Session *s, char *acc, int *acc_n) {
  uint64_t deadline = Util::TimeMs() + 30000;
  while ((int64_t)Util::TimeMs() < (int64_t)deadline) {
    char buf[256];
    int left = (int)(deadline - Util::TimeMs());
    int n;
    int i;
    if (left <= 0) {
      return -1;
    }
    n = st_recv(s->fd, buf, (int)sizeof(buf), 0, left);
    if (n <= 0) {
      return -1;
    }
    for (i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        char leftover[256];
        int rest = n - (i + 1);
        set_nick(s, acc, *acc_n);
        if (rest > 0) {
          memcpy(leftover, buf + i + 1, (size_t)rest);
        }
        *acc_n = 0;
        if (rest > 0 && feed(s, acc, acc_n, leftover, rest)) {
          return 1;
        }
        return 0;
      }
      if (*acc_n < 1023) {
        acc[(*acc_n)++] = buf[i];
      }
    }
  }
  return -1;
}

static void session_main(void *arg) {
  Session *s = (Session *)arg;
  char acc[1024];
  int acc_n = 0;
  char joined[96];
  char left[96];

  s->self = (StThread *)GlobalThreadSchedule()->GetActiveThread();
  acc[0] = '\0';
  if (read_nick(s, acc, &acc_n) != 0) {
    if (s->nick[0] != '\0' && strcmp(s->nick, "anon") != 0) {
      snprintf(left, sizeof(left), "* %s left\n", s->nick);
      broadcast(s, left);
    }
    release_session(s);
    return;
  }
  snprintf(joined, sizeof(joined), "* joined %s\n", s->nick);
  if (send_all(s->fd, joined, (int)strlen(joined)) != 0) {
    release_session(s);
    return;
  }
  snprintf(joined, sizeof(joined), "* %s joined\n", s->nick);
  if (broadcast(s, joined)) {
    send_all(s->fd, "* dropped\n", 10);
  }

  for (;;) {
    int rc;
    drain_mail(s);
    rc = st_wait(s->fd, 1, kIdleMs);
    if (rc == -1 && errno == ETIME) {
      continue;
    }
    if (rc < 0) {
      break;
    }
    if (rc & ST_WAIT_FD) {
      char buf[256];
      ssize_t n = ::recv(s->fd, buf, sizeof(buf), 0);
      if (n == 0) {
        break;
      }
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
          continue;
        }
        break;
      }
      if (feed(s, acc, &acc_n, buf, (int)n)) {
        break;
      }
    }
  }

  snprintf(left, sizeof(left), "* %s left\n", s->nick);
  broadcast(s, left);
  release_session(s);
}

static void accept_loop(int lfd) {
  for (;;) {
    struct sockaddr_in client;
    socklen_t alen = sizeof(client);
    int connfd;
    int flags;
    StEventItem *item;
    Session *s;
    StThread *th;
    memset(&client, 0, sizeof(client));
    connfd = st_accept(lfd, (struct sockaddr *)&client, &alen);
    if (connfd < 0) {
      continue;
    }
    flags = ::fcntl(connfd, F_GETFL, 0);
    if (flags < 0) {
      flags = 0;
    }
    /* 接受出来的 fd 通常是阻塞的。用 ::fcntl，不要 sys_fcntl。 */
    if (::fcntl(connfd, F_SETFL, flags | O_NONBLOCK) != 0) {
      ::close(connfd);
      continue;
    }
    item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
    if (item == NULL) {
      ::close(connfd);
      continue;
    }
    item->SetOsfd(connfd);
    item->DisableInput();
    item->DisableOutput();
    if (!GlobalEventSchedule()->Add(item)) {
      UtilPtrPoolFree(item);
      ::close(connfd);
      continue;
    }
    s = alloc_session(connfd, item);
    if (s == NULL) {
      GlobalEventSchedule()->ClearItem(item);
      UtilPtrPoolFree(item);
      ::close(connfd);
      continue;
    }
    th = Frame::CreateThread(session_main, s);
    if (th == NULL) {
      release_session(s);
      continue;
    }
    s->self = th;
  }
}

int main(int argc, char *argv[]) {
  const char *ip = "0.0.0.0";
  int port = 7700;
  StServer<StServerConnection<StConnection>, eTCP_CONN> *server;
  StNetAddr addr;
  int fd;
  int i;

  signal(SIGPIPE, SIG_IGN);
  LOG_LEVEL(LLOG_CRIT);
  if (argc >= 2) {
    ip = argv[1];
  }
  if (argc >= 3) {
    port = atoi(argv[2]);
  }
  if (port <= 0 || port > 65535) {
    fprintf(stderr, "usage: %s [ip] [port]\n", argv[0]);
    return 2;
  }
  for (i = 0; i < 64; i++) {
    g_sess[i].used = 0;
    g_sess[i].fd = -1;
  }

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  /* 只借用 CreateSocket / Listen。不调用 Loop。 */
  server = new StServer<StServerConnection<StConnection>, eTCP_CONN>();
  server->SetHookFlag();
  addr.SetAddr(ip, (uint16_t)port);
  fd = server->CreateSocket(addr);
  if (fd < 0) {
    fprintf(stderr, "CreateSocket failed: %d\n", fd);
    return 1;
  }
  if (!server->Listen()) {
    fprintf(stderr, "Listen failed on %s:%d\n", ip, port);
    return 1;
  }
  printf("listening %s:%d\n", ip, port);
  fflush(stdout);
  accept_loop(fd);
  return 0;
}
