package(default_visibility = ["//visibility:public"])

cc_library(
  name = "ucontext",
  srcs = glob(
    ["*.cpp", "*.cc", "*.c", "*.S"], 
    exclude = ["*_test.cpp", "*_test.cc", "*_test.c"]
  ),
  hdrs = glob(["*.h"]),
  copts = []
)

cc_test(
  name = "ucontext_test",
  srcs = ["ucontext_test.c"],
  deps = [
    ":ucontext",
  ],
  linkstatic = 1, # default value
)