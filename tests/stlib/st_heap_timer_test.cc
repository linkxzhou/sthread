#include "stlib/st_heap_timer.h"
#include "stlib/st_test.h"
#include "stlib/st_util.h"

using namespace stlib;

TEST(StStatus, notify) {
  StHeapTimer c;

  c.Startup(new StTimer(), 100);
  c.Startup(new StTimer(), 160);
  c.Startup(new StTimer(), 1500);
  c.Startup(new StTimer(), 99);
  c.Startup(new StTimer(), 20);
  c.Startup(new StTimer(), 5000);
  c.Startup(new StTimer(), 999);
  c.Startup(new StTimer(), 900);
  c.Startup(new StTimer(), 10000);

  LOG_TRACE("==========================");
  int count = 10;
  while (count-- > 0) {
    c.CheckExpired();

    int64_t now_ms = Util::TimeMs();
    LOG_TRACE("now_ms = %llu", now_ms);

    Util::Sleep(1);
  }
}

// 测试所有的功能
int main(int argc, char *argv[]) { return RUN_ALL_TESTS(); }