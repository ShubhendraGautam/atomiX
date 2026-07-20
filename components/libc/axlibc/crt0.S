/* axlibc program entry.
 *
 * The loader enters here with the System V frame docs/abi.md describes: sp
 * points at argc, followed by argv, NULL, envp, NULL, auxv, AT_NULL.  Nothing
 * arrives in registers, so this must read everything off the stack.
 *
 * There is no dynamic linker and no init/fini array yet, so the whole of
 * startup is: pick argc/argv/envp off the stack, call main, and pass its return
 * value to exit.  A program that returns from main exits cleanly; there is no
 * path that falls off the end. */
  .section .text._start, "ax", @progbits
  .globl _start
  .type _start, @function
_start:
  mv    s0, sp                    /* the frame the loader built */
  lw    a0, 0(s0)                 /* argc                        */
  addi  a1, s0, 4                 /* argv                        */
  slli  t0, a0, 2
  add   t1, a1, t0
  addi  a2, t1, 4                 /* envp: past argv and its NULL */
  la    t2, __libc_environ
  sw    a2, 0(t2)

  /* The psABI wants a 16-byte aligned stack at a call boundary. */
  andi  sp, s0, -16

  call  main
  call  exit                      /* main's return value is the exit status */
1:
  j     1b                        /* exit does not return */
  .size _start, . - _start
