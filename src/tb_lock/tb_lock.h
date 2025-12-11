#ifndef TINYBUF_TB_LOCK_H
#define TINYBUF_TB_LOCK_H

#if defined(_WIN32)
#include <windows.h>
typedef long tb_spinlock_t;
static inline void tb_spinlock_init(tb_spinlock_t *lk) { *lk = 0; }
static inline void tb_spinlock_lock(tb_spinlock_t *lk) { while (InterlockedExchange(lk, 1) != 0) {} }
static inline void tb_spinlock_unlock(tb_spinlock_t *lk) { InterlockedExchange(lk, 0); }
#else
typedef int tb_spinlock_t;
static inline void tb_spinlock_init(tb_spinlock_t *lk) { *lk = 0; }
static inline void tb_spinlock_lock(tb_spinlock_t *lk) { while (__sync_lock_test_and_set(lk, 1)) {} }
static inline void tb_spinlock_unlock(tb_spinlock_t *lk) { __sync_lock_release(lk); }
#endif

#define TB_WITH_LOCK(lock) for (int _tb_once = 1; _tb_once && (tb_spinlock_lock(&(lock)), 1); _tb_once = 0, tb_spinlock_unlock(&(lock)))

#endif
