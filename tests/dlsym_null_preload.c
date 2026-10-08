/*
 * 让 HOOK_SYSCALL 里的 dlsym 对一组 syscall 返回 NULL，
 * 从而走到 ST_DIRECT_SYSCALL 回退。测试进程用
 * dlsym(RTLD_DEFAULT, "st_dlsym_null_hits") 确认命中次数。
 * Android 不编这个库（见 tests/Makefile）。
 */
#if defined(__APPLE__)
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <dlfcn.h>
#include <string.h>

int st_dlsym_null_hits = 0;

static int is_hooked(const char *symbol) {
  static const char *names[] = {
      "socket", "close", "connect",    "read",  "write", "sendto", "recvfrom",
      "send",   "recv",  "setsockopt", "fcntl", "ioctl", "accept", NULL};
  int i;
  if (symbol == NULL) {
    return 0;
  }
  for (i = 0; names[i] != NULL; i++) {
    if (strcmp(symbol, names[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

typedef void *(*dlsym_fn)(void *, const char *);

#if defined(__APPLE__)

#define DYLD_INTERPOSE(_repl, _orig)                                           \
  __attribute__((used)) static struct {                                        \
    const void *replacement;                                                   \
    const void *replacee;                                                      \
  } _interpose_##_orig __attribute__((section("__DATA,__interpose"))) = {      \
      (const void *)(unsigned long)&_repl,                                     \
      (const void *)(unsigned long)&_orig}

static void *hook_dlsym(void *handle, const char *symbol) {
  static dlsym_fn fn = 0;
  static int resolving = 0;
  if (is_hooked(symbol)) {
    st_dlsym_null_hits++;
    return 0;
  }
  if (fn == 0 && !resolving) {
    resolving = 1;
    fn = (dlsym_fn)dlsym(RTLD_NEXT, "dlsym");
    resolving = 0;
  }
  if (fn == 0) {
    return 0;
  }
  return fn(handle, symbol);
}

DYLD_INTERPOSE(hook_dlsym, dlsym);

#else

static dlsym_fn real_dlsym(void) {
  static dlsym_fn fn = 0;
  /* x86_64 上 dlsym 是 GLIBC_2.2.5；aarch64 是 GLIBC_2.17。 */
  static const char *vers[] = {"GLIBC_2.2.5", "GLIBC_2.17", "GLIBC_2.34", NULL};
  int i;
  if (fn != 0) {
    return fn;
  }
  for (i = 0; vers[i] != NULL; i++) {
    fn = (dlsym_fn)dlvsym(RTLD_NEXT, "dlsym", vers[i]);
    if (fn != 0) {
      return fn;
    }
  }
  return 0;
}

void *dlsym(void *handle, const char *symbol) {
  dlsym_fn fn;
  if (is_hooked(symbol)) {
    st_dlsym_null_hits++;
    return 0;
  }
  fn = real_dlsym();
  if (fn == 0) {
    return 0;
  }
  return fn(handle, symbol);
}

#endif
