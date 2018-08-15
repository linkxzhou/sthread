/*
 * Copyright (C) zhoulv2000@163.com
 * refer to wrk(https://github.com/wg/wrk)
 */

#ifndef __UTILS_H_INCLUDED__
#define __UTILS_H_INCLUDED__

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdarg.h>
#include <getopt.h>

#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "http_parser.h"
#include "stats.h"

namespace wrk
{

#define RECVBUF  8192

#define MAX_THREAD_RATE_S   10000000
#define SOCKET_TIMEOUT_MS   2000
#define RECORD_INTERVAL_MS  100

typedef struct 
{
    int scale;
    char *base;
    char *units[6];
} units;

units time_units_us = 
{
    .scale = 1000,
    .base  = "us",
    .units = { "ms", "s", NULL }
};

units time_units_s = 
{
    .scale = 60,
    .base  = "s",
    .units = { "m", "h", NULL }
};

units binary_units = 
{
    .scale = 1024,
    .base  = "",
    .units = { "K", "M", "G", "T", "P", NULL }
};

units metric_units = 
{
    .scale = 1000,
    .base  = "",
    .units = { "k", "M", "G", "T", "P", NULL }
};

static char *aprintf(char **s, const char *fmt, ...) 
{
    char *c = NULL;
    int n, len;
    va_list ap;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);

    len = *s ? strlen(*s) : 0;

    if ((*s = (char *)::realloc(*s, (len + n) * sizeof(char)))) 
    {
        c = *s + len;
        va_start(ap, fmt);
        vsnprintf(c, n, fmt, ap);
        va_end(ap);
    }

    return c;
}

class Utils
{
public:
    static void print_stats_header() 
    {
        fprintf(stdout, "       Stats%6s%11s%8s%12s\n", "Avg", "Stdev", "Max", "+/- Stdev");
    }

    static void print_units(long double n, char *(*fmt)(long double), int width) 
    {
        char *msg = fmt(n);
        int len = strlen(msg), pad = 2;

        if (isalpha(msg[len-1])) pad--;
        if (isalpha(msg[len-2])) pad--;
        width -= pad;

        fprintf(stdout, "%*.*s%.*s", width, width, msg, pad, "  ");

        ::free(msg);
    }

    static void print_stats(const char *name, StatsCalculate *sc, char *(*fmt)(long double)) 
    {
        uint64_t max = sc->get_stats()->max;
        long double mean  = sc->mean();
        long double stdev = sc->stdev(mean);

        fprintf(stdout, "    %-10s", name);
        print_units(mean,  fmt, 8);
        print_units(stdev, fmt, 10);
        print_units(max,   fmt, 9);
        fprintf(stdout, "%8.2Lf%%\n", sc->within_stdev(mean, stdev, 1));
    }

    static void print_stats_latency(StatsCalculate *sc) 
    {
        long double percentiles[] = { 50.0, 75.0, 90.0, 99.0 };
        fprintf(stdout, "  Latency Distribution\n");
        for (size_t i = 0; i < sizeof(percentiles) / sizeof(long double); i++) 
        {
            long double p = percentiles[i];
            uint64_t n = sc->percentile(p);
            fprintf(stdout, "%7.0Lf%%", p);
            print_units(n, format_time_us, 10);
            fprintf(stdout, "\n");
        }
    }

    // 通用函数
    static char *format_units(long double n, units *m, int p) 
    {
        long double amt = n, scale;
        char *unit = m->base;
        char *msg = NULL;

        scale = m->scale * 0.85;

        for (int i = 0; m->units[i+1] && amt >= scale; i++) 
        {
            amt /= m->scale;
            unit = m->units[i];
        }

        aprintf(&msg, "%.*Lf%s", p, amt, unit);

        return msg;
    }

    static int scan_units(char *s, uint64_t *n, units *m) 
    {
        uint64_t base, scale = 1;
        char unit[3] = { 0, 0, 0 };
        int i, c;

        if ((c = sscanf(s, "%ld%2s", &base, unit)) < 1) return -1;

        if (c == 2 && strncasecmp(unit, m->base, 3)) 
        {
            for (i = 0; m->units[i] != NULL; i++) 
            {
                scale *= m->scale;
                if (!strncasecmp(unit, m->units[i], 3)) break;
            }
            if (m->units[i] == NULL) return -1;
        }

        *n = base * scale;
        return 0;
    }

    static char *format_binary(long double n) 
    {
        return format_units(n, &binary_units, 2);
    }

    static char *format_metric(long double n) 
    {
        return format_units(n, &metric_units, 2);
    }

    static char *format_time_us(long double n) 
    {
        units *units = &time_units_us;
        if (n >= 1000000.0) 
        {
            n /= 1000000.0;
            units = &time_units_s;
        }
        return format_units(n, units, 2);
    }

    static char *format_time_s(long double n) 
    {
        return format_units(n, &time_units_s, 0);
    }

    static int scan_metric(char *s, uint64_t *n) 
    {
        return scan_units(s, n, &metric_units);
    }

    static int scan_time(char *s, uint64_t *n) 
    {
        return scan_units(s, n, &time_units_s);
    }

    static uint64_t time_us() 
    {
        struct timeval t;
        ::gettimeofday(&t, NULL);
        return (t.tv_sec * 1000000) + t.tv_usec;
    }

    static int parse_url(char *url, struct http_parser_url *parts) 
    {
        if (!http_parser_parse_url(url, strlen(url), 0, parts)) 
        {
            if (!(parts->field_set & (1 << UF_SCHEMA))) return 0;
            if (!(parts->field_set & (1 << UF_HOST)))   return 0;
            return 1;
        }
        return 0;
    }

