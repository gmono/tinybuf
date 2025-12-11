#ifndef TINYBUF_STATIC_OOP_H
#define TINYBUF_STATIC_OOP_H

#include <stdint.h>

#define TB_STRUCT_BEGIN(T) typedef struct T {
#define TB_STRUCT_END(T) } T;
#define TB_CAT2(a,b) a##b
#define TB_CAT3(a,b,c) TB_CAT2(TB_CAT2(a,b), c)
#define TB_METHOD_DEF(T, Ret, Name, ...) Ret Name(T *self, __VA_ARGS__)

 

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
