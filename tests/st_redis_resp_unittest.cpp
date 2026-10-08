/*
 * Copyright (C) zhoulv2000@163.com
 *
 * RESP 编解码，不联网。拆包边界喂两次 resp_parse。
 */

#include "app/st_redisclient/resp.h"
#include "tests/st_test_compat.h"
#include <string.h>

using namespace stlib;

static void expect_text(const RespValue *v, int type, const char *text) {
  ASSERT_EQ((int)type, v->type);
  ASSERT_TRUE(v->buf != NULL);
  ASSERT_EQ(0, strcmp(v->buf, text));
}

TEST(StStatus, EncodePing) {
  const char *argv[1];
  char out[64];
  int n;
  argv[0] = "PING";
  n = resp_encode(out, (int)sizeof(out), argv, 1);
  ASSERT_TRUE(n > 0);
  ASSERT_EQ(0, strcmp(out, "*1\r\n$4\r\nPING\r\n"));
}

TEST(StStatus, DecodeSimpleErrorIntBulkNull) {
  RespParser p;
  RespValue v;
  resp_parser_init(&p);

  ASSERT_EQ(1, resp_parse(&p, "+PONG\r\n", 7, &v));
  expect_text(&v, RESP_SIMPLE, "PONG");
  resp_free(&v);

  ASSERT_EQ(1, resp_parse(&p, "-ERR x\r\n", 8, &v));
  expect_text(&v, RESP_ERROR, "ERR x");
  resp_free(&v);

  ASSERT_EQ(1, resp_parse(&p, ":1\r\n", 4, &v));
  ASSERT_EQ((int)RESP_INT, v.type);
  ASSERT_EQ(1, (int)v.i);
  resp_free(&v);

  ASSERT_EQ(1, resp_parse(&p, "$3\r\nbar\r\n", 9, &v));
  expect_text(&v, RESP_BULK, "bar");
  ASSERT_EQ(3, v.len);
  resp_free(&v);

  ASSERT_EQ(1, resp_parse(&p, "$-1\r\n", 5, &v));
  ASSERT_EQ((int)RESP_NULL, v.type);
  resp_free(&v);

  resp_parser_free(&p);
}

TEST(StStatus, DecodeSplitTypeByte) {
  RespParser p;
  RespValue v;
  resp_parser_init(&p);
  ASSERT_EQ(0, resp_parse(&p, "$", 1, &v));
  ASSERT_EQ(1, resp_parse(&p, "3\r\nbar\r\n", 8, &v));
  expect_text(&v, RESP_BULK, "bar");
  resp_free(&v);
  resp_parser_free(&p);
}

TEST(StStatus, DecodeSplitArray) {
  RespParser p;
  RespValue v;
  const char *rest = "1\r\n$4\r\nPONG\r\n";
  resp_parser_init(&p);
  ASSERT_EQ(0, resp_parse(&p, "*", 1, &v));
  ASSERT_EQ(1, resp_parse(&p, rest, (int)strlen(rest), &v));
  ASSERT_EQ((int)RESP_ARRAY, v.type);
  ASSERT_EQ(1, v.n);
  expect_text(&v.elem[0], RESP_BULK, "PONG");
  resp_free(&v);
  resp_parser_free(&p);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
