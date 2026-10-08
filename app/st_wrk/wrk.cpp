/*
 * Copyright (C) zhoulv2000@163.com
 * refer to wrk(https://github.com/wg/wrk)
 */

#include "http_parser.h"
#include "process.h"
#include "stats.h"
#include "utils.h"
#include "app/st_c.h"
#include "stlib/st_log.h"
#include "stlib/st_netaddr.h"
#include <vector>

static wrk::config cg;
int wrk::Util::s_verbose_ = 0;

static void usage() {
  printf("Usage: wrk <options> <url>                            \n"
         "  Options:                                            \n"
         "    -c, --connections <N>  Total connections (must be >= numbers)\n"
         "    -d, --duration    <T>  Duration label (SI time, e.g. 2s)\n"
         "    -n, --numbers     <N>  Worker processes (fork count)\n"
         "                           Each worker uses connections/numbers\n"
         "                           sockets; connections must be >= numbers\n"
         "                                                      \n"
         "    -H, --header      <H>  Add header to request      \n"
         "        --latency          Print latency statistics   \n"
         "        --timeout     <T>  Socket/request timeout     \n"
         "        --json             Print one JSON summary line\n"
         "    -v, --version          Print version details      \n"
         "                                                      \n"
         "  Numeric arguments may include a SI unit (1k, 1M, 1G)\n"
         "  Time arguments may include a time unit (2s, 2m, 2h)\n"
         "  A greppable SUMMARY line is always printed at the end.\n");
}

static struct option longopts[] = {
    {"connections", required_argument, NULL, 'c'},
    {"duration", required_argument, NULL, 'd'},
    {"numbers", required_argument, NULL, 'n'},
    {"header", required_argument, NULL, 'H'},
    {"latency", no_argument, NULL, 'L'},
    {"timeout", required_argument, NULL, 'T'},
    {"json", no_argument, NULL, 'j'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}};

static int parse_args(wrk::config *_cg, char **url,
                      struct http_parser_url *parts, char **headers, int argc,
                      char **argv) {
  char **header = headers;
  int c;

  memset(_cg, 0, sizeof(wrk::config));
  _cg->numbers = 2;
  _cg->connections = 10;
  _cg->duration = 10;
  _cg->timeout = SOCKET_TIMEOUT_MS;

  while ((c = getopt_long(argc, argv, "n:c:d:s:H:T:Ljrv?", longopts, NULL)) !=
         -1) {
    switch (c) {
    case 'n':
      if (wrk::Util::scan_metric(optarg, &_cg->numbers)) {
        return -1;
      }
      break;
    case 'c':
      if (wrk::Util::scan_metric(optarg, &_cg->connections)) {
        return -1;
      }
      break;
    case 'd':
      if (wrk::Util::scan_time(optarg, &_cg->duration)) {
        return -1;
      }
      break;
    case 'H':
      *header++ = optarg;
      break;
    case 'L':
      _cg->latency = true;
      break;
    case 'j':
      _cg->json = true;
      break;
    case 'T':
      if (wrk::Util::scan_time(optarg, &_cg->timeout)) {
        return -1;
      }
      _cg->timeout *= 1000;
      break;
    case 'v':
      wrk::Util::s_verbose_ = 1; // 打印版本和详细信息
      break;
    case 'h':
    case '?':
    case ':':
    default:
      return -1;
    }
  }

  if (optind == argc || !_cg->numbers || !_cg->duration)
    return -1;

  if (!wrk::Util::parse_url(argv[optind], parts)) {
    fprintf(stderr, "invalid URL: %s\n", argv[optind]);
    return -1;
  }

  if (!_cg->connections || _cg->connections < _cg->numbers) {
    fprintf(stderr, "number of connections must be >= numbers\n");
    return -1;
  }

  *url = argv[optind];
  *header = NULL;

  return 0;
}

/* 短连接上的一次 HTTP/1.1 GET。统计口径与原先按连接累计的 number 一致：
 * 失败都记到 errors.connect，成功才 complete++，bytes 为解析器吃掉的字节。 */
