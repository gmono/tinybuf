#ifndef TINYBUF_OOP_H
#define TINYBUF_OOP_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

// ---- struct & method macros ----
#define TB_STRUCT_BEGIN(T) \
    typedef struct T T;    \
    struct T               \
    {
#define TB_STRUCT_END(T) \
    }                    \
    ;

#define TB_METHOD_DECL(T, RET, NAME, ...) RET T##_##NAME(T *self, ##__VA_ARGS__)
#define TB_METHOD_DEF(T, RET, NAME, ...) RET T##_##NAME(T *self, ##__VA_ARGS__)

#define TB_STATIC_DECL(T, RET, NAME, ...) static inline RET T##_##NAME(__VA_ARGS__)
#define TB_STATIC_DEF(T, RET, NAME, ...) static inline RET T##_##NAME(__VA_ARGS__)

// ---- trait macros ----
#define TB_TRAIT_BEGIN(TR)                  \
    typedef struct TR##_vtable TR##_vtable; \
    struct TR##_vtable                      \
    {
#define TB_TRAIT_METHOD(RET, NAME, ...) RET (*NAME)(void *self, ##__VA_ARGS__);
#define TB_TRAIT_END(TR) \
    }                    \
    ;

#define TB_TRAIT_IMPL(TYPE, TR, VTABLE_VAR) /* marker for preprocessor detection */ \
    enum                                                                            \
    {                                                                               \
        TR##_IMPL__##TYPE = 1                                                       \
    };                                                                              \
    static const TR##_vtable *TYPE##_trait_##TR = &(VTABLE_VAR)

// Use in preprocessor: #if TB_HAS_TRAIT(Type, Trait)
#define TB_HAS_TRAIT(TYPE, TR) defined(TR##_IMPL__##TYPE)

#define TB_STATIC_ASSERT(expr, msg) typedef char static_assert_##msg[(expr) ? 1 : -1]
#define TB_UNUSED(x) (void)(x)

// ---- atomic-based spinlock (cross-platform) ----
#if defined(_WIN32)
#include <windows.h>
    typedef struct
    {
        volatile LONG state;
    } tb_spinlock_t;
    static inline void tb_spinlock_init(tb_spinlock_t *lk) { lk->state = 0; }
    static inline int tb_spinlock_trylock(tb_spinlock_t *lk) { return InterlockedCompareExchange(&lk->state, 1, 0) == 0; }
    static inline void tb_spinlock_lock(tb_spinlock_t *lk)
    {
        while (InterlockedCompareExchange(&lk->state, 1, 0) != 0)
        {
            YieldProcessor();
        }
    }
    static inline void tb_spinlock_unlock(tb_spinlock_t *lk) { InterlockedExchange(&lk->state, 0); }
#else
#include <stdatomic.h>
typedef struct
{
    atomic_flag flag;
} tb_spinlock_t;
static inline void tb_spinlock_init(tb_spinlock_t *lk) { atomic_flag_clear(&lk->flag); }
static inline int tb_spinlock_trylock(tb_spinlock_t *lk) { return !atomic_flag_test_and_set(&lk->flag); }
static inline void tb_spinlock_lock(tb_spinlock_t *lk)
{
    while (atomic_flag_test_and_set(&lk->flag))
    {
        ;
    }
}
static inline void tb_spinlock_unlock(tb_spinlock_t *lk) { atomic_flag_clear(&lk->flag); }
#endif

// Scoped lock helper: use as for-loop to create a critical section
#define TB_WITH_LOCK(lk) for (int _tb_once = (tb_spinlock_lock(&(lk)), 1); _tb_once; _tb_once = (tb_spinlock_unlock(&(lk)), 0))

#ifdef __cplusplus
}
#endif

#endif // TINYBUF_OOP_H
