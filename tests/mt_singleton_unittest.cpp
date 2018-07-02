#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_singleton.h"

MTHREAD_NAMESPACE_USING

class T1
{};

static void * thread_func(void *obj)
{  
    fprintf(stdout, "\n fun thread id=%lu \n", pthread_self());  
    T1* t1 = GetInstance<T1>();
    fprintf(stdout, "\n obj = %p \n", t1);
    T1* t2 = GetInstance<T1>();
    fprintf(stdout, "\n obj = %p \n", t2);

    Singleton<T1>::InstanceDestroy();
    return NULL;
} 

TEST(SingletonTest, s1)
{
    pthread_t tid;
    int err = pthread_create(&tid, NULL, thread_func, NULL);  
    if (err != 0)
    {  
        perror("fail to create thread");  
        return ;
    }

    err = pthread_create(&tid, NULL, thread_func, NULL);  
    if (err != 0)
    {  
        perror("fail to create thread");    
        return ;
    }
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}