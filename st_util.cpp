/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_util.h"

ST_NAMESPACE_USING

uint64_t g_st_threadid;

uint64_t get_sthreadid(void)
{
    return g_st_threadid;
}

void set_sthreadid(uint64_t id)
{
    g_st_threadid = id;
}
