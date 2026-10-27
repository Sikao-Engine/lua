/*
** $Id: lmem.h $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.h"
#include "lua.h"


#define luaM_error(L)	luaD_throw(L, LUA_ERRMEM)


/*
** This macro tests whether it is safe to multiply 'n' by the size of
** type 't' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
#define luaM_testsize(n,e)  \
	(sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))

#define luaM_checksize(L,n,e)  \
	(luaM_testsize(n,e) ? luaM_toobig(L) : cast_void(0))


/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' and that 'int' is not larger than 'size_t'.)
*/
#define luaM_limitN(n,t)  \
  ((cast_sizet(n) <= MAX_SIZET/sizeof(t)) ? (n) :  \
     cast_int((MAX_SIZET/sizeof(t))))


/*
** Arrays of chars do not need any test
*/
#define luaM_reallocvchar(L,b,on,n)  \
  cast_charp(luaM_saferealloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

#define luaM_freemem(L, b, s)	luaM_free_(L, (b), (s))
#define luaM_free(L, b)		luaM_free_(L, (b), sizeof(*(b)))
#define luaM_freearray(L, b, n)   luaM_free_(L, (b), (n)*sizeof(*(b)))

#define luaM_new(L,t)		cast(t*, luaM_malloc_(L, sizeof(t), 0))
#define luaM_newvector(L,n,t)  \
	cast(t*, luaM_malloc_(L, cast_sizet(n)*sizeof(t), 0))
#define luaM_newvectorchecked(L,n,t) \
  (luaM_checksize(L,n,sizeof(t)), luaM_newvector(L,n,t))

#define luaM_newobject(L,tag,s)	luaM_malloc_(L, (s), tag)

#define luaM_newblock(L, size)	luaM_newvector(L, size, char)

#define luaM_growvector(L,v,nelems,size,t,limit,e) \
	((v)=cast(t *, luaM_growaux_(L,v,nelems,&(size),sizeof(t), \
                         luaM_limitN(limit,t),e)))

#define luaM_reallocvector(L, v,oldn,n,t) \
   (cast(t *, luaM_realloc_(L, v, cast_sizet(oldn) * sizeof(t), \
                                  cast_sizet(n) * sizeof(t))))

#define luaM_shrinkvector(L,v,size,fs,t) \
   ((v)=cast(t *, luaM_shrinkvector_(L, v, &(size), fs, sizeof(t))))

#if defined(KIMIX_LUA_ARENA)		/* { */
/*
** kimix extension: size-class hints.
** The arena derives a block's size class from the requested size alone
** (the same function maps 'osize' back on free), so these hints cannot
** change where a block lands; they document the intent at the call site
** and let a KIMIX_LUA_MEMCHECK build assert the invariant "the class
** derived on alloc equals the class derived on free".
*/
#define LUA_MEMCLS_GENERIC	0	/* parser, stack, buffers */
#define LUA_MEMCLS_GCOBJ	1	/* a fixed-size GCObject */
#define LUA_MEMCLS_STR		2	/* long-string payload */
#define LUA_MEMCLS_ARR		3	/* table array block (9*N+4) */
#define LUA_MEMCLS_NODE		4	/* table node block (32*2^k [+16]) */

LUAI_FUNC void *luaM_mallocx_ (lua_State *L, size_t size, int tag, int cls);
LUAI_FUNC void *luaM_reallocx_ (lua_State *L, void *block, size_t osize,
                                size_t nsize, int cls);
LUAI_FUNC void luaM_freex_ (lua_State *L, void *block, size_t osize, int cls);

#define luaM_newvectorx(L,n,t,cls)  \
	cast(t*, luaM_mallocx_(L, cast_sizet(n)*sizeof(t), 0, (cls)))
#define luaM_newblockx(L,size,cls)  \
	cast(char*, luaM_mallocx_(L, (size), 0, (cls)))
#define luaM_freememx(L,b,s,cls)  luaM_freex_(L, (b), (s), (cls))
#define luaM_newobjectx(L,tag,s,cls)  luaM_mallocx_(L, (s), tag, (cls))
#endif					/* } */

LUAI_FUNC l_noret luaM_toobig (lua_State *L);

/* not to be called directly */
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize,
                                                          size_t size);
LUAI_FUNC void *luaM_saferealloc_ (lua_State *L, void *block, size_t oldsize,
                                                              size_t size);
LUAI_FUNC void luaM_free_ (lua_State *L, void *block, size_t osize);
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int nelems,
                               int *size, unsigned size_elem, int limit,
                               const char *what);
LUAI_FUNC void *luaM_shrinkvector_ (lua_State *L, void *block, int *nelem,
                                    int final_n, unsigned size_elem);
LUAI_FUNC void *luaM_malloc_ (lua_State *L, size_t size, int tag);

#endif

