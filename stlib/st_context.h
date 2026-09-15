/*
 * Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT
 * Project wrappers: Stack / Context / context_switch (restored from git history).
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
  int m_stk_size_;
  int m_vaddr_size_;
  uchar *m_vaddr_;
  void *m_private_;
  Context m_context_;
  unsigned int m_id_;
} Stack;

void context_init(Context *c);
int context_switch(Context *from, Context *to);
void context_exit(int _errno);

#ifdef __cplusplus
}
#endif

#endif /* _ST_CONTEXT_H_ */
