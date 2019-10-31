/*
 * Copyright (C) zhoulv2000@163.com
 * refer to wrk(https://github.com/wg/wrk)
 */

#ifndef __STATS_H___
#define __STATS_H___

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
    uint64_t end;
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
    char    *path;
    char    *query;
} config;

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

#endif /* _STATS_H__ */