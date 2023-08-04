#include "uthread.h"
#include <stdio.h>

void fun1(void *arg) {
  puts("this is fun1");
}

void fun2(void *arg) {
  puts("this is fun2 start");
  uthread_yield((schedule_t *)arg);
  puts("this is fun2 end");
}

void fun3(void *arg) {
  puts("this is fun3 start");
  uthread_yield((schedule_t *)arg);
  puts("this is fun3 end");
}

void schedule_test() {
  schedule_t *s = uthread_create_schedule();
  int id2 = uthread_create(s, fun2, s);
  int id3 = uthread_create(s, fun3, s);
  while (!schedule_finished(s)) {
    uthread_resume(s, id3);
    uthread_resume(s, id2);
  }
  uthread_destory_schedule(s);
}

int main() {
  puts("----------------");
  schedule_test();
  return 0;
}