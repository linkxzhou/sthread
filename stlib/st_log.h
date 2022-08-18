#ifndef _ST_LOG_H_
#define _ST_LOG_H_

#include "ucontext/st_def.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace stlib {

#define LLOG_EMERG 0   /* system in unusable */
#define LLOG_ALERT 1   /* action must be taken immediately */
#define LLOG_CRIT 2    /* critical conditions */
#define LLOG_ERR 3     /* error conditions */
#define LLOG_WARN 4    /* warning conditions */
#define LLOG_NOTICE 5  /* normal but significant condition (default) */
#define LLOG_INFO 6    /* informational */
#define LLOG_DEBUG 7   /* debug messages */
#define LLOG_VERB 8    /* verbose messages */
#define LLOG_VVERB 9   /* verbose messages on crack */
#define LLOG_VVVERB 10 /* verbose messages on ganga */
#define LLOG_PVERB 11  /* periodic verbose messages on crack */

#define LOG_MAX_LEN 8192 /* max length of log message */

class StLogger {
public:
  StLogger();

  ~StLogger();

  int Init(int32_t level, char *filename);

  void Reopen();

  void SetLevel(int32_t level);

  void Stacktrace();

  int LogAble(int32_t level);

  void __log(const char *file, int32_t line, int32_t level, const char *fmt,
             ...);

  void __loga(const char *fmt, ...);

  inline static StLogger &Instance() {
    static StLogger logger;
    return logger;
  }

  // 日志特殊的indexOf和lastOf
  static int StringIndexOf(const char *s, char c);

  static int StringLastOf(const char *s, char c);

private:
  char *m_name_;
  int m_level_, m_fd_, m_nerror_;
};

#define LOG_ERROR(...)                                                         \
  do {                                                                         \
    if (StLogger::Instance().LogAble(LLOG_ALERT) != 0) {                       \
      StLogger::Instance().__log(__FILE__, __LINE__, LLOG_ALERT,               \
                                 ##__VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOG_WARN(...)                                                          \
  do {                                                                         \
    if (StLogger::Instance().LogAble(LLOG_WARN) != 0) {                        \
      StLogger::Instance().__log(__FILE__, __LINE__, LLOG_WARN,                \
                                 ##__VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOG_PANIC(...)                                                         \
  do {                                                                         \
    if (StLogger::Instance().LogAble(LLOG_EMERG) != 0) {                       \
      StLogger::Instance().__log(__FILE__, __LINE__, LLOG_EMERG,               \
                                 ##__VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOG_DEBUG(...)                                                         \
  do {                                                                         \
    if (StLogger::Instance().LogAble(LLOG_VVVERB) != 0) {                      \
      StLogger::Instance().__log(__FILE__, __LINE__, LLOG_VVVERB,              \
                                 ##__VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOG_TRACE(...)                                                         \
  do {                                                                         \
    if (StLogger::Instance().LogAble(LLOG_PVERB) != 0) {                       \
      StLogger::Instance().__log(__FILE__, __LINE__, LLOG_PVERB,               \
                                 ##__VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOGA(...)                                                              \
  do {                                                                         \
    if (StLogger::Instance().LogAble(LLOG_PVERB) != 0) {                       \
      StLogger::Instance().__loga(##__VA_ARGS__);                              \
    }                                                                          \
  } while (0)

#define LOG_LEVEL(level) StLogger::Instance().SetLevel(level)

#define LOG_FUNCMARK(cls)                                                      \
  LOG_DEBUG("<<< mark function(%s: %s) >>>", #cls, __FUNCTION__)

#define LOG_ASSERT(exp) assert((exp))

} // namespace stlib

#endif