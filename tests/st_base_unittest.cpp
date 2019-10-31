#include "../include/st_base.h"

ST_NAMESPACE_USING

TEST(StStatus, StEventSuper)
{
    StEventSuper *item = new StEventSuper();
    item->InputNotify();
    item->OutputNotify();
    item->HangupNotify();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}