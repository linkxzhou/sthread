#ifndef _ST_TEST_H_
#define _ST_TEST_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <algorithm>
#include <string>

class StStatus 
{
public:
    StStatus() : 
        m_state_(NULL) 
    { }

    StStatus(StStatus &rhs) : 
        m_state_(rhs.m_state_) 
    { 
        rhs.m_state_ = NULL; 
    }

    ~StStatus() 
    { 
        delete[] m_state_; 
    }

    StStatus(const StStatus &rhs);

    StStatus& operator= (const StStatus &rhs);

    StStatus& operator=(StStatus &rhs);

    bool Ok() const 
    { 
        return (m_state_ == NULL); 
    }

    bool IsNotFound() const 
    { 
        return code() == kNotFound; 
    }

    bool IsCorruption() const 
    { 
        return code() == kCorruption; 
    }

    bool IsIOError() const 
    { 
        return code() == kIOError; 
    }

    bool IsNotSupportedError() const 
    { 
        return code() == kNotSupported; 
    }

    bool IsInvalidArgument() const 
    { 
        return code() == kInvalidArgument; 
    }

    std::string ToString() const;

    const char* CopyState(const char *state) 
    {
        uint32_t size;
        memcpy(&size, state, sizeof(size));
        char *result = new char[size + 5];
        memcpy(result, state, size + 5);

        return result;
    }

 private:
    const char *m_state_;

    enum Code 
    {
        kOk = 0,
        kNotFound = 1,
        kCorruption = 2,
        kNotSupported = 3,
        kInvalidArgument = 4,
        kIOError = 5
    };

    Code code() const 
    {
        return (m_state_ == NULL) ? kOk : static_cast<Code>(m_state_[4]);
    }
};

class StTester 
{
public:
    StTester(const char *f, int l) : 
        m_ok_(true), 
        m_fname_(f), 
        m_line_(l) 
    { }

    ~StTester() 
    {
        if (!m_ok_) 
        {
            fprintf(stderr, "[FAILED]%s:%d:%s\n", m_fname_, m_line_, m_ss_.str().c_str());
            exit(0);
        }
    }

    StTester& Is(bool b, const char *msg) 
    {
        if (!b) 
        {
            m_ss_ << " Assertion failure " << msg;
            m_ok_ = false;
        }

        return *this;
    }

    StTester& IsOk(const StStatus &s) 
    {
        if (!s.Ok()) 
        {
            m_ss_ << " " << s.ToString();
            m_ok_ = false;
        }

        return *this;
    }

    StTester& IsOk(bool s) 
    {
        if (!s) 
        {
            m_ok_ = false;
        }

        return *this;
    }

    #define BINARY_OP(name, op)                             \
        template <class X, class Y>                         \
        StTester& name(const X& x, const Y& y)                \
        {                                                   \
            if (!(x op y))                                  \
            {                                               \
                m_ss_ << " failed: " << x << (" " #op " ") << y;    \
                m_ok_ = false;                                      \
            }                                                       \
            return *this;                                           \
        }

    BINARY_OP(IsEq, ==)
    BINARY_OP(IsNe, !=)
    BINARY_OP(IsGe, >=)
    BINARY_OP(IsGt, >)
    BINARY_OP(IsLe, <=)
    BINARY_OP(IsLt, <)
    #undef BINARY_OP

    template <class V>
    StTester& operator<< (const V &value) 
    {
        if (!m_ok_) 
        {
            m_ss_ << " " << value;
        }

        return *this;
    }
    
    static int RunAllTests();

    static int RandomSeed();

    static bool RegisterTest(const char *base, 
        const char *name, void (*func)());

private:
    bool m_ok_;
    const char *m_fname_;
    int m_line_;
    std::stringstream m_ss_;
};

#define ASSERT_TRUE(c)  StTester(__FILE__, __LINE__).Is((c), #c)
#define ASSERT_OK(s)    StTester(__FILE__, __LINE__).IsOk((s))
#define ASSERT_EQ(a,b)  StTester(__FILE__, __LINE__).IsEq((a),(b))
#define ASSERT_NE(a,b)  StTester(__FILE__, __LINE__).IsNe((a),(b))
#define ASSERT_GE(a,b)  StTester(__FILE__, __LINE__).IsGe((a),(b))
#define ASSERT_GT(a,b)  StTester(__FILE__, __LINE__).IsGt((a),(b))
#define ASSERT_LE(a,b)  StTester(__FILE__, __LINE__).IsLe((a),(b))
#define ASSERT_LT(a,b)  StTester(__FILE__, __LINE__).IsLt((a),(b))

#define TCONCAT(a, b) TCONCAT1(a, b)
#define TCONCAT1(a, b) a##b

#define TEST(base, name)                                            \
class TCONCAT(_Test_, name) : public base {                         \
    public:                                                         \
        void _Run();                                                \
        static void _RunIt()                                        \
        {                                                           \
            TCONCAT(_Test_, name) t;                                \
            t._Run();                                               \
        }                                                           \
};                                                                  \
bool TCONCAT(_Test_ignored_, name) =                                \
  StTester::RegisterTest(#base, #name, &TCONCAT(_Test_, name)::_RunIt); \
void TCONCAT(_Test_, name)::_Run()

struct Test 
{
    const char *base;
    const char *name;
    void (*func)();
};

#define RUN_ALL_TESTS()     StTester::RunAllTests()

#endif  // _ST_TEST_H_
