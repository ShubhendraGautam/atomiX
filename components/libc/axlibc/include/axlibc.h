/* axlibc: a small freestanding C library for aXos programs.
 *
 * Deliberately not a full libc.  It provides what a program needs to be an
 * ordinary C program -- startup, syscalls with errno, strings, an allocator,
 * and formatted output -- and nothing else.  It exists so a program can be
 * written normally rather than in syscall assembly, and so the ABI has a
 * consumer that is not the ABI's own test.
 *
 * It is a component: a profile can select a real libc (picolibc, newlib)
 * instead, and nothing above it changes, because everything here is the
 * standard spelling.
 *
 * Not provided, and the reasons are the same as in docs/abi.md: no floating
 * point, no locale, no signals, no threads, no dynamic allocation of file
 * descriptors beyond the console. */
#pragma once

typedef unsigned int size_t;
typedef int ssize_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

#define NULL ((void *)0)

/* The errno subset docs/abi.md defines. */
#define EPERM  1
#define ENOENT 2
#define EBADF  9
#define ENOMEM 12
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38

extern int errno;
extern char **__libc_environ;

long __libc_syscall(long nr, long a0, long a1, long a2);
long __libc_ret(long value);

/* unistd */
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int getpid(void);
void *sbrk(intptr_t increment);

/* stdlib */
void exit(int status);
void _exit(int status);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);

/* string */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strchr(const char *s, int c);

/* stdio: a console-only subset.  There are no FILE streams because there is
 * nothing to point them at yet -- adding them before the filesystem is bound
 * would be inventing an interface with no implementation behind it. */
int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
