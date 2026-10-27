/*
** $Id: larena.h $
** kimix extension: per-lua_State memory arena (KIMIX_LUA_ARENA)
** See Copyright Notice in lua.h
*/

/*
** {==================================================================
** Design (see .kimix_cache/lua_memory_opt_plan.md, sections 4.1-4.6)
** ===================================================================
** The arena is a 'lua_Alloc'. Everything below is a pure function of
** the requested size: 'lua_arena_classof' maps a request to a size class
** and the same function maps 'osize' back on free, so no per-block
** header is needed (a header would cost 8-16 bytes on every 9*N+4 array
** block). A slot is never smaller than the request, so even a
** mis-classified block is a valid, correctly sized slot.
**
** Tiers (all sizes in bytes):
**   tiny       <= 512       : 16-byte granule, 32 classes
**   small      <= small_max : x1.25 steps snapped to 16
**   medium     <= small_max : the same ladder (it is continuous;
**                             tiny/small/medium only label it)
**   exact-ARR  <= small_max : 9*N+4 with N a power of two, no rounding
**   exact-NODE <= small_max : 32*2^k (+16 for the 'lastfree' prefix)
**   huge       >  small_max : one exact run from mimalloc + a per-size
**                             LRU of freed runs
**
** A class owns whole segments of 'segment_bytes' (64 KiB by default),
** carved into 'nslots_per_seg' slots at first use. Each segment keeps its
** own intrusive LIFO free list (the first pointer-sized bytes of a free
** slot hold 'next'), so an empty segment is exactly a segment whose free
** count equals its slot count -- which is what 'trim' hands back to
** mimalloc. No locks: one arena per lua_State.
*/

#ifndef larena_h
#define larena_h

#include <stddef.h>
#include <stdio.h>

#include "lua.h"
#include "llimits.h"

#if defined(KIMIX_LUA_ARENA)		/* { */

/*
** Double-free / invalid-free checking: on whenever the build is not an
** NDEBUG one, or explicitly through KIMIX_LUA_MEMCHECK.
*/
#if !defined(KIMIX_LUA_MEMCHECK) && !defined(NDEBUG)
#define KIMIX_LUA_MEMCHECK	1
#endif

#if defined(KIMIX_LUA_MEMCHECK)
#include <assert.h>
#define lua_arena_check(c)	assert(c)
#else
#define lua_arena_check(c)	((void)0)
#endif

/* tiers, for diagnostics and for the compaction planner */
#define ARENA_TIER_TINY		0
#define ARENA_TIER_SMALL	1
#define ARENA_TIER_MEDIUM	2
#define ARENA_TIER_ARR		3
#define ARENA_TIER_NODE		4

/* 'lua_arena_classof' result for "not slotted" (the huge tier) */
#define ARENA_CLASS_NONE	(-1)

#define ARENA_MAX_CLASSES	192
#define ARENA_MAX_HUGE		32	/* hard cap on cached huge runs */
#define ARENA_TINY_MIN		16u
#define ARENA_TINY_LIMIT	512u	/* tiny classes: 16, 32, ... 512 */
#define ARENA_SMALL_TIER	8192u	/* 'small' tier label boundary */
#define ARENA_DEF_SEGMENT	(64u * 1024u)
#define ARENA_DEF_SMALL_MAX	65536	/* 'small_max' default */
#define ARENA_DEF_HUGE_CACHE	4

/* segment flags */
#define ARENA_SEG_NOSLOT	1u	/* the compaction planner fenced it off */

typedef struct ArenaSeg {
  void *base;		/* start of the payload */
  void *freelist;	/* intrusive LIFO of this segment's free slots */
  unsigned char *bits;	/* occupancy bitmap (KIMIX_LUA_MEMCHECK only) */
  unsigned nslots;
  unsigned nfree;
  unsigned flags;
} ArenaSeg;

typedef struct ArenaClass {
  unsigned slot_size;		/* bytes handed out per slot */
  unsigned nslots_per_seg;	/* slots carved out of one segment */
  unsigned seg_payload;		/* nslots_per_seg * slot_size */
  int tier;			/* ARENA_TIER_* */
  unsigned seg_count;
  unsigned slots_total;		/* seg_count * nslots_per_seg */
  unsigned slots_used;
  unsigned slots_free;
  unsigned high_water;		/* peak 'slots_used', decays on trim */
  ArenaSeg **segs;		/* sorted by 'base', ascending */
  unsigned seg_cap;
  ArenaSeg *cur;		/* allocation hint */
} ArenaClass;

/* one cached huge run: exact size only, so it is reusable verbatim */
typedef struct ArenaHuge {
  void *p;
  size_t size;
} ArenaHuge;

