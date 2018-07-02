/*
 * Copyright (C) zhoulv2000@163.com
 * refer to wrk(https://github.com/wg/wrk)
 */

#ifndef _UTILS_H_INCLUDED_
#define _UTILS_H_INCLUDED_

#include "stats.h"

typedef struct 
{
    int scale;
    char *base;
    char *units[];
} units;

/******
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
*******/

class Utils
{
public:
    static void print_stats_header() 
    {
        printf("  Thread Stats%6s%11s%8s%12s\n", "Avg", "Stdev", "Max", "+/- Stdev");
    }

    static void print_units(long double n, char *(*fmt)(long double), int width) 
    {
        char *msg = fmt(n);
        int len = strlen(msg), pad = 2;

        if (isalpha(msg[len-1])) pad--;
        if (isalpha(msg[len-2])) pad--;
        width -= pad;

        printf("%*.*s%.*s", width, width, msg, pad, "  ");

        ::free(msg);
    }

    static void print_stats(char *name, StatsCalculate *sc, char *(*fmt)(long double)) 
    {
        uint64_t max = sc->get_stats()->max;
        long double mean  = sc->mean();
        long double stdev = sc->stdev(mean);

        printf("    %-10s", name);
        print_units(mean,  fmt, 8);
        print_units(stdev, fmt, 10);
        print_units(max,   fmt, 9);
        printf("%8.2Lf%%\n", sc->within_stdev(mean, stdev, 1));
    }

    static void print_stats_latency(StatsCalculate *sc) 
    {
        long double percentiles[] = { 50.0, 75.0, 90.0, 99.0 };
        printf("  Latency Distribution\n");
        for (size_t i = 0; i < sizeof(percentiles) / sizeof(long double); i++) 
        {
            long double p = percentiles[i];
            uint64_t n = sc->percentile(p);
            printf("%7.0Lf%%", p);
            print_units(n, format_time_us, 10);
            printf("\n");
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

        if ((c = sscanf(s, "%"SCNu64"%2s", &base, unit)) < 1) return -1;

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
        gettimeofday(&t, NULL);
        return (t.tv_sec * 1000000) + t.tv_usec;
    }
};

#endif