extern "C" int wrk_on_message_complete(http_parser *p) {
  int *done = (int *)p->data;
  if (done != NULL) {
    *done = 1;
  }
  return 0;
}

static void release_client(StExecClientConnection *conn) {
  if (conn != NULL) {
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
  }
}

static int remaining_ms(uint64_t start_ms, int timeout_ms) {
  int used = (int)(Util::TimeMs() - start_ms);
  if (used >= timeout_ms) {
    return 0;
  }
  return timeout_ms - used;
}

static void http_fail(wrk::number *num) {
  num->errors.connect++;
  num->end = wrk::Util::time_us();
}

static void http_get(wrk::number *num, const char *host, const std::string &path,
                     struct sockaddr_in *dst, int timeout_ms) {
  char req[4096];
  int reqlen;
  StExecClientConnection *conn = NULL;
  int fd = -1;
  uint64_t start_ms;
  int left;
  ssize_t sent;
  char rbuf[4096];
  int accepted = 0;
  int done = 0;
  http_parser parser;
  http_parser_settings settings;
  const char *use_host = host != NULL ? host : "";

  memset(num, 0, sizeof(*num));
  reqlen = snprintf(req, sizeof(req),
                    "GET %s HTTP/1.1\r\nHost: %s\r\n"
                    "User-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n",
                    path.c_str(), use_host);
  if (reqlen < 0 || reqlen >= (int)sizeof(req)) {
    num->start = wrk::Util::time_us();
    http_fail(num);
    return;
  }
  num->requests++;
  num->start = wrk::Util::time_us();
  if (wrk::Util::s_verbose_) {
    printf("[SEND]len : %d, buf : %s\n", reqlen, req);
  }

  start_ms = Util::TimeMs();
  {
    StNetAddr addr(*dst);
    conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
        eTCP_CONN, &addr);
    if (conn == NULL) {
      http_fail(num);
      return;
    }
    left = remaining_ms(start_ms, timeout_ms);
    if (left <= 0) {
      release_client(conn);
      http_fail(num);
      return;
    }
    conn->SetTimeout(left);
    fd = conn->Create(addr);
  }
  if (fd < 0) {
    release_client(conn);
    http_fail(num);
    return;
  }

  left = remaining_ms(start_ms, timeout_ms);
  sent = st_send(fd, req, (size_t)reqlen, 0, left);
  if (sent < 0 || (int)sent != reqlen) {
    release_client(conn);
    http_fail(num);
    return;
  }

  http_parser_init(&parser, HTTP_RESPONSE);
  http_parser_settings_init(&settings);
  parser.data = &done;
  settings.on_message_complete = wrk_on_message_complete;

  while (!done && accepted < (int)sizeof(rbuf)) {
    int nread;
    size_t parsed;
    left = remaining_ms(start_ms, timeout_ms);
    if (left <= 0) {
      break;
    }
    nread = st_recv(fd, rbuf + accepted, (int)sizeof(rbuf) - accepted, 0, left);
    if (nread < 0) {
      break;
    }
    if (nread == 0) {
      http_parser_execute(&parser, &settings, rbuf + accepted, 0);
      break;
    }
    parsed =
        http_parser_execute(&parser, &settings, rbuf + accepted, (size_t)nread);
    if (wrk::Util::s_verbose_) {
      printf("[RECV]http_len : %d, len : %d, buf : %.*s\n", (int)parsed, nread,
             nread, rbuf + accepted);
    }
    if (HTTP_PARSER_ERRNO(&parser) != HPE_OK) {
      break;
    }
    accepted += (int)parsed;
    if (done) {
      break;
    }
    if ((int)parsed != nread) {
      break;
    }
  }
  release_client(conn);
  if (!done) {
    http_fail(num);
    return;
  }
  num->bytes += (uint64_t)accepted;
  num->complete++;
  num->end = wrk::Util::time_us();
}

