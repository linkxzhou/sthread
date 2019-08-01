#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/st_item.h"

ST_NAMESPACE_USING

TEST(Item, StEventItem)
{
    StEventItem *item = new StEventItem();
    item->InputNotify();
    item->OutputNotify();
    item->HangupNotify();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}