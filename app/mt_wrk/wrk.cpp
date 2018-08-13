/*
 * Copyright (C) zhoulv2000@163.com
 * refer to wrk(https://github.com/wg/wrk)
 */

#include <vector>
#include "http_parser.h"
#include "utils.h"
#include "stats.h"
#include "process.h"

static wrk::config cg;

static struct 
{
    wrk::stats *latency;
    wrk::stats *requests;
} statistics;

static void usage() 
{
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
    { "connections", required_argument, NULL, 'c' },
    { "duration",    required_argument, NULL, 'd' },
    { "numbers",     required_argument, NULL, 'n' },
    { "header",      required_argument, NULL, 'H' },
    { "latency",     no_argument,       NULL, 'L' },
    { "timeout",     required_argument, NULL, 'T' },
    { "help",        no_argument,       NULL, 'h' },
    { "version",     no_argument,       NULL, 'v' },
    { NULL,          0,                 NULL,  0  }
};

static int parse_args(wrk::config *_cg, char **url, struct http_parser_url *parts, 
    char **headers, int argc, char **argv) 
{
    char **header = headers;
    int c;

    memset(_cg, 0, sizeof(wrk::config));
    _cg->numbers     = 2;
    _cg->connections = 10;
    _cg->duration    = 10;
    _cg->timeout     = SOCKET_TIMEOUT_MS;

    while ((c = getopt_long(argc, argv, "n:c:d:s:H:T:Lrv?", longopts, NULL)) != -1) 
    {
        switch (c) 
        {
            case 'n':
                if (wrk::Utils::scan_metric(optarg, &_cg->numbers))
                {
                    return -1;
                }
                break;
            case 'c':
                if (wrk::Utils::scan_metric(optarg, &_cg->connections)) 
                {
                    return -1;
                }
                break;
            case 'd':
                if (wrk::Utils::scan_time(optarg, &_cg->duration)) 
                {
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
                if (wrk::Utils::scan_time(optarg, &_cg->timeout))
                {
                    return -1;
                } 
                _cg->timeout *= 1000;
                break;
            case 'h':
            case '?':
            case ':':
            default:
                return -1;
        }
    }

    if (optind == argc || !_cg->numbers || !_cg->duration) return -1;

    if (!wrk::Utils::parse_url(argv[optind], parts)) 
    {
        fprintf(stderr, "invalid URL: %s\n", argv[optind]);
        return -1;
    }

    if (!_cg->connections || _cg->connections < _cg->numbers) 
    {
        fprintf(stderr, "number of connections must be >= numbers\n");
        return -1;
    }

    *url    = argv[optind];
    *header = NULL;

    return 0;
}

static char *copy_url_part(char *url, struct http_parser_url *parts, enum http_parser_url_fields field) 
{
    char *part = NULL;

    if (parts->field_set & (1 << field)) 
    {
        uint16_t off = parts->field_data[field].off;
        uint16_t len = parts->field_data[field].len;
        part = (char *)calloc(1, len + 1 * sizeof(char));
        memcpy(part, &url[off], len);
    }

    return part;
}

void callback(void *data)
{
    wrk::Process *p = (wrk::Process*)data;
    
    // -------------- 1.定义sockaddr_in，发起网络请求 --------------
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(cg.port);  ///服务器端口
    char ipstr[32] = {0};
    if (wrk::Utils::getip_by_domain(cg.host, ipstr) != 0) 
    {
        memcpy(ipstr, cg.host, strlen(cg.host));
    }
    servaddr.sin_addr.s_addr = inet_addr(ipstr);  ///服务器ip

    // TODO : debug
    // printf("ipstr : %s, port : %d\n", ipstr, cg.port);

    int ret = mt_init_frame();
    mt_set_hook_flag();
    IMtActionClient *actionframe = GetInstance<IMtActionClient>();

    // -------------- 2.创建action --------------
    int count = (int)(cg.connections / cg.numbers);
    std::vector<wrk::HttpIMessage*> msg_vc;
    std::vector<IMtAction*> action_vc;
    for (int i = 0; i < count; i++)
    {
        wrk::HttpIMessage *msg = new wrk::HttpIMessage();
        msg->m_host_ = cg.host;
        msg_vc.push_back(msg);

        IMtAction* action = new wrk::HttpIMtAction();
        action->SetMsgDstAddr(&servaddr);
        action->SetConnType(eTCP_SHORT_CONN);
        action->SetMsgBufferSize(4096); // 设置buffsize大小
        action->SetIMessagePtr(msg);
        actionframe->Add(action);
        action_vc.push_back(action);
    }

    ret = actionframe->SendRecv(10000);

    // TODO : debug
    // printf("print_number_debug[2] : \n");
    // wrk::Utils::print_number_debug(&(msg->m_number_));

    std::vector<wrk::HttpIMessage*>::iterator iter = msg_vc.begin();
    while (iter != msg_vc.end())
    {
        p->ChildProcessSend(&((*iter)->m_number_), sizeof(wrk::number));
        iter++;
    }

    // 删除数组元素
    std::vector<wrk::HttpIMessage*>::iterator iter1 = msg_vc.begin();
    while (iter1 != msg_vc.end())
    {
        safe_delete(*iter1);
        iter1++;
    }
    std::vector<IMtAction*>::iterator iter2 = action_vc.begin();
    while (iter2 != action_vc.end())
    {
        safe_delete(*iter2);
        iter2++;
    }
}

int main(int argc, char **argv) 
{
    char *url, **headers = (char **)malloc(argc * sizeof(char *));
    struct http_parser_url parts = {};

    if (parse_args(&cg, &url, &parts, headers, argc, argv)) 
    {
        usage();
        exit(1);
    }

    char *schema  = copy_url_part(url, &parts, UF_SCHEMA);
    char *host    = copy_url_part(url, &parts, UF_HOST);
    char *port    = copy_url_part(url, &parts, UF_PORT);
    char *service = port ? port : schema;

    wrk::StatsCalculate slatency(cg.timeout * 1000), srequests(MAX_THREAD_RATE_S);

    statistics.latency  = slatency.get_stats();
    statistics.requests = srequests.get_stats();
    cg.host = host;
    cg.port = (port != NULL) ? atoi(port) : 80;

    // TODO : debug
    // wrk::Utils::print_config_debug(&cg);

    // -------------- 开启进程，然后进行数据统计 ---------------
    uint64_t start    = wrk::Utils::time_us();
    uint64_t complete = 0;
    uint64_t bytes    = 0;

    // 创建子进程
    wrk::Process p;
    int ret = p.Create(cg.numbers, callback);
    // 创建失败
    if (ret < 0)
    {
        printf("create child process error, code : %d", ret);
        exit(1);
    }

    wrk::number *numbers = (wrk::number *)malloc(cg.numbers * sizeof(wrk::number));
    int count = 0;
    do
    {
        unsigned int num_len;
        int r = p.ParentProcessRecv(numbers + count, num_len);

        // TODO : debug
        // wrk::Utils::print_number_debug(numbers + count);

        if (r < 0)
        {
            break;
        }
        count++;
    } while (true);
    p.Wait();

    char *time = wrk::Utils::format_time_s(cg.duration);
    printf("Running %s test @ %s\n", time, url);
    printf("  %ld numbers and %ld connections\n", cg.numbers, cg.connections);

    wrk::errors errors = { 0, 0, 0, 0, 0 };

    for (uint64_t i = 0; i < cg.numbers; i++) 
    {
        wrk::number *t = &numbers[i];

        complete += t->complete;
        bytes    += t->bytes;

        errors.connect += t->errors.connect;
        errors.read    += t->errors.read;
        errors.write   += t->errors.write;
        errors.timeout += t->errors.timeout;
        errors.status  += t->errors.status;
    }

    uint64_t runtime_us = wrk::Utils::time_us() - start;
    long double runtime_s = runtime_us / 1000000.0;
    long double req_per_s = complete   / runtime_s;
    long double bytes_per_s = bytes    / runtime_s;

    if (complete / cg.connections > 0) 
    {
        int64_t interval = runtime_us / (complete / cg.connections);
        slatency.correct(interval);
    }

    wrk::Utils::print_stats_header();
    wrk::Utils::print_stats("Latency", &slatency, wrk::Utils::format_time_us);
    wrk::Utils::print_stats("Req/Sec", &srequests, wrk::Utils::format_metric);
    if (cg.latency)
    {
        wrk::Utils::print_stats_latency(&slatency);
    }

    char *runtime_msg = wrk::Utils::format_time_us(runtime_us);
    printf("  %ld requests in %s, %sB read\n", complete, runtime_msg, wrk::Utils::format_binary(bytes));
    if (errors.connect || errors.read || errors.write || errors.timeout)
    {
        printf("  Socket errors: connect %d, read %d, write %d, timeout %d\n",
               errors.connect, errors.read, errors.write, errors.timeout);
    }
    if (errors.status)
    {
        printf("  Non-2xx or 3xx responses: %d\n", errors.status);
    }

    printf("Requests/sec: %9.2Lf\n", req_per_s);
    printf("Transfer/sec: %10sB\n", wrk::Utils::format_binary(bytes_per_s));

    return 0;
}