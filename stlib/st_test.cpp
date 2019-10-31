#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include "st_test.h"

stlib_namespace_using

std::vector<Test> *tests;

StStatus::StStatus(const StStatus &rhs) 
{
    m_state_ = (rhs.m_state_ == NULL) ? NULL : CopyState(rhs.m_state_);
}

StStatus& StStatus::operator=(const StStatus &rhs) 
{
    if (m_state_ != rhs.m_state_)
    {
        delete[] m_state_;
        m_state_ = (rhs.m_state_ == NULL) ? NULL : CopyState(rhs.m_state_);
    }

    return *this;
}

StStatus& StStatus::operator=(StStatus &rhs) 
{
    std::swap(m_state_, rhs.m_state_);
    return *this;
}

bool StTester::RegisterTest(const char *base, const char *name, void (*func)()) 
{
    if (tests == NULL) 
    {
        tests = new std::vector<Test>();
    }

    Test t;
    t.base = base;
    t.name = name;
    t.func = func;
    tests->push_back(t);

    return true;
}

int StTester::RunAllTests() 
{
    const char *matcher = ::getenv("LEVELDB_TESTS");

    int num = 0;
    if (tests != NULL) 
    {
        for (size_t i = 0; i < tests->size(); i++) 
        {
            const Test &t = (*tests)[i];
            if (matcher != NULL) 
            {
                std::string name = t.base;
                name.push_back('.');
                name.append(t.name);
                if (strstr(name.c_str(), matcher) == NULL) 
                {
                    continue;
                }
            }

            fprintf(stderr, "==== Test %s.%s\n", t.base, t.name);

            (*t.func)();
            ++num;
        }
    }

    fprintf(stderr, "==== PASSED %d tests\n", num);

    return 0;
}

int StTester::RandomSeed() 
{
    const char *env = ::getenv("TEST_RANDOM_SEED");
    int result = (env != NULL ? atoi(env) : 301);
    if (result <= 0)
    {
        result = 301;
    }

    return result;
}
