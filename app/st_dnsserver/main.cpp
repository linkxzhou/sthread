/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 最小权威 DNS 样例：StServer 绑 UDP，本进程循环 recvfrom/应答。
 * 默认 0.0.0.0:5353；A 记录仅 *.bench.local / *.bench.sthread.local → 127.0.0.1
 * 非 A：空 ANSWER + NOERROR（plan/08 D2=b）。
 *
 * Build:  make -C app/st_dnsserver
 * Run:    ./main [bind_ip] [port]
 */

#include "app/st_c.h"
#include "app/st_dns/dns_proto.h"
#include "src/st_public.h"
#include "src/st_server.h"
#include "src/st_sys.h"
#include "stlib/st_log.h"
#include "stlib/st_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace sthread;
using namespace stlib;

/* StServer 模板需要一个 ConnectionT；UDP 不走 accept/Loop，仅占位 */
class DnsConn : public StServerConnection<DnsConn> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    (void)buf;
    return len > 0 ? len : -1;
  }
  virtual int32_t DoOutput(void *buf, int32_t &len) {
    (void)buf;
    (void)len;
    return 0;
  }
  virtual int32_t DoProcess() { return 0; }
  virtual int32_t DoError(int32_t err) {
    (void)err;
    return 0;
  }
};

int main(int argc, char *argv[]) {
  const char *bind_ip = "0.0.0.0";
  int port = 5353;

  if (argc >= 2 && argv[1][0] != '\0') {
    bind_ip = argv[1];
  }
  if (argc >= 3) {
    port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
      fprintf(stderr, "usage: %s [bind_ip] [port]\n", argv[0]);
      return 1;
    }
  }

  LOG_LEVEL(LLOG_ERR);

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  /* 用 StServer 走 SOCK_DGRAM + bind + 事件注册（与 UdpSrvConn 单测对齐）。
   * UDP 无 accept，故不用 Listen/Loop，改为 st_recvfrom 循环。 */
  StServer<DnsConn, eUDP_CONN> *server = new StServer<DnsConn, eUDP_CONN>();
  server->SetHookFlag();

  StNetAddr addr;
  addr.SetAddr(bind_ip, (uint16_t)port);
  int fd = server->CreateSocket(addr);
  if (fd < 0) {
    fprintf(stderr, "CreateSocket failed: %d\n", fd);
    return 1;
  }
  /* CreateSocket 默认 EnableOutput；UDP 常可写会导致 st_recvfrom 空转。 */
  {
    StEventItem *item = GlobalEventSchedule()->GetEventItem(fd);
    if (item != NULL) {
      item->DisableOutput();
      item->EnableInput();
      GlobalEventSchedule()->Add(item);
    }
  }

  printf("sthread dns server listening on udp://%s:%d/\n", bind_ip, port);
  printf("zone: *.bench.local / *.bench.sthread.local -> 127.0.0.1 (A)\n");
  fflush(stdout);

  for (;;) {
    char qbuf[ST_DNS_MAX_PKT];
    char rbuf[ST_DNS_MAX_PKT];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    memset(&from, 0, sizeof(from));
    int n = st_recvfrom(fd, qbuf, (int)sizeof(qbuf), 0,
                        (struct sockaddr *)&from, &fromlen, -1);
    if (n <= 0) {
      continue;
    }
    int rlen = st_dns_build_reply(qbuf, n, rbuf, (int)sizeof(rbuf));
    if (rlen <= 0) {
      continue; /* 坏包丢弃 */
    }
    (void)st_sendto(fd, rbuf, rlen, 0, (struct sockaddr *)&from, (int)fromlen,
                    1000);
  }

  return 0;
}
