#include "../include/st_base.h"

ST_NAMESPACE_USING

TEST(StStatus, StEventSuper) {
  StEventSuper *item = new StEventSuper();
  item->EvInput();
  item->EvOutput();
  item->EvHangup();
}

// 测试所有的功能
int main(int argc, char *argv[]) { return RUN_ALL_TESTS(); }