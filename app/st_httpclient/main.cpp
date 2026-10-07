/*
 * Copyright (C) zhoulv2000@163.com
 *
 * st_httpclient：同步 HTTP/1.1 命令行样例。见 README.md。
 */

#include "app/st_frame.h"
#include "http_client.h"
#include "http_parser.h"
#include "stlib/st_log.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace stlib;

static int g_ok = 0;
static int g_fail = 0;
static int g_2xx = 0;
static int g_other = 0;
static long long g_bytes = 0;
static int g_pending = 0;
static char *g_last_body = NULL;
static int g_last_len = 0;
static int g_total = 0;

struct WorkerArg {
  int index;
  int count;
  int begin;
  const StHttpRequest *req;
  int *lat;
};

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [-X GET|POST] [-d DATA | -D FILE] [-H 'K: V']...\n"
          "          [-c CONC] [-n TOTAL] [-t TIMEOUT_MS] [-k] [-o FILE]\n"
          "          [-q] [-v] [--json] URL\n"
          "exit: 0 all 2xx; 1 failure or non-2xx; 2 bad args/URL; 3 DNS\n",
          argv0);
}

static int copy_field(const char *url, const struct http_parser_url *u, int id,
                      char *out, int outlen) {
  int off;
  int len;
  if ((u->field_set & (1 << id)) == 0) {
    out[0] = '\0';
    return 0;
  }
  off = u->field_data[id].off;
  len = u->field_data[id].len;
  if (len >= outlen) {
    return -1;
  }
  memcpy(out, url + off, (size_t)len);
  out[len] = '\0';
  return 0;
}

