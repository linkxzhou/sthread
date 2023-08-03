#ifndef _UTHREAD_H_
#define _UTHREAD_H_

#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#include "ucontext.h"

#ifdef UTHREAD_STACK_SZIE
#define DEFAULT_STACK_SZIE (UTHREAD_STACK_SZIE)
#else
#define DEFAULT_STACK_SZIE 1024 * 128
#endif

#ifdef UTHREAD_MAX_SIZE
#define DEFAULT_MAX_SIZE (UTHREAD_MAX_SIZE)
#else
#define DEFAULT_MAX_SIZE 1024
#endif

enum ThreadState { FREE, RUNNABLE, RUNNING, SUSPEND };

typedef void (*uthread_fun)(void *arg);

typedef struct uthread_t {
  ucontext_t ctx;
  uthread_fun func;
  void *arg;
  enum ThreadState state;
  char stack[DEFAULT_STACK_SZIE];
} uthread_t;

typedef struct schedule_t {
  ucontext_t main;
  int running_thread;
  uthread_t *threads;
  int max_index;
} schedule_t;

/* help the thread running in the schedule */
static void uthread_body(schedule_t *ps);

schedule_t* uthread_create_schedule();

void uthread_destory_schedule(schedule_t *schedule);

/***
 * create a user's thread
 *    @param[in]:
 *        schedule_t &schedule
 *        uthread_fun func: user's function
 *        void *arg: the arg of user's function
 *    @param[out]:
 *    @return:
 *        return the index of the created thread in schedule
 ***/
int uthread_create(schedule_t *schedule, uthread_fun func, void *arg);

/* hang the currently running thread, switch to main thread */
void uthread_yield(schedule_t *schedule);

/* resume the thread which index equal id*/
void uthread_resume(schedule_t *schedule, int id);

/***
 * test whether all the threads in schedule run over
 *    @param[in]:
 *        const schedule_t & schedule
 *    @param[out]:
 *    @return:
 *        return 1 if all threads run over,otherwise return 0
 ***/
int schedule_finished(const schedule_t *schedule);

#endif