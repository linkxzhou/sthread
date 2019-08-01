#ifndef _ST_LOG_H_INCLUDED_
#define _ST_LOG_H_INCLUDED_

#include <sys/time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <assert.h>

#define LLOG_EMERG   0      /* system in unusable */
#define LLOG_ALERT   1      /* action must be taken immediately */
#define LLOG_CRIT    2      /* critical conditions */
#define LLOG_ERR     3      /* error conditions */
#define LLOG_WARN    4      /* warning conditions */
#define LLOG_NOTICE  5      /* normal but significant condition (default) */
#define LLOG_INFO    6      /* informational */
#define LLOG_DEBUG   7      /* debug messages */
#define LLOG_VERB    8      /* verbose messages */
#define LLOG_VVERB   9      /* verbose messages on crack */
#define LLOG_VVVERB  10     /* verbose messages on ganga */
#define LLOG_PVERB   11     /* periodic verbose messages on crack */

#define LOG_MAX_LEN  2046   /* max length of log message */

class StLogger
{
public:
    StLogger();
    
    ~StLogger();

    int Init(int32_t level, char *filename);

    void Reopen();

    void SetLevel(int32_t level);

    void Stacktrace();

    int LogAble(int32_t level);

    void _log(const char *file, int32_t line, 
        int32_t panic, const char *fmt, ...);

    void _loga(const char *fmt, ...);

    inline static StLogger& GetInstance()
    {
        static StLogger logger;
        return logger;
    }

    // 日志特殊的indexOf和lastOf
    static int StringIndexOf(const char *s, char c);

    static int StringLastOf(const char *s, char c);

private:
    char *m_name_;
    int m_level_;
    int m_fd_;
    int m_nerror_;
};

#define LOG_ERROR(...) do                                                       \
    {                                                                           \
        if (StLogger::GetInstance().LogAble(LLOG_ALERT) != 0) {                 \
            StLogger::GetInstance()._log(__FILE__, __LINE__, 0, __VA_ARGS__);   \
        }                                                                       \
    } while (0)

#define LOG_WARN(...) do                                                        \
    {                                                                           \
        if (StLogger::GetInstance().LogAble(LLOG_WARN) != 0) {                  \
            StLogger::GetInstance()._log(__FILE__, __LINE__, 0, __VA_ARGS__);   \
        }                                                                       \
    } while (0)

#define LOG_PANIC(...) do                                                       \
    {                                                                           \
        if (StLogger::GetInstance().LogAble(LLOG_EMERG) != 0) {                 \
            StLogger::GetInstance()._log(__FILE__, __LINE__, 1, __VA_ARGS__);   \
        }                                                                       \
    } while (0)

#define LOG_DEBUG(...) do                                                       \
    {                                                                           \
        if (StLogger::GetInstance().LogAble(LLOG_VVVERB) != 0) {                \
            StLogger::GetInstance()._log(__FILE__, __LINE__, 0, __VA_ARGS__);   \
        }                                                                       \
    } while (0)

#define LOG_TRACE(...) do                                                     \
    {                                                                           \
        if (StLogger::GetInstance().LogAble(LLOG_PVERB) != 0) {                 \
            StLogger::GetInstance()._log(__FILE__, __LINE__, 0, __VA_ARGS__);   \
        }                                                                       \
    } while (0)

#define LOGA(...) do                                                        \
    {                                                                       \
        if (StLogger::getInstance().LogAble(LLOG_PVERB) != 0) {             \
            StLogger::getInstance()._loga(__VA_ARGS__);                     \
        }                                                                   \
    } while (0)

#define FUNCTION_INTO(cls)  LOG_DEBUG("\033[32mrun into %s:%s \033[0m", #cls, __FUNCTION__)

#define FUNCTION_OUT(cls)   LOG_DEBUG("\033[34mrun out %s:%s \033[0m", #cls, __FUNCTION__)

#define ASSERT(exp)         assert((exp))

#endif