    /*****************************
    typedef struct 
    {
        uint64_t connections;
        uint64_t duration;
        uint64_t numbers;
        uint64_t timeout;
        bool     delay;
        bool     dynamic;
        bool     latency;
        char    *host;
    } config;
    *****************************/
    static void print_config_debug(config *cg)
    {
        printf("============ debug ============\n");
        printf("connections : %ld\n", cg->connections);
        printf("duration : %ld\n", cg->duration);
        printf("numbers : %ld\n", cg->numbers);
        printf("timeout : %ld\n", cg->timeout);
        printf("host : %s\n", cg->host);
        printf("port : %d\n", cg->port);
    }

    /*****************************
    typedef struct 
    {
        struct addrinfo *addr;
        uint64_t connections;
        uint64_t complete;
        uint64_t requests;
        uint64_t bytes;
        uint64_t start;
        errors errors;
    } number;
    *****************************/
    static void print_number_debug(number *num)
    {
        printf("============ debug ============\n");
        printf("connections : %ld\n", num->connections);
        printf("complete : %ld\n", num->complete);
        printf("requests : %ld\n", num->requests);
        printf("bytes : %ld\n", num->bytes);
    }

    static int getip_by_domain(const char *domain, char *ip)
    {
        char **pptr;
        struct hostent *hptr;
    
        hptr = ::gethostbyname(domain);
        if (NULL == hptr)
        {
            LOG_ERROR("gethostbyname error for host:%s/n", domain);
            return -1;
        }
    
        for (pptr = hptr->h_addr_list ; *pptr != NULL; pptr++)
        {
            if (NULL != inet_ntop(hptr->h_addrtype, *pptr, ip, 16) )
            {
                return 0;
            }
        }
    
        return -1;
    }

    static void init_number(number *num)
    {
        num->connections = 0;
        num->complete = 0;
        num->requests = 0;
        num->bytes = 0;
        num->errors.connect = 0;
        num->errors.read = 0;
        num->errors.write = 0;
        num->errors.status = 0;
        num->errors.timeout = 0;
    }

public:
    static int s_verbose_;
};

static int empty_cb (http_parser *p) { return 0; }
static int empty_data_cb (http_parser *p, const char *buf, size_t len) { return 0; }

static http_parser_settings settings_null =
  {.on_message_begin = empty_cb
  ,.on_header_field = empty_data_cb
  ,.on_header_value = empty_data_cb
  ,.on_url = empty_data_cb
  ,.on_body = empty_data_cb
  ,.on_message_complete = empty_cb
  ,.on_chunk_header = empty_cb
  ,.on_chunk_complete = empty_cb
  };

class HttpIMessage: public IMessage
{
public:
    HttpIMessage()
    {
        m_parser_ = (http_parser *)malloc(sizeof(http_parser));
        m_number_.complete = 0;
        m_number_.requests = 0;
        m_number_.bytes = 0;
        m_number_.errors.connect = 0; 
        m_number_.errors.read = 0; 
        m_number_.errors.write = 0; 
        m_number_.errors.status = 0; 
        m_number_.errors.timeout = 0;
    }

public:
    number m_number_;
    http_parser *m_parser_;
    std::string m_host_, m_get_;
};

class HttpIMtAction: public IMtAction
{
public:
    virtual int HandleEncode(void* buf, int& len, IMessage* msg)
    {
        HttpIMessage *m = ((HttpIMessage *)msg);
        if (m == NULL)
        {
            return -1;
        }

        char request_buf[4096] = {0};
        snprintf(request_buf, sizeof(request_buf) - 1, "GET %s HTTP/1.1\r\nHost: %s\r\n"
            "User-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n", 
            m->m_get_.c_str(), m->m_host_.c_str()) ;
        len = strlen(request_buf);
        memcpy(buf, request_buf, len);
        ((char *)buf)[len] = '\0';

        if (wrk::Utils::s_verbose_)
        {
            printf("[SEND]len : %d, buf : %s\n", len, buf);
        }

        http_parser_init(m->m_parser_, HTTP_REQUEST);
        m->m_number_.requests++;
        m->m_number_.start = wrk::Utils::time_us();

        return 0;
    }
    virtual int HandleInput(void* buf, int len, IMessage* msg)
    {
        HttpIMessage *m = ((HttpIMessage *)msg);

        http_parser_init(m->m_parser_, HTTP_RESPONSE); // 初始化parser为Response类型
        int http_len = http_parser_execute(m->m_parser_, 
            &settings_null, (const char *)buf, len);
        
        if (wrk::Utils::s_verbose_)
        {
            printf("[RECV]http_len : %d, len : %d, buf : %s\n", http_len, len, buf);
        }

        if (http_len > 0)
        {
            m->m_number_.bytes += http_len;
            return http_len;
        }
        else
        {
            return (http_len == 0) ? -1 : http_len - 1; // 进入异常 
        }
    }
    virtual int HandleProcess(void* buf, int len, IMessage* msg)
    {
        HttpIMessage *m = ((HttpIMessage *)msg);
        m->m_number_.complete++;
        m->m_number_.end = wrk::Utils::time_us();

        return 0;
    }
    virtual int HandleError(int err, IMessage* msg)
    {
        HttpIMessage *m = ((HttpIMessage *)msg);

        LOG_ERROR("http_parser_execute : %d", err + 1);

        if (m != NULL)
        {
            m->m_number_.errors.connect++;
            m->m_number_.end = wrk::Utils::time_us();
        }

        return 0;
    }
};

}

#endif