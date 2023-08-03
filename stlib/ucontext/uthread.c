#ifndef _UTHREAD_C_
#define _UTHREAD_C_

#include "uthread.h"

schedule_t* uthread_create_schedule() {
  schedule_t* schedule = (schedule_t *)malloc(sizeof(schedule_t));
  schedule->running_thread = -1;
  schedule->max_index = 0;
  schedule->threads = (uthread_t *)malloc(DEFAULT_MAX_SIZE * sizeof(uthread_t));
  for (int i = 0; i < DEFAULT_MAX_SIZE; i++) {
    (schedule->threads)[i].state = FREE;
  }

  return schedule;
}

void uthread_destory_schedule(schedule_t* schedule) {
  if (NULL != schedule && NULL != schedule->threads) {
    free(schedule->threads);
  }

  if (NULL != schedule) {
    free(schedule);
  }
}

void uthread_resume(schedule_t* schedule, int id) {
  if (id < 0 || id >= schedule->max_index) {
    return;
  }

  uthread_t* t = &(schedule->threads[id]);

  if (t->state == SUSPEND) {
    swapcontext(&(schedule->main), &(t->ctx));
  }
}

void uthread_yield(schedule_t* schedule) {
  if (schedule->running_thread != -1) {
    uthread_t* t = &(schedule->threads[schedule->running_thread]);
    t->state = SUSPEND;
    schedule->running_thread = -1;
    swapcontext(&(t->ctx), &(schedule->main));
  }
}

void uthread_body(schedule_t *ps) {
  int id = ps->running_thread;

  if (id != -1) {
    uthread_t* t = &(ps->threads[id]);
    t->func(t->arg);
    t->state = FREE;
    ps->running_thread = -1;
  }
}

int uthread_create(schedule_t *schedule, uthread_fun func, void *arg) {
  int id = 0;

  for (id = 0; id < schedule->max_index; ++id) {
    if (schedule->threads[id].state == FREE) {
      break;
    }
  }

  if (id == schedule->max_index) {
    schedule->max_index++;
  }

  uthread_t* t = &(schedule->threads[id]);

  t->state = RUNNABLE;
  t->func = func;
  t->arg = arg;

  getcontext(&(t->ctx));

  t->ctx.uc_stack.ss_sp = t->stack;
  t->ctx.uc_stack.ss_size = DEFAULT_STACK_SZIE;
  t->ctx.uc_stack.ss_flags = 0;
  t->ctx.uc_link = &(schedule->main);
  schedule->running_thread = id;

  makecontext(&(t->ctx), (void (*)(void))(uthread_body), 1, schedule);
  swapcontext(&(schedule->main), &(t->ctx));

  return id;
}

int schedule_finished(const schedule_t *schedule) {
  if (schedule->running_thread != -1) {
    return 0;
  }

  for (int i = 0; i < schedule->max_index; ++i) {
    if (schedule->threads[i].state != FREE) {
      return 0;
    }
  }

  return 1;
}

#endif