static char *copy_url_part(char *url, struct http_parser_url *parts,
                           enum http_parser_url_fields field) {
  char *part = NULL;

  if (parts->field_set & (1 << field)) {
    uint16_t off = parts->field_data[field].off;
    uint16_t len = parts->field_data[field].len;
    part = (char *)calloc(1, len + 1 * sizeof(char));
    memcpy(part, &url[off], len);
  }

  return part;
}

void callback(void *data) {
  wrk::Process *p = (wrk::Process *)data;

  // -------------- 1.定义sockaddr_in，发起网络请求 --------------
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(cg.port); ///服务器端口
  char ipstr[32] = {0};
  if (wrk::Util::getip_by_domain(cg.host, ipstr) != 0) {
    memcpy(ipstr, cg.host, strlen(cg.host));
  }
  servaddr.sin_addr.s_addr = inet_addr(ipstr); ///服务器ip

  // debug选项
  if (wrk::Util::s_verbose_) {
    printf("dst_ip : %s, port : %d\n", ipstr, cg.port);
  }

  int ret = mt_init_frame();
  mt_set_hook_flag();
  if (!wrk::Util::s_verbose_) {
    LOG_LEVEL(LLOG_ERR);
  }

  // 每进程 connections/numbers 次短连接，顺序发出（与原先 SendRecv 循环一致）。
  int count = (int)(cg.connections / cg.numbers);
  if (wrk::Util::s_verbose_) {
    printf("mt_init_frame ret : %d, count : %d\n", ret, count);
  }
  std::vector<wrk::number> results;
  results.resize(count);
  for (int i = 0; i < count; i++) {
    std::string path = "/";
    if (cg.path != NULL) {
      path = cg.path;
      if (cg.query != NULL) {
        path += "?";
        path += cg.query;
      }
    }
    http_get(&results[i], cg.host, path, &servaddr, 10000);
  }

  ret = 0;
  if (wrk::Util::s_verbose_) {
    printf("actionframe ret : %d, count : %d\n", ret, count);
  }

  for (int i = 0; i < count; i++) {
    p->ChildProcessSend(&results[i], sizeof(wrk::number));
    if (wrk::Util::s_verbose_) {
      printf("[CHILDREN] send : \n");
      wrk::Util::print_number_debug(&results[i]);
    }
  }
}

