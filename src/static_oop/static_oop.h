#ifndef TINYBUF_STATIC_OOP_H
#define TINYBUF_STATIC_OOP_H

#include <stdint.h>

#define TB_STRUCT_BEGIN(T) typedef struct T {
#define TB_STRUCT_END(T) } T;
#define TB_CAT2(a,b) a##b
#define TB_CAT3(a,b,c) TB_CAT2(TB_CAT2(a,b), c)
#define TB_METHOD_DEF(T, Ret, Name, ...) Ret Name(T *self, __VA_ARGS__)

#if defined(__GNUC__)
typedef int tb_spinlock_t;
static inline void tb_spinlock_init(tb_spinlock_t *lk) { *lk = 0; }
static inline void tb_spinlock_lock(tb_spinlock_t *lk) { while (__sync_lock_test_and_set(lk, 1)) {} }
static inline void tb_spinlock_unlock(tb_spinlock_t *lk) { __sync_lock_release(lk); }
#define TB_WITH_LOCK(lock) for (int _tb_once = 1; _tb_once && (tb_spinlock_lock(&(lock)), 1); _tb_once = 0, tb_spinlock_unlock(&(lock)))
#else
typedef int tb_spinlock_t;
static inline void tb_spinlock_init(tb_spinlock_t *lk) { *lk = 0; }
static inline void tb_spinlock_lock(tb_spinlock_t *lk) { (void)lk; }
static inline void tb_spinlock_unlock(tb_spinlock_t *lk) { (void)lk; }
#define TB_WITH_LOCK(lock) for (int _tb_once = 1; _tb_once; _tb_once = 0)
#endif

#define TB_TRAIT(Trait) typedef struct Trait##_trait Trait##_trait

#if defined(__cplusplus)
#define TB_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#define TB_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

#define type_trait_impl(Trait,Type) struct Trait##_##Type##_trait_impl { int _; };
#define type_trait_require(Trait,Type) TB_STATIC_ASSERT(sizeof(struct Trait##_##Type##_trait_impl) > 0, "trait not implemented")

#define TB_STATIC_DEF(T, Fn) T Fn()

#endif
