#pragma once

#include "tiny_util.h"
#include <cassert>

#define MAX_HTTP_HEADERS 40

struct http_header {
  tiny_string name;  // Header name
  tiny_string value; // Header value
};

struct http_message {
  tiny_string method, uri, query, proto; // Request/response line
  http_header headers[MAX_HTTP_HEADERS]; // Headers
  tiny_string body;                      // Body
  tiny_string head;                      // Request + headers
  tiny_string chunk;   // Chunk for chunked encoding, or partial body
  tiny_string message; // Request + headers + body
};

class TinyHttp {
public:
  static inline bool ok(uint8_t c) {
    return c == '\n' || c == '\r' || c >= ' ';
  }

  static inline const char *skip(const char *s, const char *e, const char *d,
                                 tiny_string *v) {
    v->ptr = s;
    while (s < e && *s != '\n' && strchr(d, *s) == NULL)
      s++;
    v->len = (size_t)(s - v->ptr);
    while (s < e && strchr(d, *s) != NULL)
      s++;
    return s;
  }

  static void parseHeaders(const char *s, const char *end, http_header *h,
                           int max_headers) {
    int i;
    for (i = 0; i < max_headers; i++) {
      tiny_string k, v, tmp;
      const char *he = skip(s, end, "\n", &tmp);
      s = skip(s, he, ": \r\n", &k);
      s = skip(s, he, "\r\n", &v);
      if (k.len == tmp.len)
        continue;
      while (v.len > 0 && v.ptr[v.len - 1] == ' ')
        v.len--; // Trim spaces
      if (k.len == 0)
        break;
      h[i].name = k;
      h[i].value = v;
    }
  }

  static tiny_string *getHeader(http_message *h, const char *name) {
    size_t i, n = strlen(name),
              max = sizeof(h->headers) / sizeof(h->headers[0]);
    for (i = 0; i < max && h->headers[i].name.len > 0; i++) {
      tiny_string *k = &h->headers[i].name, *v = &h->headers[i].value;
      if (n == k->len && tiny_ncasecmp(k->ptr, name, n) == 0)
        return v;
    }
    return NULL;
  }

  static int getRequestLength(const unsigned char *buf, size_t buf_len) {
    for (size_t i = 0; i < buf_len; i++) {
      if (!ok(buf[i]))
        return -1;
      if ((i > 0 && buf[i] == '\n' && buf[i - 1] == '\n') ||
          (i > 3 && buf[i] == '\n' && buf[i - 1] == '\r' && buf[i - 2] == '\n'))
        return (int)i + 1;
    }
    return 0;
  }

  int parse(const char *s, size_t len, http_message *hm) {
    int is_response, req_len = getRequestLength((unsigned char *)s, len);
    const char *end = s + req_len, *qs;
    struct tiny_string *cl;
    memset(hm, 0, sizeof(*hm));
    if (req_len <= 0)
      return req_len;

    hm->message.ptr = hm->head.ptr = s;
    hm->body.ptr = end;
    hm->head.len = (size_t)req_len;
    hm->chunk.ptr = end;
    hm->message.len = hm->body.len = (size_t)~0; // Set body length to infinite

    // Parse request line
    s = skip(s, end, " ", &hm->method);
    s = skip(s, end, " ", &hm->uri);
    s = skip(s, end, "\r\n", &hm->proto);

    // Sanity check. Allow protocol/reason to be empty
    if (hm->method.len == 0 || hm->uri.len == 0)
      return -1;

    // If URI contains '?' character, setup query string
    if ((qs = (const char *)memchr(hm->uri.ptr, '?', hm->uri.len)) != NULL) {
      hm->query.ptr = qs + 1;
      hm->query.len = (size_t)(&hm->uri.ptr[hm->uri.len] - (qs + 1));
      hm->uri.len = (size_t)(qs - hm->uri.ptr);
    }

    parseHeaders(s, end, hm->headers,
                 sizeof(hm->headers) / sizeof(hm->headers[0]));
    if ((cl = getHeader(hm, "Content-Length")) != NULL) {
      hm->body.len = (size_t)tiny_to64(*cl);
      hm->message.len = (size_t)req_len + hm->body.len;
    }

    // http_parse() is used to parse both HTTP requests and HTTP
    // responses. If HTTP response does not have Content-Length set, then
    // body is read until socket is closed, i.e. body.len is infinite (~0).
    //
    // For HTTP requests though, according to
    // http://tools.ietf.org/html/rfc7231#section-8.1.3,
    // only POST and PUT methods have defined body semantics.
    // Therefore, if Content-Length is not specified and methods are
    // not one of PUT or POST, set body length to 0.
    //
    // So, if it is HTTP request, and Content-Length is not set,
    // and method is not (PUT or POST) then reset body length to zero.
    is_response = tiny_ncasecmp(hm->method.ptr, "HTTP/", 5) == 0;
    if (hm->body.len == (size_t)~0 && !is_response &&
        tiny_vcasecmp(&hm->method, "PUT") != 0 &&
        tiny_vcasecmp(&hm->method, "POST") != 0) {
      hm->body.len = 0;
      hm->message.len = (size_t)req_len;
    }

    // The 204 (No content) responses also have 0 body length
    if (hm->body.len == (size_t)~0 && is_response &&
        tiny_vcasecmp(&hm->uri, "204") == 0) {
      hm->body.len = 0;
      hm->message.len = (size_t)req_len;
    }

    return req_len;
  }
};