/*
 * Copyright (C) zhoulv2000@163.com
 * refer to wrk(https://github.com/wg/wrk)
 */

#ifndef __STATS_H_INCLUDED__
#define __STATS_H_INCLUDED__

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include "mt_action.h"

namespace wrk
{

#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

typedef struct 
{
    uint32_t connect;
    uint32_t read;
    uint32_t write;
    uint32_t status;
    uint32_t timeout;
} errors;

typedef struct 
{
    uint64_t count;
    uint64_t limit;
    uint64_t min;
    uint64_t max;
    uint64_t data[];
} stats;

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
    uint64_t port;
} config;

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
    std::string m_host_;
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
        snprintf(request_buf, sizeof(request_buf) - 1, "GET / HTTP/1.1\r\nHost: %s\r\n"
            "User-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n", m->m_host_.c_str()) ;
        len = strlen(request_buf);
        memcpy(buf, request_buf, len);
        ((char *)buf)[len] = '\0';

        http_parser_init(m->m_parser_, HTTP_REQUEST);
        m->m_number_.requests++;

        return 0;
    }
    virtual int HandleInput(void* buf, int len, IMessage* msg)
    {
        HttpIMessage *m = ((HttpIMessage *)msg);

        http_parser_init(m->m_parser_, HTTP_RESPONSE); // 初始化parser为Response类型
        int http_len = http_parser_execute(m->m_parser_, 
            &settings_null, (const char *)buf, len);
        
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

        return 0;
    }
    virtual int HandleError(int err, IMessage* msg)
    {
        HttpIMessage *m = ((HttpIMessage *)msg);

        LOG_ERROR("http_parser_execute : %d", err + 1);

        if (m != NULL)
        {
            m->m_number_.errors.connect++;
        }

        return 0;
    }
};

class StatsCalculate
{
public:
    StatsCalculate(uint64_t max) : m_stats_(NULL)
    {
        uint64_t limit = max + 1;
        m_stats_ = (stats *)calloc(1, sizeof(stats) + sizeof(uint64_t) * limit);
        m_stats_->limit = limit;
        m_stats_->min   = UINT64_MAX;
    }

    ~StatsCalculate()
    {
        if (NULL != m_stats_)
        {
            ::free(m_stats_);
            m_stats_ = NULL;
        }
    }

    int record(uint64_t n) 
    {
        if (n >= m_stats_->limit) return 0;
        __sync_fetch_and_add(&m_stats_->data[n], 1);
        __sync_fetch_and_add(&m_stats_->count, 1);
        uint64_t min = m_stats_->min;
        uint64_t max = m_stats_->max;
        while (n < min) min = __sync_val_compare_and_swap(&m_stats_->min, min, n);
        while (n > max) max = __sync_val_compare_and_swap(&m_stats_->max, max, n);
        return 1;
    }

    void correct(int64_t expected) 
    {
        for (uint64_t n = expected * 2; n <= m_stats_->max; n++) 
        {
            uint64_t count = m_stats_->data[n];
            int64_t m = (int64_t) n - expected;
            while (count && m > expected) 
            {
                m_stats_->data[m] += count;
                m_stats_->count += count;
                m -= expected;
            }
        }
    }

    long double mean() 
    {
        if (m_stats_->count == 0) return 0.0;

        uint64_t sum = 0;
        for (uint64_t i = m_stats_->min; i <= m_stats_->max; i++) 
        {
            sum += m_stats_->data[i] * i;
        }

        return sum / (long double) m_stats_->count;
    }

    long double stdev(long double mean) {
        long double sum = 0.0;
        if (m_stats_->count < 2) return 0.0;
        for (uint64_t i = m_stats_->min; i <= m_stats_->max; i++) 
        {
            if (m_stats_->data[i]) 
            {
                sum += ::powl(i - mean, 2) * m_stats_->data[i];
            }
        }

        return ::sqrtl(sum / (m_stats_->count - 1));
    }

    long double within_stdev(long double mean, long double stdev, uint64_t n) 
    {
        long double upper = mean + (stdev * n);
        long double lower = mean - (stdev * n);
        uint64_t sum = 0;

        for (uint64_t i = m_stats_->min; i <= m_stats_->max; i++) 
        {
            if (i >= lower && i <= upper) 
            {
                sum += m_stats_->data[i];
            }
        }

        return (sum / (long double) m_stats_->count) * 100;
    }

    uint64_t percentile(long double p) 
    {
        uint64_t rank = ::round((p / 100.0) * m_stats_->count + 0.5);
        uint64_t total = 0;
        for (uint64_t i = m_stats_->min; i <= m_stats_->max; i++) 
        {
            total += m_stats_->data[i];
            if (total >= rank) return i;
        }

        return 0;
    }

    uint64_t popcount() 
    {
        uint64_t count = 0;
        for (uint64_t i = m_stats_->min; i <= m_stats_->max; i++) 
        {
            if (m_stats_->data[i]) count++;
        }

        return count;
    }

    uint64_t value_at(uint64_t index, uint64_t *count) 
    {
        *count = 0;
        for (uint64_t i = m_stats_->min; i <= m_stats_->max; i++) 
        {
            if (m_stats_->data[i] && (*count)++ == index) 
            {
                *count = m_stats_->data[i];
                return i;
            }
        }

        return 0;
    }

    stats* get_stats()
    {
        return m_stats_;
    }

private:
    stats* m_stats_;
};

}

#endif /* _STATS_H_INCLUDED_ */