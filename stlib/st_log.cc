#include "st_log.h"

using namespace stlib;

static const char *g_level_cn[12] = {"EMERG", "ALERT",  "CRIT",   "ERR",
                                     "WARN",  "NOTICE", "INFO",   "DEBUG",
                                     "VERB",  "VVERB",  "VVVERB", "PVERB"};

StLogger::StLogger() {
  // 默认输出/dev/stdout
  m_fd_ = 1;
  m_level_ = LLOG_PVERB;
}

StLogger::~StLogger() {
  if (m_fd_ > 0 && m_fd_ != STDERR_FILENO) {
    ::close(m_fd_);
    ::free(m_name_);
  }
}

int32_t StLogger::Init(int32_t level, char *name) {
  m_level_ = ST_MAX(LLOG_EMERG, ST_MIN(level, LLOG_PVERB));
  m_name_ = NULL;
  if (name == NULL || !strlen(name)) {
    m_fd_ = STDERR_FILENO;
  } else {
    m_name_ = ::strdup(name);
    m_fd_ = ::open(name, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (m_fd_ < 0) {
      return -1;
    }
  }

  return 0;
}

void StLogger::Reopen() {
  if (m_fd_ != STDERR_FILENO) {
    ::close(m_fd_);
    m_fd_ = ::open(m_name_, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (m_fd_ < 0) {
      abort();
    }
  }
}

void StLogger::SetLevel(int32_t level) {
  m_level_ = ST_MAX(LLOG_EMERG, ST_MIN(level, LLOG_PVERB));
}

void StLogger::Stacktrace(void) {
  if (m_fd_ < 0) {
    return;
  }
}

int32_t StLogger::LogAble(int32_t level) {
  if (level > m_level_) {
    return 0;
  }

  return 1;
}

int32_t StLogger::StringIndexOf(const char *s, char c) {
  int32_t i = -1;
  if (s != NULL && *s != '\0') {
    i = 0;
    while (*s != '\0' && *s != c) {
      s++;
      i++;
    }
  }

  return i;
}

int32_t StLogger::StringLastOf(const char *s, char c) {
  int32_t i = -1;
  if (s != NULL && *s != '\0') {
    int32_t last = -1;
    i = 0;
    while (*s != '\0') {
      if (*s == c) {
        if (i >= last) {
          last = i;
        }
      }
      s++;
      i++;
    }

    i = last;
  }

  return i;
}

void StLogger::_log(const char *file, int32_t line, int32_t level,
                    const char *fmt, ...) {
  static char buf[LOG_MAX_LEN];

  int32_t len, size, errno_save;
  va_list args;
  ssize_t n;
  struct timeval tv;

  if (m_fd_ < 0) {
    return;
  }

  errno_save = errno;
  len = 0;            /* length of output buffer */
  size = LOG_MAX_LEN; /* size of output buffer */

  ::gettimeofday(&tv, NULL);
  buf[len++] = '[';
  len += ::strftime(buf + len, size - len, "%Y-%m-%d %H:%M:%S.",
                    localtime(&tv.tv_sec));
  len += ::snprintf(buf + len, size - len, "%03ld", tv.tv_usec / 1000);

  char filetemp[256];
  int32_t filetemp_len = StringLastOf(file, '/') + 1;
  filetemp_len = (filetemp_len < 0) ? 0 : filetemp_len;
  strncpy(filetemp, file + filetemp_len, strlen(file) - filetemp_len + 1);
  len += ::snprintf(buf + len, size - len, "] [%s] [%s:%d] ",
                    (level < 0 || level > 11) ? "None" : g_level_cn[level],
                    filetemp, line);

  va_start(args, fmt);
  len += ::vsnprintf(buf + len, size - len, fmt, args);
  va_end(args);

  buf[len++] = '\n';

  n = ::write(m_fd_, buf, len);
  if (n < 0) {
    m_nerror_++;
  }

  errno = errno_save;

  if (level == LLOG_EMERG) {
    abort();
  }
}

void StLogger::_loga(const char *fmt, ...) {
  static char buf[LOG_MAX_LEN];

  int32_t len, size, errno_save;
  va_list args;
  ssize_t n;

  if (m_fd_ < 0) {
    return;
  }

  errno_save = errno;
  len = 0;            /* length of output buffer */
  size = LOG_MAX_LEN; /* size of output buffer */

  va_start(args, fmt);
  len += ::vsnprintf(buf + len, size - len, fmt, args);
  va_end(args);

  buf[len++] = '\n';

  n = ::write(m_fd_, buf, len);
  if (n < 0) {
    m_nerror_++;
  }

  errno = errno_save;
}