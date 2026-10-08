/*
 * Copyright (C) zhoulv2000@163.com
 *
 * RESP 子集，只给 st_redisclient 和离线单测用。不进 libmthread，没有 hiredis。
 * 能编码命令数组，能解码简单字符串、错误、整数、批量、空批量，以及一层数组。
 */

#ifndef _ST_REDIS_RESP_H_
#define _ST_REDIS_RESP_H_

enum RespType {
  RESP_NONE = 0,
  RESP_SIMPLE = 1,
  RESP_ERROR = 2,
  RESP_INT = 3,
  RESP_BULK = 4,
  RESP_ARRAY = 5,
  RESP_NULL = 6
};

struct RespValue {
  int type;
  long long i;
  char *buf;
  int len;
  RespValue *elem;
  int n;
};

/* 流式解码。把多次喂入的字节拼起来，凑齐一条值再返回。 */
struct RespParser {
  char *buf;
  int len;
  int cap;
};

void resp_parser_init(RespParser *p);
void resp_parser_free(RespParser *p);

/* 1 凑齐一条，写入 *out（调用方 resp_free）。0 还要更多字节。-1 协议错误。 */
int resp_parse(RespParser *p, const char *data, int n, RespValue *out);

void resp_free(RespValue *v);

/* 写成 *<argc>\r\n$<len>\r\n...\r\n。返回字节数，放不下返回 -1。 */
int resp_encode(char *out, int cap, const char *const *argv, int argc);

#endif
