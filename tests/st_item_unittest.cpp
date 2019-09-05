#include "../include/st_item.h"

ST_NAMESPACE_USING

TEST(StStatus, StEventItem)
{
    StEventItem *item = new StEventItem();
    item->InputNotify();
    item->OutputNotify();
    item->HangupNotify();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}