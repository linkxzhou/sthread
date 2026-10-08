/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 最短的 TCP echo。用 eTCP_KEEPLIVE_CONN 只为了 CallBack 能多转几圈把一行收齐。
 * FreePtr 仍然关掉 fd，这不是连接池。
 */

#include "app/st_c.h"
#include "src/st_server.h"
#include "stlib/st_log.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace sthread;
using namespace stlib;

class EchoConn : public StServerConnection<EchoConn> {
public:
  EchoConn() : line_len_(0), have_line_(0) { line_[0] = '\0'; }

  virtual int32_t DoInput(void *buf, int32_t len) {
    char *p;
    int32_t i;
    int32_t n;
    if (buf == NULL || len < 0) {
      return -1;
    }
    p = (char *)buf;
    for (i = 0; i < len; i++) {
      if (p[i] == '\n') {
        break;
      }
    }
    if (i == len) {
      if (len >= (int32_t)ST_RECV_BUFFSIZE) {
        return -1;
      }
      return 0;
    }
    n = i + 1;
    if (n >= (int32_t)sizeof(line_)) {
      return -1;
    }
    memcpy(line_, p, (size_t)n);
    line_[n] = '\0';
    line_len_ = n;
    have_line_ = 1;
    /* \n 后面的字节会被整段清掉。协议因此是「一次只保证一行」。 */
    return ST_CONN_RESET_RECVBUF;
  }

  virtual int32_t DoProcess() {
    if (GetSendBuffer() != NULL) {
      GetSendBuffer()->SetHaveSendLen(0);
    }
    return 0;
  }

  virtual int32_t DoOutput(void *buf, int32_t &len) {
    if (!have_line_) {
      len = 0;
      return 0;
    }
    if (line_len_ > len) {
      return -1;
    }
    memcpy(buf, line_, (size_t)line_len_);
    len = line_len_;
    have_line_ = 0;
    line_len_ = 0;
    return 0;
  }

  virtual int32_t DoError(int32_t err) {
    LOG_ERROR("echo conn error: %d", err);
    return 0;
  }

private:
  char line_[ST_RECV_BUFFSIZE];
  int line_len_;
  int have_line_;
};

int main(int argc, char *argv[]) {
  const char *ip = "0.0.0.0";
  int port = 7707;
  StServer<EchoConn, eTCP_KEEPLIVE_CONN> *server;
  StNetAddr addr;
  int fd;

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

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  server = new StServer<EchoConn, eTCP_KEEPLIVE_CONN>();
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
  server->Loop();
  return 0;
}
