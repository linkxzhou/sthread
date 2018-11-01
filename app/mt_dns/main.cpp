#include <string.h>
#include <string>
#include <sstream>
#include <iostream>
#include <netdb.h>
#include "dns.h"

MT_DNS_NAMESPACE_USING

void func(void *args)
{
    std::string *str = (std::string*)args;
    time_t ttl = 0;

    std::vector<int32_t> vc;
    int ret = GetInstance<DNS>()->dns_lookup(str->c_str(), vc, &ttl);
    if (ret < 0) 
    {
        LOG_ERROR("%s, make dns query failed", str->c_str());
        return ;
    }
    for (std::vector<int32_t>::iterator iter = vc.begin(); 
        iter != vc.end(); iter++) 
    {
        struct in_addr addr;
        memcpy(&addr, &(*iter), sizeof(struct in_addr));
        LOG_DEBUG("%s, %d, %s", str->c_str(), *iter, inet_ntoa(addr));
    }
    LOG_DEBUG("ttl : %ld", ttl);
}

int main(int argc, char* argv[])
{
    int ret = mt_init_frame();
    LOG_DEBUG("init ret : %d", ret);
    mt_set_hook_flag();
    mt_set_timeout(100);

    Frame *frame = GetInstance<Frame>();

    // 测试 : 使用协程请求耗时
    long start = Utils::system_ms(), end = 0;
    LOG_DEBUG("--- start time : %ld", start);
    for (int i = 2000; i < 2150; i++)
    {
        std::stringstream ss;
        ss << "www." << i << ".com";
        std::string *s = new std::string(ss.str());
        Frame::CreateThread(func, s);
    }
    Frame::Loop(true);
    end = Utils::system_ms();
    LOG_DEBUG("--- end time : %ld", end);

    LOG_DEBUG("cost time : %ld", end - start);

    return 0;
}