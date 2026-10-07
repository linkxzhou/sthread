/*
 * Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT
 * Project wrappers: Stack / Context / context_switch (restored from git
 * history).
 */

#ifndef _ST_CONTEXT_H_
#define _ST_CONTEXT_H_

#include "stlib/ucontext/ucontext.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  STACK = 260096 /* 256K — restored from commit 478d209 st_ucontext.h */
};

#ifndef uchar
typedef unsigned char uchar;
#endif

typedef struct Context {
  ucontext_t uc;
} Context;

typedef struct ustack {
  /* Context first: keep uc_mcontext better aligned (old offset was 24). */
  Context m_context_;
  int m_stk_size_;
  int m_vaddr_size_;
  uchar *m_vaddr_;
  void *m_private_;
  unsigned int m_id_;
} Stack;

void context_init(Context *c);
int context_switch(Context *from, Context *to);
void context_exit(int _errno);

/*
 * 分配 Stack 与 memsize 字节的栈，并 makecontext。
 * entry 的实参约定与历史 InitContext 相同：makecontext(entry, 2, ty, tx)，
 * ty/tx 是 Stack* 拆开的两个 32 位值。ss_sp 为 vaddr+16，ss_size 为
 * memsize-64，与改动前逐项一致。
 * 返回 0 成功；-1 分配失败（*out 为 NULL）；-2 getcontext 失败
 * （栈已分配，*out 非空，调用方打日志，与旧 InitContext 一样不释放）。
 * m_id_ / m_private_ / 名字由调用方在返回后填写。协程真正跑起来之前写即可。
 */
int context_make(Stack **out, void (*entry)(), unsigned int stack_size,
                 unsigned int memsize);
void context_free(Stack *stack);

#ifdef __cplusplus
}
#endif

#endif /* _ST_CONTEXT_H_ */
