/*
 * Copyright (C) zhoulv2000@163.com
 * refer to wrk(https://github.com/wg/wrk)
 */

#include "http_parser.h"
#include "process.h"
#include "stats.h"
#include "utils.h"
#include <vector>

static wrk::config cg;
int wrk::Util::s_verbose_ = 0;

static void usage() {
  printf("Usage: wrk <options> <url>                            \n"
         "  Options:                                            \n"
         "    -c, --connections <N>  Connections to keep open   \n"
         "    -d, --duration    <T>  Duration of test           \n"
         "    -n, --numbers     <N>  Number of request to use   \n"
         "                                                      \n"
         "    -H, --header      <H>  Add header to request      \n"
         "        --latency          Print latency statistics   \n"
         "        --timeout     <T>  Socket/request timeout     \n"
         "    -v, --version          Print version details      \n"
         "                                                      \n"
         "  Numeric arguments may include a SI unit (1k, 1M, 1G)\n"
         "  Time arguments may include a time unit (2s, 2m, 2h)\n");
}

static struct option longopts[] = {
    {"connections", required_argument, NULL, 'c'},
    {"duration", required_argument, NULL, 'd'},
    {"numbers", required_argument, NULL, 'n'},
    {"header", required_argument, NULL, 'H'},
    {"latency", no_argument, NULL, 'L'},
    {"timeout", required_argument, NULL, 'T'},
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

  while ((c = getopt_long(argc, argv, "n:c:d:s:H:T:Lrv?", longopts, NULL)) !=
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
  IMtActionClient *actionframe = Instance<IMtActionClient>();

  // -------------- 2.创建action --------------
  int count = (int)(cg.connections / cg.numbers);
  if (wrk::Util::s_verbose_) {
    printf("mt_init_frame ret : %d, count : %d\n", ret, count);
  }
  std::vector<wrk::HttpIMessage *> msg_vc;
  std::vector<IMtAction *> action_vc;
  for (int i = 0; i < count; i++) {
    wrk::HttpIMessage *msg = new wrk::HttpIMessage();
    msg->m_host_ = cg.host;
    if (cg.path != NULL) {
      msg->m_get_ = std::string(cg.path);
      if (cg.query != NULL) {
        msg->m_get_ += "?" + std::string(cg.query);
      }
    } else {
      msg->m_get_ = "/";
    }

    IMtAction *action = new wrk::HttpIMtAction();
    action->SetMsgDstAddr(&servaddr);
    action->SetConnType(eTCP_SHORT_CONN);
    action->SetMsgBufferSize(4096); // 设置buffsize大小
    action->SetIMessagePtr(msg);
    actionframe->Add(action);

    msg_vc.push_back(msg);
    action_vc.push_back(action);
  }

  ret = actionframe->SendRecv(10000);
  // debug选项
  if (wrk::Util::s_verbose_) {
    printf("actionframe ret : %d, count : %d\n", ret, count);
  }

  std::vector<wrk::HttpIMessage *>::iterator iter = msg_vc.begin();
  while (iter != msg_vc.end()) {
    p->ChildProcessSend(&((*iter)->m_number_), sizeof(wrk::number));
    if (wrk::Util::s_verbose_) {
      printf("[CHILDREN] send : \n");
      wrk::Util::print_number_debug(&((*iter)->m_number_));
    }
    iter++;
  }

  // 删除数组元素
  std::vector<wrk::HttpIMessage *>::iterator iter1 = msg_vc.begin();
  while (iter1 != msg_vc.end()) {
    safe_delete(*iter1);
    iter1++;
  }
  std::vector<IMtAction *>::iterator iter2 = action_vc.begin();
  while (iter2 != action_vc.end()) {
    safe_delete(*iter2);
    iter2++;
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

  wrk::errors errors = {0, 0, 0, 0, 0};
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

  return 0;
}