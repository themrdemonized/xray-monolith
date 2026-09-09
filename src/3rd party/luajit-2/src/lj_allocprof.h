/*
** Allocation profiler: byte-sampled per-function attribution (jit.allocprof).
*/

#ifndef _LJ_ALLOCPROF_H
#define _LJ_ALLOCPROF_H

#include "lj_obj.h"

#if LJ_HASPROFILE

typedef struct AllocProfEntry {
  char key[4096];
  uint64_t bytes;
} AllocProfEntry;

extern int lj_allocprof_active;
extern uint64_t lj_allocprof_pending;
extern uint64_t lj_allocprof_dropped;
extern uint64_t lj_allocprof_drains;
extern uint64_t lj_allocprof_seen;

LJ_FUNC LJ_NOINLINE void LJ_FASTCALL lj_allocprof_interpreter(lua_State *L);
LJ_FUNC void lj_allocprof_start(lua_State *L, int depth);
LJ_FUNC void lj_allocprof_stop(lua_State *L);
LJ_FUNC void lj_allocprof_reset(void);
LJ_FUNC void lj_allocprof_pause(void);
LJ_FUNC void lj_allocprof_resume(void);
LJ_FUNC int lj_allocprof_count(int stacks);
LJ_FUNC int lj_allocprof_slots(int stacks);
LJ_FUNC const AllocProfEntry *lj_allocprof_slot(int stacks, int k);
LJ_FUNC int lj_allocprof_usage(void);

#endif

#endif
