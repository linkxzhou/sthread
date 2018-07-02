#include "dns.h"

MT_DNS_NAMESPACE_USING

//域名转化
static int ch_name(const char *fname, char *tname)
{
    int j = 0;
    int i = strlen(fname) - 1;
    tname[i + 2] = 0;
    int k = i + 1;
    for (; i >= 0; i--, k--) 
    {
        if (fname[i] == '.') 
        {
            tname[k] = j;
            j = 0;
        }
        else 
        {
            tname[k] = fname[i];
            j++;
        }
    }
    tname[k] = j;

    return strlen(tname) + 1;
}
//设置dns包头
int DNS::set_dns_head()
{
    memset(m_send_buf_, 0, sizeof(dns_head_type));

    //设置头部
    dns_head_type *dns_head = (dns_head_type *)m_send_buf_;
    dns_head->ID = (unsigned short)1;
    dns_head->tag = htons(0x0100);
    dns_head->numQ = htons(0x0001);
    dns_head->numA = 0x0000;
    dns_head->numA1 = 0x0000;
    dns_head->numA2 = 0x0000;

    dns_query_type *dns_query = (dns_query_type *)(m_send_buf_ + sizeof(dns_head_type));
    int name_len = ch_name(m_dns_query_node_, (char *)dns_query);

    //设置查询信息
    dns_query = (dns_query_type *)((char *)dns_query + name_len);
    dns_query->classes = htons(0x0001);
    dns_query->type = htons(0x0001);

    return 0;
}

/***
 * 输入：
 *  node    const char *    域名
 *  buf     char*           dns请求对应的结构体
 *  query_len   int*        dns请求的长度
 ***/
int DNS::make_dns_query_format()
{
    if (NULL == m_dns_query_node_ || strlen(m_dns_query_node_) > DOMAIN_MAX_SIZE) 
    {
        LOG_ERROR("invalid argument node, %s", m_dns_query_node_);
        return -1;
    }

    set_dns_head();
    return sizeof(dns_head_type) + sizeof(dns_query_type) + 
        strlen(m_dns_query_node_) + 2;
}

int DNS::dns_lookup(const char *node, std::vector<int32_t> &vc, time_t *ttl, int timeout)
{
    m_dns_query_node_ = node;
    m_timeout_ = timeout;

    struct sockaddr_in addr;
    int ret = -1;
    dns_head_type *dns_head;
    int off = 0, num;

#ifdef WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    addr.sin_family = AF_INET;
    if (m_dns_svr_ == NULL)
    {
        addr.sin_addr.s_addr = inet_addr(PUBLIC_DNS_DEFAULT_SERVER);  ///服务器ip
    }
    else
    {
        addr.sin_addr.s_addr = inet_addr(m_dns_svr_);  ///服务器ip
    }
    addr.sin_port = htons((uint16_t)PUBLIC_DNS_DEFAULT_PORT);

    int query_len = make_dns_query_format();
    if (query_len <= 0) 
    {
        return -1;
    }

    int recv_len = sizeof(m_recv_buf_);
    ret = udp_sendrecv(&addr, m_send_buf_, query_len, m_recv_buf_, recv_len, m_timeout_);
    LOG_DEBUG("ret : %d, recv_buf : %s, recv_len : %d", ret, m_recv_buf_, recv_len);

    int Anum = 0;
    //只支持A记录
    dns_head = (dns_head_type *)m_recv_buf_;
    num = DNS_GET16(dns_head->numA);
    for (int i = 0; i < num; i++) 
    {
        char *result_set = m_recv_buf_ + query_len + off;
        response *rp = (response *)(result_set + 2); //2 bytes' offsets
        uint16_t type = DNS_GET16(rp->type);
        *ttl = DNS_GET32(rp->ttl);
        // 解析A记录
        if (TYPE_A == type) 
        {
            // memcpy(m_recv_buf_ + Anum * 4, (char *)(rp + 1), 4);
            vc.push_back(*(int32_t*)(rp + 1));
            Anum++;
            off += (2 + sizeof(response) + 4);
        }
        else if (TYPE_CNAME == type) 
        {
            // 如果是CNAME记录则直接查找下一条记录
            off += (2 + sizeof(response) + DNS_GET16(rp->length));
        }
        else 
        {
            goto clear;
        }
    }
    if (Anum == 0)
    {
        ret = -1;
    }

clear:
    return ret;
}