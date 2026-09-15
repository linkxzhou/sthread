#include "stlib/st_context.h"
#include "tests/st_test_compat.h"
#include <string.h>

ST_NAMESPACE_USING

static Context g_main_ctx, g_child_ctx;
static volatile int g_child_ran = 0;

static void child(uint ty, uint tx) {
  (void)ty;
  (void)tx;
  g_child_ran = 1;
  setcontext(&g_main_ctx.uc);
}

TEST(StStatus, ContextSwitch) {
  char stack[64 * 1024];
  memset(&g_main_ctx, 0, sizeof(g_main_ctx));
  memset(&g_child_ctx, 0, sizeof(g_child_ctx));
  g_child_ran = 0;

  ASSERT_TRUE(getcontext(&g_child_ctx.uc) == 0);
  g_child_ctx.uc.uc_stack.ss_sp = stack;
  g_child_ctx.uc.uc_stack.ss_size = sizeof(stack);
  g_child_ctx.uc.uc_link = 0;
  makecontext(&g_child_ctx.uc, (void (*)())child, 2, 0, 0);

  context_init(&g_main_ctx);
  int r = context_switch(&g_main_ctx, &g_child_ctx);
  ASSERT_TRUE(r == 0);
  ASSERT_TRUE(g_child_ran == 1);
  context_exit(0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
