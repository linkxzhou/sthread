#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/st_server.h"

ST_NAMESPACE_USING

TEST(ServerTest, accept)
{
	Manager<>* manager = GetInstance< Manager<> >();
    manager->SetHookFlag();
    
    StServer< Manager<> > *server = new StServer< Manager<> >(manager);
    int fd = server->CreateSocket("127.0.0.1", 8001);
    if (!server->Listen())
    {
        LOG_ERROR("listen error!!!");
        return ;
    }
    
    server->Loop();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}