typedef struct lua_Arena {
  lua_ArenaConfig cfg;
  ArenaClass classes[ARENA_MAX_CLASSES];
  int nclasses;
  /* generic ladder: ascending slot sizes and their class ids */
  unsigned generic_slot[ARENA_MAX_CLASSES];
  int generic_class[ARENA_MAX_CLASSES];
  int ngeneric;
  int arr_by_log[32];		/* exact array classes, by log2(N) */
  int node_by_log[32];		/* exact node classes, by log2(slots) */
  int nodex_by_log[32];		/* the '+16' variant, by log2(slots) */
  /* accounting */
  size_t logical_live;		/* requested bytes, live */
  unsigned long calls;		/* lua_Alloc calls served */
  unsigned long huge_live_count;
  size_t huge_live_bytes;
  unsigned long huge_cached;
  size_t huge_cached_bytes;
  /* trim / purge */
  unsigned long trims;
  size_t trim_bytes;
  unsigned long purges;
  /* plan / dry-run counters */
  unsigned long would_segments;
  unsigned long would_alloc;
  unsigned long would_free;
  unsigned long would_reuse;
  unsigned long would_huge_reuse;
  size_t would_shrink_bytes;
  size_t would_compact_bytes;
  size_t compacted_bytes;
  unsigned long compactions;
    unsigned long compaction_skips;
    int in_compaction;
#if defined(KIMIX_LUA_MEMCHECK)
    unsigned interior_holders;  /* luaH_* calls holding a pointer into a vector */
#endif
  /* the huge tier */
  ArenaHuge huge[ARENA_MAX_HUGE];
  unsigned nhuge;
  size_t huge_shadow[ARENA_MAX_HUGE];	/* dry-run mirror of 'huge' */
  unsigned nhuge_shadow;
  /* allocation trace (KIMIX_LUA_MEMTRACE=<path>) */
  FILE *trace;
  size_t trace_written;
  size_t trace_limit;
  int trace_done;
} lua_Arena;


/* create/destroy an arena; 'cfg' may be NULL for the defaults */
LUAI_FUNC lua_Arena *lua_arena_new (const lua_ArenaConfig *cfg);
LUAI_FUNC void lua_arena_free (lua_Arena *a);

/* the 'lua_Alloc' the arena installs */
LUAI_FUNC void *lua_arena_alloc (void *ud, void *ptr, size_t osize,
                                 size_t nsize);

/* pure size -> class mapping; ARENA_CLASS_NONE for the huge tier */
LUAI_FUNC int lua_arena_classof (lua_Arena *a, size_t n);
/* the slot size a class hands out (0 for an invalid class) */
LUAI_FUNC unsigned lua_arena_slotsize (lua_Arena *a, int cid);

/* return whole empty segments (level 0) and, at level 1, also drop the
** huge cache and purge free mimalloc pages; returns bytes released */
LUAI_FUNC size_t lua_arena_trim (lua_Arena *a, int level);

  LUAI_FUNC void lua_arena_stats (lua_Arena *a, lua_ArenaStats *st);
  /*
  ** Debug net for the compaction pass. The only functions that form an
  ** interior pointer (a 'Value *' / 'Node *' into an array or node vector)
  ** and can be entered from outside a Lua/C call bump 'interior_holders'
  ** around their body, so a move that would invalidate such a pointer is an
  ** assert instead of silent corruption. 'luaH_get*' and 'luaH_pset*' carry
  ** no 'lua_State *' in upstream's API, so they cannot reach the arena; a
  ** compaction only runs at a quiescent point anyway (nCcalls == 0).
  */
  LUAI_FUNC void lua_arena_interior_enter (lua_State *L);
  LUAI_FUNC void lua_arena_interior_leave (lua_State *L);
  /* an error unwinds out of a holder body without running its 'leave', so
     'luaD_throw' clears the counter: a leaked increment must not turn into a
     false assert on the next compaction */
  LUAI_FUNC void lua_arena_interior_reset (lua_State *L);
  /*
  ** The compaction plan a dry run can compute: the same arithmetic as the
  ** planner in lgc.c, on the class counters alone (a dry run carves no
  ** segment, so it has no segment to inspect).
  */
  LUAI_FUNC size_t lua_arena_dry_plan (lua_Arena *a);
LUAI_FUNC void lua_arena_defaults (lua_ArenaConfig *cfg);

/* attach the arena to a state created with 'lua_arena_alloc' and this
** arena as its 'ud' */
LUAI_FUNC void lua_arena_attach (lua_State *L, lua_Arena *a);

/* slot-level primitives, used by luaC_compact (lgc.c) to move a block
** from one slot of a class to another: take allocates, give frees (the
** caller has already copied the payload) */
LUAI_FUNC void *lua_arena_take_slot (lua_Arena *a, int cid);
LUAI_FUNC void lua_arena_give_slot (lua_Arena *a, int cid, void *block);

/* segment introspection, used by the compaction planner in lgc.c */
LUAI_FUNC ArenaSeg *lua_arena_segment (ArenaClass *c, const void *p);
LUAI_FUNC unsigned lua_arena_segment_index (ArenaClass *c, ArenaSeg *s);
/* return a segment to mimalloc when it is wholly free; 1 when dropped */
LUAI_FUNC int lua_arena_release_segment (lua_Arena *a, int cid, ArenaSeg *s);

#endif					/* } */

#endif
