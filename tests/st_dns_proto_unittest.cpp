#include "app/st_dns/dns_proto.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

static int make_query(const char *name, uint16_t qtype, char *buf, int buf_max) {
  int nlen;
  uint16_t id = htons(1);
  uint16_t flags = htons(0x0100);
  uint16_t one = htons(1);
  uint16_t zero = 0;
  uint16_t qt, qc;
  int pos = 0;
  if (buf_max < ST_DNS_HDR_SIZE + 64) {
    return -1;
  }
  memcpy(buf + pos, &id, 2);
  pos += 2;
  memcpy(buf + pos, &flags, 2);
  pos += 2;
  memcpy(buf + pos, &one, 2);
  pos += 2;
  memcpy(buf + pos, &zero, 2);
  pos += 2;
  memcpy(buf + pos, &zero, 2);
  pos += 2;
  memcpy(buf + pos, &zero, 2);
  pos += 2;
  nlen = st_dns_encode_qname(name, buf + pos, buf_max - pos);
  if (nlen < 0) {
    return -1;
  }
  pos += nlen;
  qt = htons(qtype);
  qc = htons(ST_DNS_CLASS_IN);
  memcpy(buf + pos, &qt, 2);
  pos += 2;
  memcpy(buf + pos, &qc, 2);
  pos += 2;
  return pos;
}

TEST(StStatus, DnsQnameRoundtrip) {
  char wire[ST_DNS_MAX_NAME];
  char name[ST_DNS_MAX_NAME];
  int n = st_dns_encode_qname("www.1.bench.local", wire, (int)sizeof(wire));
  ASSERT_TRUE(n > 1);
  ASSERT_TRUE(st_dns_decode_qname(wire, n, 0, name, (int)sizeof(name)) == n);
  ASSERT_TRUE(strcmp(name, "www.1.bench.local") == 0);
  ASSERT_TRUE(st_dns_name_in_zone(name) == 1);
  ASSERT_TRUE(st_dns_name_in_zone("example.com") == 0);
}

TEST(StStatus, DnsAReplyInZone) {
  char q[ST_DNS_MAX_PKT];
  char r[ST_DNS_MAX_PKT];
  int qlen = make_query("www.1.bench.local", ST_DNS_TYPE_A, q, (int)sizeof(q));
  ASSERT_TRUE(qlen > ST_DNS_HDR_SIZE);
  int rlen = st_dns_build_reply(q, qlen, r, (int)sizeof(r));
  ASSERT_TRUE(rlen > qlen);
  uint16_t an = 0, flags = 0;
  memcpy(&an, r + 6, 2);
  memcpy(&flags, r + 2, 2);
  an = ntohs(an);
  flags = ntohs(flags);
  ASSERT_TRUE(an == 1);
  ASSERT_TRUE((flags & 0x8000) != 0);
  ASSERT_TRUE((flags & 0x000F) == 0);
  uint32_t ip = 0;
  memcpy(&ip, r + rlen - 4, 4);
  ASSERT_TRUE(ip == inet_addr("127.0.0.1"));
}

TEST(StStatus, DnsNonAEmptyAnswer) {
  char q[ST_DNS_MAX_PKT];
  char r[ST_DNS_MAX_PKT];
  int qlen = make_query("www.1.bench.local", 28 /* AAAA */, q, (int)sizeof(q));
  int rlen = st_dns_build_reply(q, qlen, r, (int)sizeof(r));
  ASSERT_TRUE(rlen == qlen);
  uint16_t an = 0, flags = 0;
  memcpy(&an, r + 6, 2);
  memcpy(&flags, r + 2, 2);
  ASSERT_TRUE(ntohs(an) == 0);
  ASSERT_TRUE((ntohs(flags) & 0x000F) == 0);
}

TEST(StStatus, DnsBadPacketDropped) {
  char r[ST_DNS_MAX_PKT];
  ASSERT_TRUE(st_dns_build_reply("xx", 2, r, (int)sizeof(r)) < 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
