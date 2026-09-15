#include "src/st_poll.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

TEST(StStatus, StEventItem) {
  StEventItem *item = new StEventItem();
  item->EvInput();
  item->EvOutput();
  item->EvHangup();
}

// 测试所有的功能
int main(int argc, char *argv[]) { return RUN_ALL_TESTS(); }