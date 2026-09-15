/*
 * Minimal HTTP/1.1 server sample on top of StServer.
 *
 * Build:  make -C app/st_httpserver
 * Run:    ./main [port]          # default 8765
 * Probe:  curl -v http://127.0.0.1:8765/
 * Bench:  ../st_wrk/wrk -n 1000 -c 50 -d 5s http://127.0.0.1:8765/
 */

#include "app/st_c.h"
#include "src/st_server.h"
#include "stlib/st_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace sthread;
using namespace stlib;

class HttpConn : public StServerConnection<HttpConn> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    if (buf == NULL || len <= 0) {
      return -1;
    }
    /* Wait until request headers are complete (simple HTTP/1.x). */
    char *p = (char *)buf;
    for (int32_t i = 0; i + 3 < len; i++) {
      if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' &&
          p[i + 3] == '\n') {
        return i + 4;
      }
    }
    /* Cap runaway clients. */
    if (len >= (int32_t)ST_RECV_BUFFSIZE) {
      return -1;
    }
    return 0;
  }

  virtual int32_t DoOutput(void *buf, int32_t &len) {
    static const char *kBody = "hello from sthread\n";
    int body_len = (int)strlen(kBody);
    int n = snprintf(
        (char *)buf, (size_t)len,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Server: sthread-httpserver/1.0\r\n"
        "\r\n"
        "%s",
        body_len, kBody);
    if (n < 0 || n >= len) {
      return -1;
    }
    len = n;
    return 0;
  }

  virtual int32_t DoProcess() { return 0; }

  virtual int32_t DoError(int32_t err) {
    LOG_ERROR("http conn error: %d", err);
    return 0;
  }
};

int main(int argc, char *argv[]) {
  int port = 8765;
  if (argc >= 2) {
    port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
      fprintf(stderr, "usage: %s [port]\n", argv[0]);
      return 1;
    }
  }

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  StServer<HttpConn, eTCP_CONN> *server =
      new StServer<HttpConn, eTCP_CONN>();
  server->SetHookFlag();

  StNetAddr addr;
  addr.SetAddr("0.0.0.0", port);
  int fd = server->CreateSocket(addr);
  if (fd < 0) {
    fprintf(stderr, "CreateSocket failed: %d\n", fd);
    return 1;
  }
  if (!server->Listen()) {
    fprintf(stderr, "Listen failed on port %d\n", port);
    return 1;
  }

  printf("sthread http server listening on http://0.0.0.0:%d/\n", port);
  printf("try: curl http://127.0.0.1:%d/\n", port);
  server->Loop();
  return 0;
}
