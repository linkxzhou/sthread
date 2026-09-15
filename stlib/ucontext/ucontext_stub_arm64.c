/*
 * Previously an Apple Silicon stub. Real getmcontext/setmcontext/makecontext
 * now live in asm.S (NEEDARM64CONTEXT) and ucontext.c (NEEDARM64MAKECONTEXT).
 * This file is kept empty so old references fail closed at link if reintroduced.
 */
#if 0
#error "ucontext_stub_arm64.c is obsolete; use asm.S"
#endif
