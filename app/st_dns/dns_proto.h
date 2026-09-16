/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 共享最小 DNS 编解码（HEADER + QNAME + TYPE_A）。
 * plan/08 D1=b：先内嵌于 st_dnsserver，Phase 3 抽到本头供 server 与单测共用。
 */

#ifndef _ST_DNS_PROTO_H_
#define _ST_DNS_PROTO_H_

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

#define ST_DNS_TYPE_A 1
#define ST_DNS_CLASS_IN 1
#define ST_DNS_HDR_SIZE 12
#define ST_DNS_MAX_NAME 256
#define ST_DNS_MAX_PKT 512
#define ST_DNS_TTL_SEC 60

/* 把点分域名写成 DNS QNAME，返回写入字节数；失败 -1 */
static int st_dns_encode_qname(const char *name, char *out, int out_max) {
  int n = 0;
  const char *p = name;
  if (name == NULL || out == NULL || out_max < 2) {
    return -1;
  }
  if (name[0] == '\0') {
    if (out_max < 1) {
      return -1;
    }
    out[0] = 0;
    return 1;
  }
  while (*p) {
    const char *dot = strchr(p, '.');
    int lablen = dot ? (int)(dot - p) : (int)strlen(p);
    if (lablen <= 0 || lablen > 63 || n + 1 + lablen + 1 > out_max) {
      return -1;
    }
    out[n++] = (char)lablen;
    memcpy(out + n, p, (size_t)lablen);
    n += lablen;
    if (!dot) {
      break;
    }
    p = dot + 1;
    if (*p == '\0') {
      break;
    }
  }
  if (n + 1 > out_max) {
    return -1;
  }
  out[n++] = 0;
  return n;
}

/* 从 off 处解 QNAME 到点分串。不跟随压缩指针（query 侧无压缩）。
 * 返回下一个字段偏移；失败 -1 */
static int st_dns_decode_qname(const char *pkt, int pkt_len, int off,
                               char *name, int name_max) {
  int npos = 0;
  int hops = 0;
  if (pkt == NULL || name == NULL || name_max < 2) {
    return -1;
  }
  name[0] = '\0';
  while (off < pkt_len && hops < 128) {
    unsigned char lab = (unsigned char)pkt[off];
    if (lab == 0) {
      off++;
      if (npos == 0) {
        name[0] = '.';
        name[1] = '\0';
      } else {
        name[npos] = '\0';
      }
      return off;
    }
    if ((lab & 0xC0) == 0xC0) {
      /* 压缩指针：本样例 query 不用；遇到则失败，避免越界 */
      return -1;
    }
    if (lab > 63) {
      return -1;
    }
    off++;
    if (off + lab > pkt_len) {
      return -1;
    }
    if (npos > 0) {
      if (npos + 1 >= name_max) {
        return -1;
      }
      name[npos++] = '.';
    }
    if (npos + lab >= name_max) {
      return -1;
    }
    memcpy(name + npos, pkt + off, (size_t)lab);
    npos += lab;
    off += lab;
    hops++;
  }
  return -1;
}

static int st_dns_name_in_zone(const char *name) {
  size_t n;
  static const char *kSuffix[] = {".bench.local", ".bench.sthread.local", NULL};
  int i;
  if (name == NULL || name[0] == '\0') {
    return 0;
  }
  n = strlen(name);
  for (i = 0; kSuffix[i] != NULL; i++) {
    size_t s = strlen(kSuffix[i]);
    if (n == s - 1 && strcmp(name, kSuffix[i] + 1) == 0) {
      return 1;
    }
    if (n >= s && strcmp(name + (n - s), kSuffix[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

/* 根据 query 组应答。成功返回应答长度；坏包返回 -1（调用方丢弃）。
 * D2=b：非 A → 空 ANSWER + NOERROR。A 且在区内 → 127.0.0.1 */
static int st_dns_build_reply(const char *query, int qlen, char *out,
                              int out_max) {
  int qoff;
  char qname[ST_DNS_MAX_NAME];
  uint16_t qtype, qclass;
  uint16_t flags, numq;
  int rlen;
  int want_a;

  if (query == NULL || out == NULL || qlen < ST_DNS_HDR_SIZE ||
      qlen > ST_DNS_MAX_PKT || out_max < qlen) {
    return -1;
  }

  memcpy(&numq, query + 4, 2);
  numq = ntohs(numq);
  if (numq < 1) {
    return -1;
  }

  qoff = st_dns_decode_qname(query, qlen, ST_DNS_HDR_SIZE, qname,
                             (int)sizeof(qname));
  if (qoff < 0 || qoff + 4 > qlen) {
    return -1;
  }
  memcpy(&qtype, query + qoff, 2);
  memcpy(&qclass, query + qoff + 2, 2);
  qtype = ntohs(qtype);
  qclass = ntohs(qclass);

  memcpy(out, query, (size_t)qlen);
  rlen = qlen;

  memcpy(&flags, query + 2, 2);
  flags = ntohs(flags);
  flags |= 0x8000; /* QR */
  flags |= 0x0400; /* AA */
  flags |= 0x0080; /* RA */
  flags &= (uint16_t)~0x000F; /* RCODE = NOERROR */
  {
    uint16_t f = htons(flags);
    memcpy(out + 2, &f, 2);
  }
  {
    uint16_t zero = 0;
    uint16_t one = htons(1);
    memcpy(out + 6, &zero, 2); /* ANCOUNT，下面按需改 */
    memcpy(out + 8, &zero, 2);
    memcpy(out + 10, &zero, 2);
    memcpy(out + 4, &one, 2); /* QDCOUNT = 1 */
  }

  want_a = (qtype == ST_DNS_TYPE_A && qclass == ST_DNS_CLASS_IN &&
            st_dns_name_in_zone(qname));
  if (!want_a) {
    /* 空 ANSWER + NOERROR（含非 A、以及区外 A） */
    return rlen;
  }

  if (rlen + 16 > out_max) {
    return -1;
  }
  out[rlen++] = (char)0xC0;
  out[rlen++] = (char)0x0C;
  out[rlen++] = 0x00;
  out[rlen++] = (char)ST_DNS_TYPE_A;
  out[rlen++] = 0x00;
  out[rlen++] = (char)ST_DNS_CLASS_IN;
  {
    uint32_t ttl = htonl((uint32_t)ST_DNS_TTL_SEC);
    memcpy(out + rlen, &ttl, 4);
    rlen += 4;
  }
  out[rlen++] = 0x00;
  out[rlen++] = 0x04;
  {
    uint32_t ip = inet_addr("127.0.0.1");
    memcpy(out + rlen, &ip, 4);
    rlen += 4;
  }
  {
    uint16_t an = htons(1);
    memcpy(out + 6, &an, 2);
  }
  return rlen;
}

#endif
