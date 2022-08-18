#pragma once

#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

struct tiny_string {
  const char *ptr; // Pointer to string data
  size_t len;      // String len
};

static tiny_string tiny_str_s(const char *s) {
  tiny_string str = {s, s == NULL ? 0 : strlen(s)};
  return str;
}

static tiny_string tiny_str_n(const char *s, size_t n) {
  struct tiny_string str = {s, n};
  return str;
}

static int tiny_lower(const char *s) {
  int c = *s;
  if (c >= 'A' && c <= 'Z')
    c += 'a' - 'A';
  return c;
}

static int tiny_ncasecmp(const char *s1, const char *s2, size_t len) {
  int diff = 0;
  if (len > 0)
    do {
      diff = tiny_lower(s1++) - tiny_lower(s2++);
    } while (diff == 0 && s1[-1] != '\0' && --len > 0);
  return diff;
}

static int tiny_casecmp(const char *s1, const char *s2) {
  return tiny_ncasecmp(s1, s2, (size_t)~0);
}

static int tiny_vcmp(const tiny_string *s1, const char *s2) {
  size_t n2 = strlen(s2), n1 = s1->len;
  int r = strncmp(s1->ptr, s2, (n1 < n2) ? n1 : n2);
  if (r == 0)
    return (int)(n1 - n2);
  return r;
}

static int tiny_vcasecmp(const tiny_string *str1, const char *str2) {
  size_t n2 = strlen(str2), n1 = str1->len;
  int r = tiny_ncasecmp(str1->ptr, str2, (n1 < n2) ? n1 : n2);
  if (r == 0)
    return (int)(n1 - n2);
  return r;
}

static tiny_string tiny_strdup(const tiny_string s) {
  tiny_string r = {NULL, 0};
  if (s.len > 0 && s.ptr != NULL) {
    char *sc = (char *)calloc(1, s.len + 1);
    if (sc != NULL) {
      memcpy(sc, s.ptr, s.len);
      sc[s.len] = '\0';
      r.ptr = sc;
      r.len = s.len;
    }
  }
  return r;
}

static int tiny_strcmp(const tiny_string str1, const tiny_string str2) {
  size_t i = 0;
  while (i < str1.len && i < str2.len) {
    int c1 = str1.ptr[i];
    int c2 = str2.ptr[i];
    if (c1 < c2)
      return -1;
    if (c1 > c2)
      return 1;
    i++;
  }
  if (i < str1.len)
    return 1;
  if (i < str2.len)
    return -1;
  return 0;
}

const char *tiny_strstr(const tiny_string haystack, const tiny_string needle) {
  size_t i;
  if (needle.len > haystack.len)
    return NULL;
  for (i = 0; i <= haystack.len - needle.len; i++) {
    if (memcmp(haystack.ptr + i, needle.ptr, needle.len) == 0) {
      return haystack.ptr + i;
    }
  }
  return NULL;
}

static bool is_space(int c) {
  return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

static tiny_string tiny_strstrip(tiny_string s) {
  while (s.len > 0 && is_space((int)*s.ptr))
    s.ptr++, s.len--;
  while (s.len > 0 && is_space((int)*(s.ptr + s.len - 1)))
    s.len--;
  return s;
}

static bool tiny_url_safe(int c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-' || c == '~';
}

static char *tiny_hex(const void *buf, size_t len, char *to) {
  const unsigned char *p = (const unsigned char *)buf;
  const char *hex = "0123456789abcdef";
  size_t i = 0;
  for (; len--; p++) {
    to[i++] = hex[p[0] >> 4];
    to[i++] = hex[p[0] & 0x0f];
  }
  to[i] = '\0';
  return to;
}

static size_t tiny_urlencode(const char *s, size_t sl, char *buf, size_t len) {
  size_t i, n = 0;
  for (i = 0; i < sl; i++) {
    int c = *(unsigned char *)&s[i];
    if (n + 4 >= len)
      return 0;
    if (tiny_url_safe(c)) {
      buf[n++] = s[i];
    } else {
      buf[n++] = '%';
      tiny_hex(&s[i], 1, &buf[n]);
      n += 2;
    }
  }
  return n;
}

uint64_t tiny_tou64(tiny_string str) {
  uint64_t result = 0;
  size_t i = 0;
  while (i < str.len && (str.ptr[i] == ' ' || str.ptr[i] == '\t'))
    i++;
  while (i < str.len && str.ptr[i] >= '0' && str.ptr[i] <= '9') {
    result *= 10;
    result += (unsigned)(str.ptr[i] - '0');
    i++;
  }
  return result;
}

int64_t tiny_to64(tiny_string str) {
  int64_t result = 0, neg = 1, max = 922337203685477570 /* INT64_MAX/10-10 */;
  size_t i = 0;
  while (i < str.len && (str.ptr[i] == ' ' || str.ptr[i] == '\t'))
    i++;
  if (i < str.len && str.ptr[i] == '-')
    neg = -1, i++;
  while (i < str.len && str.ptr[i] >= '0' && str.ptr[i] <= '9') {
    if (result > max)
      return 0;
    result *= 10;
    result += (str.ptr[i] - '0');
    i++;
  }
  return result * neg;
}

static const char *tiny_http_status_code(int status_code) {
  switch (status_code) {
  case 100:
    return "Continue";
  case 201:
    return "Created";
  case 202:
    return "Accepted";
  case 204:
    return "No Content";
  case 206:
    return "Partial Content";
  case 301:
    return "Moved Permanently";
  case 302:
    return "Found";
  case 304:
    return "Not Modified";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 418:
    return "I'm a teapot";
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  default:
    return "OK";
  }
}