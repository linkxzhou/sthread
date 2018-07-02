#include "mt_utils.h"

MTHREAD_NAMESPACE_USING

unsigned long       g_mt_threadid;

unsigned long mt_get_threadid(void)
{
    return g_mt_threadid;
}

void mt_set_threadid(unsigned long id)
{
    g_mt_threadid = g_mt_threadid;
}
