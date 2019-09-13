#ifndef _DNS53_H_
#define _DNS53_H_

#include "mt_action.h"
#include <vector>
#include <string>

#define TYPE_A      1   /* a host address */
#define TYPE_CNAME  5   /* the canonical name for an alias */

#define DNS_GET16(num) ((((uint16_t)(num))>>8) | ((uint16_t)((num)<<8)))
#define DNS_GET32(num) ((num >> 24)|((num >>8)&0x0000ff00)|((num << 8)&0x00ff0000)|(num << 24));

#define DOMAIN_MAX_SIZE             256
#define PUBLIC_DNS_DEFAULT_SERVER   "8.8.8.8"
#define PUBLIC_DNS_DEFAULT_PORT     53
#define DNS_DEFAULT_DATA_SIZE       512
#define DNS_TIMEOUT                 10000

#define MT_DNS_NAMESPACE_BEGIN namespace mt_dns {
#define MT_DNS_NAMESPACE_END   }
#define MT_DNS_NAMESPACE_USING using namespace mt_dns;

MT_DNS_NAMESPACE_BEGIN

//用于dns解析的结构体
typedef struct _dns_head_info 
{ //dns 头部
    unsigned short ID;
    unsigned short tag; // dns 标志(参数)
    unsigned short numQ; // 问题数
    unsigned short numA; // 答案数
    unsigned short numA1; // 权威答案数
    unsigned short numA2; // 附加答案数
} dns_head_type;

typedef struct _dns_query_info 
{ //dns 查询结构
    //char name[64];
    //查询的域名,这是一个大小在0到63之间的字符串；
    unsigned short type;
    //查询类型，大约有20个不同的类型
    unsigned short classes;
    //查询类,通常是A类既查询IP地址
} dns_query_type;

#ifdef WIN32

#pragma pack(push) //保存对齐状态
#pragma pack(2) // 2 bytes对齐
typedef struct dns_response 
{ //DNS响应数据报：
    unsigned short type; //查询类型
    unsigned short classes; //类型码
    unsigned int ttl; //生存时间
    unsigned short length; //资源数据长度
} response;
#pragma pack(pop) //恢复对齐状态

#else
typedef struct dns_response 
{ //DNS响应数据报：
    unsigned short type __attribute__((packed)); //查询类型
    unsigned short classes  __attribute__((packed)); //类型码
    unsigned int ttl __attribute__((packed)); //生存时间
    unsigned short length __attribute__((packed)); //资源数据长度
} response;
#endif

class DNS
{
public:
    DNS() : m_dns_svr_(NULL), m_timeout_(DNS_TIMEOUT)
    { 
        memset(m_send_buf_, 0, sizeof(m_send_buf_));
        memset(m_recv_buf_, 0, sizeof(m_recv_buf_));
    }
    ~DNS()
    {
        safe_free(m_dns_svr_);
    }
    int dns_lookup(const char *node, std::vector<int32_t> &vc, 
        time_t *ttl, int timeout = DNS_TIMEOUT);

    inline void set_dns_svr(const char *_ip)
    {
        m_dns_svr_ = strdup(_ip);
    }

private:
    int set_dns_head();
    int make_dns_query_format();

private:
    char *m_dns_svr_; // dns解析的ip
    char m_send_buf_[DNS_DEFAULT_DATA_SIZE], m_recv_buf_[DNS_DEFAULT_DATA_SIZE*4];
    int m_timeout_;
    const char *m_dns_query_node_;
};

MT_DNS_NAMESPACE_END

#endif