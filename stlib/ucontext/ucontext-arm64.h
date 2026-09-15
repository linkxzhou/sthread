#define setcontext(u) setmcontext(&(u)->uc_mcontext)
#define getcontext(u) getmcontext(&(u)->uc_mcontext)

typedef struct mcontext mcontext_t;
typedef struct ucontext ucontext_t;

/* Avoid Apple libsystem makecontext/swapcontext (wrong layout → SIGSEGV).
 * Types are already libthread_*; functions must be renamed the same way.
 * getcontext/setcontext are macros → getmcontext/setmcontext (our asm). */
#define makecontext libthread_makecontext
#define swapcontext libthread_swapcontext

#ifdef __cplusplus
extern "C" {
#endif
extern int swapcontext(ucontext_t *, const ucontext_t *);
extern void makecontext(ucontext_t *, void (*)(), int, ...);
extern int getmcontext(mcontext_t *);
extern void setmcontext(const mcontext_t *);
#ifdef __cplusplus
}
#endif

/*
 * Compact coroutine mcontext for Apple Silicon / aarch64.
 * Layout must match getmcontext/setmcontext in asm.S (NEEDARM64CONTEXT).
 *
 * Offsets (bytes):
 *   mc_x[0]  .. mc_x[30] : 0 .. 240
 *   mc_sp                : 248
 *   mc_pc                : 256
 *   mc_d[0]  .. mc_d[7]  : 264 .. 320  (callee-saved d8-d15)
 */
struct mcontext {
  long mc_x[31]; /* x0-x30 (x30 == lr) */
  long mc_sp;
  long mc_pc;
  long mc_d[8]; /* d8-d15 */
};

struct ucontext {
  sigset_t uc_sigmask;
  mcontext_t uc_mcontext;
  struct __ucontext *uc_link;
  stack_t uc_stack;
  int __spare__[8];
};
