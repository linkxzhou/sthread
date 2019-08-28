/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_NET_H_INCLUDED_
#define _ST_NET_H_INCLUDED_

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include "st_util.h"

ST_NAMESPACE_BEGIN

class StNetAddress
{
public:
    StNetAddress() : 
        m_errno_(0), 
        m_isipv6_(false)
    { }

    explicit StNetAddress(const struct sockaddr_in& addr) : 
        m_addr_(addr), 
        m_errno_(0),
        m_isipv6_(false)
    { }

    explicit StNetAddress(const struct sockaddr_in6& addr) : 
        m_addr6_(addr), 
        m_errno_(0),
        m_isipv6_(false)
    { }

    inline void SetAddr(const char *hostname)
    {
        struct hostent* he = NULL;

        he = ::gethostbyname(hostname);
        if (he != NULL)
        {
            assert(he->h_addrtype == AF_INET && he->h_length == sizeof(uint32_t));
            m_addr_.sin_addr = *reinterpret_cast<struct in_addr*>(he->h_addr);
        }
        else
        {
            m_errno_ = -1;
        }
    }

    inline void SetAddr(uint16_t port = 0, 
        bool loopback_only = false, 
        bool is_ipv6 = false)
    {
        if (is_ipv6)
        {
            m_isipv6_ = true;
            bzero(&m_addr6_, sizeof m_addr6_);
            m_addr6_.sin6_family = AF_INET6;
            in6_addr ip = loopback_only ? in6addr_loopback : in6addr_any;
            m_addr6_.sin6_addr = ip;
            m_addr6_.sin6_port = htons(port);
        }
        else
        {
            bzero(&m_addr_, sizeof m_addr_);
            m_addr_.sin_family = AF_INET;
            in_addr_t ip = loopback_only ? INADDR_LOOPBACK : INADDR_ANY;
            m_addr_.sin_addr.s_addr = htonl(ip);
            m_addr_.sin_port = htons(port);
        }
    }

    inline void SetAddr(const char *ip, 
        uint16_t port, 
        bool is_ipv6 = false)
    {
        if (is_ipv6)
        {
            m_isipv6_ = true;
            bzero(&m_addr6_, sizeof m_addr6_);
            m_addr6_.sin6_family = AF_INET;
            m_addr6_.sin6_port = htons(port);
            if (::inet_pton(AF_INET, ip, &m_addr6_.sin6_addr) <= 0)
            {
                m_errno_ = -1;
            }
        }
        else
        {
            bzero(&m_addr_, sizeof m_addr_);
            m_addr_.sin_family = AF_INET;
            m_addr_.sin_port = htons(port);
            if (::inet_pton(AF_INET, ip, &m_addr_.sin_addr) <= 0)
            {
                m_errno_ = -1;
            }
        }
    }

    inline const char* IP() const
    {
        static char buf[64] = "";
        socklen_t size = sizeof(buf);

        if (!m_isipv6_)
        {
            assert(size >= INET_ADDRSTRLEN);
            const struct sockaddr_in* addr4 = static_cast<const struct sockaddr_in*>(&m_addr_);
            ::inet_ntop(AF_INET, &addr4->sin_addr, buf, static_cast<socklen_t>(size));
        }
        else
        {
            assert(size >= INET6_ADDRSTRLEN);
            const struct sockaddr_in6* addr6 = static_cast<const struct sockaddr_in6*>(&m_addr6_);
            ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, static_cast<socklen_t>(size));
        }

        return buf;
    }

    inline const char* IPPort() const
    {
        static char buf[64] = "";
        socklen_t size = sizeof(buf);

        memcpy(buf, IP(), size);
        size_t end = ::strlen(buf);
        const struct sockaddr_in* addr4 = static_cast<const struct sockaddr_in*>(&m_addr_);
        uint16_t port = ntohs(addr4->sin_port);
        snprintf(buf + end, size - end, ":%u", port);

        return buf;
    }

    inline uint16_t Port() const
    {
        return ntohs(PortNetEndian());
    }

    inline void GetSockAddr(struct sockaddr* &addr) const 
    { 
        addr = (struct sockaddr*)(&m_addr_);
    }

    inline void GetSockAddr(struct sockaddr_in6* &addr) const 
    { 
        addr = (struct sockaddr_in6*)(&m_addr6_);
    }

    inline uint16_t PortNetEndian() const 
    { 
        return m_addr_.sin_port; 
    }

    inline bool IsError()
    {
        return m_errno_ == 0 ? false : true;
    }

private:
    union
    {
        struct sockaddr_in m_addr_;
        struct sockaddr_in6 m_addr6_;
    };

    int m_errno_;
    bool m_isipv6_;
};

ST_NAMESPACE_END

#endif