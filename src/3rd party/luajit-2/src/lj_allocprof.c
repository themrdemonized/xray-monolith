/*
** Allocation profiler: byte-sampled per-function attribution (jit.allocprof).
*/

#define lj_allocprof_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASPROFILE

#include "lj_allocprof.h"
#include "lj_dispatch.h"
#include "lj_profile.h"
#include "lj_trace.h"
#include "luajit.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define ALLOCPROF_LEAF_SLOTS	4096
#define ALLOCPROF_STACK_SLOTS	16384
#define ALLOCPROF_MAX_DEPTH	64
#define ALLOCPROF_SAMPLE_MEAN	8192

int lj_allocprof_active = 0;
uint64_t lj_allocprof_pending = 0;
uint64_t lj_allocprof_dropped = 0;
uint64_t lj_allocprof_drains = 0;
uint64_t lj_allocprof_seen = 0;

static struct {
  global_State *g;
  int depth;
  int leaf_used;
  int stack_used;
  uint8_t saved_mask;
  int32_t saved_count;
  int32_t saved_cstart;
  AllocProfEntry *leaf;   /* Heap, first start; kept so dump reads work after stop. */
  AllocProfEntry *stack;
} AS;

static uint32_t allocprof_hash(const char *s, size_t n)
{
  uint32_t h = 2166136261u;
  while (n--) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

/* Keys longer than the entry buffer are truncated BEFORE hash and compare, so a long stack aggregates into one entry instead of flooding the table. */
static void allocprof_record(AllocProfEntry *tab, int slots, int *used,
			     const char *key, uint64_t bytes)
{
  size_t n = strlen(key);
  uint32_t h, i;
  if (n >= sizeof(tab->key)) {
    /* Cut at the last ';' so a partial frame drops whole, never mid-name. */
    size_t j = sizeof(tab->key) - 1;
    while (j > 0 && key[j] != ';') j--;
    n = j > 0 ? j : sizeof(tab->key) - 1;
  }
  h = allocprof_hash(key, n) & (uint32_t)(slots - 1);
  for (i = 0; i < (uint32_t)slots; i++) {
    AllocProfEntry *e = &tab[(h + i) & (uint32_t)(slots - 1)];
    if (e->bytes == 0) {
      memcpy(e->key, key, n);
      e->key[n] = '\0';
      e->bytes = bytes;
      (*used)++;
      return;
    }
    if (e->key[n] == '\0' && memcmp(e->key, key, n) == 0) {
      e->bytes += bytes;
      return;
    }
  }
  lj_allocprof_dropped += bytes;
}

static uint64_t allocprof_rng = 0x9e3779b97f4a7c15ull;
static uint64_t allocprof_next = 0;

/* Next sample distance from exp(mean): randomized to avoid fixed-interval aliasing. */
static uint64_t allocprof_sample_dist(void)
{
  uint64_t x = allocprof_rng;
  double u, d;
  x ^= x << 13; x ^= x >> 7; x ^= x << 17;
  allocprof_rng = x;
  u = ((double)(x >> 11) + 1.0) / 9007199254740993.0;
  d = -(double)ALLOCPROF_SAMPLE_MEAN * log(u);
  return d < 1.0 ? 1u : (uint64_t)d;
}

LJ_NOINLINE void LJ_FASTCALL lj_allocprof_interpreter(lua_State *L)
{
  uint64_t bytes = lj_allocprof_pending;
  size_t len;
  const char *st;
  lj_allocprof_drains++;
  if (bytes < allocprof_next) return;
  lj_allocprof_pending = 0;
  lj_allocprof_seen += bytes;
  allocprof_next = allocprof_sample_dist();
  st = luaJIT_profile_dumpstack(L, "", AS.depth, &len);
  if (!st || !st[0]) return;
  {
    const char *semi = strchr(st, ';');
    size_t llen = semi ? (size_t)(semi - st) : len;
    char leafkey[192];
    if (llen >= sizeof(leafkey)) llen = sizeof(leafkey) - 1;
    memcpy(leafkey, st, llen);
    leafkey[llen] = '\0';
    allocprof_record(AS.leaf, ALLOCPROF_LEAF_SLOTS, &AS.leaf_used, leafkey, bytes);
  }
  if (AS.depth > 1)
    allocprof_record(AS.stack, ALLOCPROF_STACK_SLOTS, &AS.stack_used, st, bytes);
}

void lj_allocprof_start(lua_State *L, int depth)
{
  global_State *g = G(L);
  if (lj_profile_active()) return;
  if (AS.g) {
    lj_allocprof_stop(L);
    if (AS.g) return;
  }
  if (depth < 1) depth = 1;
  if (depth > ALLOCPROF_MAX_DEPTH) depth = ALLOCPROF_MAX_DEPTH;
  if (!AS.leaf) {
    AS.leaf = (AllocProfEntry *)calloc(ALLOCPROF_LEAF_SLOTS, sizeof(AllocProfEntry));
    AS.stack = (AllocProfEntry *)calloc(ALLOCPROF_STACK_SLOTS, sizeof(AllocProfEntry));
    if (!AS.leaf || !AS.stack) {
      free(AS.leaf); free(AS.stack);
      AS.leaf = NULL; AS.stack = NULL;
      return;
    }
  } else {
    memset(AS.leaf, 0, ALLOCPROF_LEAF_SLOTS * sizeof(AllocProfEntry));
    memset(AS.stack, 0, ALLOCPROF_STACK_SLOTS * sizeof(AllocProfEntry));
  }
  AS.leaf_used = 0;
  AS.stack_used = 0;
  AS.g = g;
  AS.depth = depth;
  AS.saved_mask = g->hookmask;
  AS.saved_count = g->hookcount;
  AS.saved_cstart = g->hookcstart;
  lj_allocprof_pending = 0;
  lj_allocprof_dropped = 0;
  lj_allocprof_drains = 0;
  lj_allocprof_seen = 0;
  allocprof_next = allocprof_sample_dist();
  lj_allocprof_active = 1;
  lj_trace_abort(g);  /* The drain bypasses lj_trace_ins; abort any in-progress recording. */
  /* LUA_MASKLINE forces the per-instruction trap the drain uses (JIT traces bypass it). */
  g->hookmask = (uint8_t)(g->hookmask | HOOK_PROFILE | LUA_MASKLINE);
  lj_dispatch_update(g, 0);
}

void lj_allocprof_stop(lua_State *L)
{
  global_State *g = AS.g;
  if (G(L) == g) {
    lj_allocprof_active = 0;
    AS.g = NULL;
    g->hookmask = (uint8_t)((g->hookmask & ~(HOOK_PROFILE | LUA_MASKLINE)) |
			    (AS.saved_mask & (HOOK_PROFILE | LUA_MASKLINE)));
    g->hookcount = AS.saved_count;
    g->hookcstart = AS.saved_cstart;
    lj_dispatch_update(g, 0);
  }
}

void lj_allocprof_reset(void)
{
  if (!(AS.g && AS.leaf)) return;
  memset(AS.leaf, 0, ALLOCPROF_LEAF_SLOTS * sizeof(AllocProfEntry));
  memset(AS.stack, 0, ALLOCPROF_STACK_SLOTS * sizeof(AllocProfEntry));
  AS.leaf_used = 0;
  AS.stack_used = 0;
  lj_allocprof_dropped = 0;
  lj_allocprof_drains = 0;
  lj_allocprof_seen = 0;
}

void lj_allocprof_pause(void)
{
  global_State *g = AS.g;
  if (!g || !lj_allocprof_active) return;
  lj_allocprof_active = 0;
  g->hookmask &= (uint8_t)~(HOOK_PROFILE | LUA_MASKLINE);
  lj_dispatch_update(g, 0);
}

void lj_allocprof_resume(void)
{
  global_State *g = AS.g;
  if (!g || lj_allocprof_active) return;
  lj_allocprof_pending = 0;
  lj_allocprof_active = 1;
  lj_trace_abort(g);
  g->hookmask = (uint8_t)(g->hookmask | HOOK_PROFILE | LUA_MASKLINE);
  lj_dispatch_update(g, 0);
}

int lj_allocprof_count(int stacks)
{
  return stacks ? AS.stack_used : AS.leaf_used;
}

int lj_allocprof_usage(void)
{
  int leaf_pct = (int)((int64_t)AS.leaf_used * 100 / ALLOCPROF_LEAF_SLOTS);
  int stack_pct = (AS.depth > 1)
    ? (int)((int64_t)AS.stack_used * 100 / ALLOCPROF_STACK_SLOTS) : 0;
  return leaf_pct > stack_pct ? leaf_pct : stack_pct;
}

int lj_allocprof_slots(int stacks)
{
  return stacks ? ALLOCPROF_STACK_SLOTS : ALLOCPROF_LEAF_SLOTS;
}

const AllocProfEntry *lj_allocprof_slot(int stacks, int k)
{
  AllocProfEntry *tab = stacks ? AS.stack : AS.leaf;
  return (tab && tab[k].bytes) ? &tab[k] : NULL;
}

#endif