int main(int argc, char **argv) {
  char *url, **headers = (char **)malloc(argc * sizeof(char *));
  struct http_parser_url parts = {};

  if (parse_args(&cg, &url, &parts, headers, argc, argv)) {
    usage();
    exit(1);
  }

  char *schema = copy_url_part(url, &parts, UF_SCHEMA);
  char *host = copy_url_part(url, &parts, UF_HOST);
  char *port = copy_url_part(url, &parts, UF_PORT);
  char *path = copy_url_part(url, &parts, UF_PATH);
  char *query = copy_url_part(url, &parts, UF_QUERY);
  char *service = port ? port : schema;

  wrk::StatsCalculate slatency(cg.timeout * 1000), srequests(MAX_THREAD_RATE_S);

  cg.host = host;
  cg.port = (port != NULL) ? atoi(port) : 80;
  cg.path = path;
  cg.query = query;

  if (wrk::Util::s_verbose_) {
    wrk::Util::print_config_debug(&cg);
  }

  // -------------- 开启进程，然后进行数据统计 ---------------
  uint64_t start = wrk::Util::time_us();
  // 创建子进程
  wrk::Process p;
  int ret = p.Create(cg.numbers, callback);
  // 创建失败
  if (ret < 0) {
    printf("create child process error, code : %d\n", ret);
    exit(1);
  }

  wrk::number *numbers = (wrk::number *)malloc(sizeof(wrk::number));
  // 初始化为0
  wrk::Util::init_number(numbers);
  do {
    unsigned int num_len;
    wrk::number num_o;

    int r = p.ParentProcessRecv(&num_o, num_len);
    if (r < 0) {
      break;
    }

    numbers->connections += num_o.connections;
    numbers->complete += num_o.complete;
    numbers->requests += num_o.requests;
    numbers->bytes += num_o.bytes;
    numbers->errors.connect += num_o.errors.connect;
    numbers->errors.read += num_o.errors.read;
    numbers->errors.write += num_o.errors.write;
    numbers->errors.status += num_o.errors.status;
    numbers->errors.timeout += num_o.errors.timeout;

    if (!slatency.record(num_o.end - num_o.start)) {
      numbers->errors.timeout++;
    }
    if (num_o.requests > 0) {
      uint64_t elapsed_ms = (num_o.end - num_o.start) / 1000;
      srequests.record((num_o.requests / (double)elapsed_ms) * 1000);
    }

    // TODO : debug
    if (wrk::Util::s_verbose_) {
      printf("[PARENT] recv : \n");
      wrk::Util::print_number_debug(numbers);
    }
  } while (true);
  p.Wait();

  char *time = wrk::Util::format_time_s(cg.duration);
  printf("Running %s test @ %s\n", time, url);
  printf("  %ld numbers and %ld connections\n", cg.numbers, cg.connections);

  uint64_t complete = numbers->complete;
  uint64_t bytes = numbers->bytes;

  wrk::error_counts errors = {0, 0, 0, 0, 0};
  errors.connect += numbers->errors.connect;
  errors.read += numbers->errors.read;
  errors.write += numbers->errors.write;
  errors.timeout += numbers->errors.timeout;
  errors.status += numbers->errors.status;

  uint64_t runtime_us = wrk::Util::time_us() - start;
  long double runtime_s = runtime_us / 1000000.0;
  long double req_per_s = complete / runtime_s;
  long double bytes_per_s = bytes / runtime_s;

  if (complete / cg.connections > 0) {
    int64_t interval = runtime_us / (complete / cg.connections);
    slatency.correct(interval);
  }

  wrk::Util::print_stats_header();
  wrk::Util::print_stats("Latency", &slatency, wrk::Util::format_time_us);
  wrk::Util::print_stats("Req/Sec", &srequests, wrk::Util::format_metric);
  if (cg.latency) {
    wrk::Util::print_stats_latency(&slatency);
  }

  char *runtime_msg = wrk::Util::format_time_us(runtime_us);
  printf("  %ld requests in %s, %sB read\n", complete, runtime_msg,
         wrk::Util::format_binary(bytes));
  if (errors.connect || errors.read || errors.write || errors.timeout) {
    printf("  Socket errors: connect %d, read %d, write %d, timeout %d\n",
           errors.connect, errors.read, errors.write, errors.timeout);
  }
  if (errors.status) {
    printf("  Non-2xx or 3xx responses: %d\n", errors.status);
  }

  printf("Requests/sec: %9.2Lf\n", req_per_s);
  printf("Transfer/sec: %10sB\n", wrk::Util::format_binary(bytes_per_s));
  printf("All Transfer: %10sB\n", wrk::Util::format_binary(bytes));

  {
    unsigned int err_all = errors.connect + errors.read + errors.write +
                           errors.timeout + errors.status;
    printf("SUMMARY complete=%lu requests=%lu req_per_s=%.2f bytes=%lu "
           "errors=%u runtime_us=%lu\n",
           (unsigned long)complete, (unsigned long)numbers->requests,
           (double)req_per_s, (unsigned long)bytes, err_all,
           (unsigned long)runtime_us);
    if (cg.json) {
      printf("JSON {\"complete\":%lu,\"requests\":%lu,\"req_per_s\":%.2f,"
             "\"bytes\":%lu,\"errors\":%u,\"runtime_us\":%lu}\n",
             (unsigned long)complete, (unsigned long)numbers->requests,
             (double)req_per_s, (unsigned long)bytes, err_all,
             (unsigned long)runtime_us);
    }
  }

  return 0;
}