/* 0 成功；2 URL 错误；3 DNS 失败。 */
static int resolve_url(const char *url, char *host, int hostlen, char *path,
                       int pathlen, int *port, unsigned int *ip_be) {
  struct http_parser_url u;
  char schema[8];
  char query[2048];
  char portbuf[8];
  struct in_addr ina;
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct sockaddr_in *sin;
  int rc;

  http_parser_url_init(&u);
  if (http_parser_parse_url(url, strlen(url), 0, &u) != 0) {
    fprintf(stderr, "bad url\n");
    return 2;
  }
  if (copy_field(url, &u, UF_SCHEMA, schema, (int)sizeof(schema)) != 0) {
    fprintf(stderr, "bad url scheme\n");
    return 2;
  }
  if (strcmp(schema, "https") == 0) {
    fprintf(stderr, "https is not supported\n");
    return 2;
  }
  if (strcmp(schema, "http") != 0) {
    fprintf(stderr, "only http URLs are supported\n");
    return 2;
  }
  if (copy_field(url, &u, UF_HOST, host, hostlen) != 0 || host[0] == '\0') {
    fprintf(stderr, "missing host\n");
    return 2;
  }
  if ((u.field_set & (1 << UF_PORT)) != 0) {
    *port = (int)u.port;
  } else {
    *port = 80;
  }
  if (copy_field(url, &u, UF_PATH, path, pathlen) != 0) {
    fprintf(stderr, "path too long\n");
    return 2;
  }
  if (path[0] == '\0') {
    if (pathlen < 2) {
      return 2;
    }
    path[0] = '/';
    path[1] = '\0';
  }
  if ((u.field_set & (1 << UF_QUERY)) != 0) {
    if (copy_field(url, &u, UF_QUERY, query, (int)sizeof(query)) != 0) {
      fprintf(stderr, "query too long\n");
      return 2;
    }
    if ((int)strlen(path) + 1 + (int)strlen(query) >= pathlen) {
      fprintf(stderr, "path too long\n");
      return 2;
    }
    strcat(path, "?");
    strcat(path, query);
  }

  if (inet_pton(AF_INET, host, &ina) == 1) {
    *ip_be = ina.s_addr;
    return 0;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(portbuf, sizeof(portbuf), "%d", *port);
  rc = getaddrinfo(host, portbuf, &hints, &res);
  if (rc != 0 || res == NULL || res->ai_addr == NULL) {
    fprintf(stderr, "dns failed for %s\n", host);
    if (res != NULL) {
      freeaddrinfo(res);
    }
    return 3;
  }
  sin = (struct sockaddr_in *)res->ai_addr;
  *ip_be = sin->sin_addr.s_addr;
  freeaddrinfo(res);
  return 0;
}

static char *read_file(const char *path, int *len) {
  FILE *fp;
  long sz;
  char *buf;
  size_t n;
  fp = fopen(path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "cannot read %s\n", path);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  sz = ftell(fp);
  if (sz < 0 || sz > 32 * 1024 * 1024) {
    fclose(fp);
    fprintf(stderr, "file too large\n");
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  buf = (char *)malloc((size_t)sz + 1);
  if (buf == NULL) {
    fclose(fp);
    return NULL;
  }
  n = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  buf[n] = '\0';
  *len = (int)n;
  return buf;
}

static int count_open_fds(void) {
  int n = 0;
  int fd;
  for (fd = 0; fd < 256; fd++) {
    if (fcntl(fd, F_GETFD) != -1) {
      n++;
    }
  }
  return n;
}

static void worker(void *arg) {
  WorkerArg *w = (WorkerArg *)arg;
  StHttpConn io;
  int i;
  st_http_conn_init(&io);
  for (i = 0; i < w->count; i++) {
    StHttpResponse resp;
    uint64_t t0 = Util::TimeMs();
    int rc;
    int global_i;
    memset(&resp, 0, sizeof(resp));
    rc = st_http_exchange(w->req, &io, &resp);
    w->lat[w->begin + i] = (int)(Util::TimeMs() - t0);
    if (rc == 0 && resp.status >= 200 && resp.status < 300) {
      g_ok++;
      g_2xx++;
    } else if (rc == 0) {
      g_fail++;
      g_other++;
    } else {
      g_fail++;
    }
    g_bytes += resp.body_bytes;
    global_i = w->begin + i;
    if (global_i == g_total - 1) {
      free(g_last_body);
      g_last_body = resp.body;
      g_last_len = resp.body_len;
      resp.body = NULL;
    }
    st_http_response_free(&resp);
  }
  st_http_conn_close(&io);
  g_pending--;
}

extern "C" int lat_cmp(const void *a, const void *b) {
  int ia = *(const int *)a;
  int ib = *(const int *)b;
  if (ia < ib) {
    return -1;
  }
  if (ia > ib) {
    return 1;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  const char *method = NULL;
  const char *data = NULL;
  const char *data_file = NULL;
  const char *out_path = NULL;
  StHttpHeader headers[32];
  int header_count = 0;
  int conc = 1;
  int total = -1;
  int timeout = 3000;
  int keepalive = 0;
  int quiet = 0;
  int verbose = 0;
  int json = 0;
  int opt;
  char host[256];
  char path[4096];
  int port = 80;
  unsigned int ip_be = 0;
  char *file_body = NULL;
  int file_len = 0;
  StHttpRequest req;
  int *lat = NULL;
  WorkerArg *args = NULL;
  int base;
  int rem;
  int i;
  uint64_t start;
  uint64_t elapsed;
  int p50 = 0;
  int p99 = 0;
  double qps;
  int rc;
  static const struct option kLong[] = {{"json", 0, 0, 1}, {0, 0, 0, 0}};

  signal(SIGPIPE, SIG_IGN);

  while ((opt = getopt_long(argc, argv, "X:d:D:H:c:n:t:ko:qv", kLong, NULL)) !=
         -1) {
    if (opt == 1) {
      json = 1;
      continue;
    }
    switch (opt) {
    case 'X':
      method = optarg;
      break;
    case 'd':
      data = optarg;
      break;
    case 'D':
      data_file = optarg;
      break;
    case 'H':
      if (header_count >= 32) {
        fprintf(stderr, "too many -H\n");
        return 2;
      }
      headers[header_count].text = optarg;
      header_count++;
      break;
    case 'c':
      conc = atoi(optarg);
      break;
    case 'n':
      total = atoi(optarg);
      break;
    case 't':
      timeout = atoi(optarg);
      break;
    case 'k':
      keepalive = 1;
      break;
    case 'o':
      out_path = optarg;
      break;
    case 'q':
      quiet = 1;
      break;
    case 'v':
      verbose = 1;
      break;
    default:
      usage(argv[0]);
      return 2;
    }
  }
  if (optind != argc - 1) {
    usage(argv[0]);
    return 2;
  }
  if (data != NULL && data_file != NULL) {
    fprintf(stderr, "-d and -D are mutually exclusive\n");
    return 2;
  }
  if (method != NULL && strcmp(method, "GET") != 0 &&
      strcmp(method, "POST") != 0) {
    fprintf(stderr, "-X must be GET or POST\n");
    return 2;
  }
  if (conc < 1 || timeout < 1) {
    fprintf(stderr, "bad -c or -t\n");
    return 2;
  }
  if (total < 0) {
    total = conc;
  }
  if (total < 1) {
    fprintf(stderr, "bad -n\n");
    return 2;
  }
  if (conc > total) {
    conc = total;
  }
  if (method == NULL) {
    method = (data != NULL || data_file != NULL) ? "POST" : "GET";
  }

  rc = resolve_url(argv[optind], host, (int)sizeof(host), path,
                   (int)sizeof(path), &port, &ip_be);
  if (rc != 0) {
    return rc;
  }
  if (data_file != NULL) {
    file_body = read_file(data_file, &file_len);
    if (file_body == NULL) {
      return 2;
    }
    data = file_body;
  }

  /* ERR 会打到 stdout，把响应体和 curl 对不上。样例只靠退出码和 SUMMARY。 */
  LOG_LEVEL(LLOG_CRIT);
  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    free(file_body);
    return 1;
  }
  st_set_hook_flag();

  memset(&req, 0, sizeof(req));
  req.method = method;
  req.host = host;
  req.port = port;
  req.path = path;
  req.body = data;
  req.body_len =
      data != NULL ? (file_body != NULL ? file_len : (int)strlen(data)) : 0;
  req.timeout_ms = timeout;
  req.keepalive = keepalive;
  req.verbose = verbose;
  req.headers = headers;
  req.header_count = header_count;
  req.ip_be = ip_be;

  g_total = total;
  lat = (int *)calloc((size_t)total, sizeof(int));
  args = new WorkerArg[conc];
  if (lat == NULL || args == NULL) {
    fprintf(stderr, "out of memory\n");
    free(file_body);
    free(lat);
    delete[] args;
    return 1;
  }
  base = total / conc;
  rem = total % conc;
  for (i = 0; i < conc; i++) {
    args[i].index = i;
    args[i].count = base + (i < rem ? 1 : 0);
    args[i].begin = 0;
    args[i].req = &req;
    args[i].lat = lat;
  }
  for (i = 1; i < conc; i++) {
    args[i].begin = args[i - 1].begin + args[i - 1].count;
  }

  g_pending = conc;
  start = Util::TimeMs();
  if (conc == 1) {
    worker(&args[0]);
  } else {
    for (i = 0; i < conc; i++) {
      if (Frame::CreateThread(worker, &args[i]) == NULL) {
        fprintf(stderr, "CreateThread failed at %d\n", i);
        g_fail += args[i].count;
        g_pending--;
      }
    }
    {
      int64_t deadline =
          (int64_t)start + (int64_t)timeout * (int64_t)(base + 1) + 5000;
      while (g_pending > 0 && (int64_t)Util::TimeMs() < deadline) {
        st_sleep(10);
      }
    }
  }
  elapsed = Util::TimeMs() - start;
  if (elapsed == 0) {
    elapsed = 1;
  }

  if (total > 0) {
    qsort(lat, (size_t)total, sizeof(int), lat_cmp);
    p50 = lat[(total - 1) * 50 / 100];
    p99 = lat[(total - 1) * 99 / 100];
  }
  qps = (double)(g_ok + g_fail) * 1000.0 / (double)elapsed;

  if (!quiet && conc == 1 && total == 1 && g_last_body != NULL &&
      g_last_len > 0) {
    fwrite(g_last_body, 1, (size_t)g_last_len, stdout);
    if (g_last_body[g_last_len - 1] != '\n') {
      fputc('\n', stdout);
    }
  }
  if (out_path != NULL) {
    FILE *fp = fopen(out_path, "wb");
    if (fp == NULL) {
      fprintf(stderr, "cannot write %s\n", out_path);
      rc = 1;
    } else {
      if (g_last_body != NULL && g_last_len > 0) {
        fwrite(g_last_body, 1, (size_t)g_last_len, fp);
      }
      fclose(fp);
    }
  }
  if (json) {
    printf("{\"ok\":%d,\"fail\":%d,\"status_2xx\":%d,\"status_other\":%d,"
           "\"bytes\":%lld,\"qps\":%.2f,\"elapsed_ms\":%llu,\"p50_ms\":%d,"
           "\"p99_ms\":%d}\n",
           g_ok, g_fail, g_2xx, g_other, g_bytes, qps,
           (unsigned long long)elapsed, p50, p99);
  } else {
    printf("SUMMARY ok=%d fail=%d status_2xx=%d status_other=%d bytes=%lld "
           "qps=%.2f elapsed_ms=%llu p50_ms=%d p99_ms=%d\n",
           g_ok, g_fail, g_2xx, g_other, g_bytes, qps,
           (unsigned long long)elapsed, p50, p99);
  }
  fflush(stdout);

  {
    int open_fds = count_open_fds();
    if (open_fds > 32) {
      fprintf(stderr, "too many fds still open: %d\n", open_fds);
      rc = 1;
    }
  }

  free(g_last_body);
  free(file_body);
  free(lat);
  delete[] args;

  if (rc == 1) {
    return 1;
  }
  if (g_pending > 0 || g_fail != 0 || g_ok != total) {
    return 1;
  }
  return 0;
}
