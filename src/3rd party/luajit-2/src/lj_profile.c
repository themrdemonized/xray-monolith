/*
** Low-overhead profiling.
** Copyright (C) 2005-2016 Mike Pall. See Copyright Notice in luajit.h
**
** Backported from LuaJIT 2.1 to this 2.0.4 fork: native dumpstack (no lj_buf),
** no prof_mode. Arming rides LUA_MASKLINE, so a sample resolves at the next
** interpreted bytecode; inside a JIT trace, at the next trace exit (the
** JIT-compiled (N) share reads as a floor). prof_mode needs JIT-backend codegen.
*/

#define lj_profile_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASPROFILE

#include "lj_dispatch.h"
#include "lj_profile.h"
#include "lj_allocprof.h"

#include "luajit.h"
#include "lua.h"

#include <stdio.h>

#if LJ_PROFILE_SIGPROF

#include <sys/time.h>
#include <signal.h>
#define profile_lock(ps)	UNUSED(ps)
#define profile_unlock(ps)	UNUSED(ps)

#elif LJ_PROFILE_PTHREAD

#include <pthread.h>
#include <time.h>
#define profile_lock(ps)	pthread_mutex_lock(&ps->lock)
#define profile_unlock(ps)	pthread_mutex_unlock(&ps->lock)

#elif LJ_PROFILE_WTHREAD

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef unsigned int (WINAPI *WMM_TPFUNC)(unsigned int);
#define profile_lock(ps)	EnterCriticalSection(&ps->lock)
#define profile_unlock(ps)	LeaveCriticalSection(&ps->lock)

#endif

/* Profiler state. */
typedef struct ProfileState {
  global_State *g;		/* VM state that started the profiler. */
  luaJIT_profile_callback cb;	/* Profiler callback. */
  void *data;			/* Profiler callback data. */
  int interval;			/* Sample interval in milliseconds. */
  int samples;			/* Number of samples for next callback. */
  int vmstate;			/* VM state when profile timer triggered. */
  int added_maskline;		/* Profiler owns the LUA_MASKLINE trap bit. */
  int32_t saved_count;		/* Saved hookcount, restored on stop. */
  int32_t saved_cstart;		/* Saved hookcstart, restored on stop. */
#if LJ_PROFILE_SIGPROF
  struct sigaction oldsa;	/* Previous SIGPROF state. */
#elif LJ_PROFILE_PTHREAD
  pthread_mutex_t lock;		/* g->hookmask update lock. */
  pthread_t thread;		/* Timer thread. */
  int abort;			/* Abort timer thread. */
#elif LJ_PROFILE_WTHREAD
  HINSTANCE wmm;		/* WinMM library handle. */
  WMM_TPFUNC wmm_tbp;		/* WinMM timeBeginPeriod function. */
  WMM_TPFUNC wmm_tep;		/* WinMM timeEndPeriod function. */
  CRITICAL_SECTION lock;	/* g->hookmask update lock. */
  HANDLE thread;		/* Timer thread. */
  int abort;			/* Abort timer thread. */
#endif
} ProfileState;

/* Sadly, we have to use a static profiler state. Only one VM at a time. */
static ProfileState profile_state;

/* Default sample interval in milliseconds. */
#define LJ_PROFILE_INTERVAL_DEFAULT	10

/* -- Profiler/hook interaction ------------------------------------------- */

#if !LJ_PROFILE_SIGPROF
void LJ_FASTCALL lj_profile_hook_enter(global_State *g)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    hook_enter(g);
    profile_unlock(ps);
  } else {
    hook_enter(g);
  }
}

void LJ_FASTCALL lj_profile_hook_leave(global_State *g)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    hook_leave(g);
    profile_unlock(ps);
  } else {
    hook_leave(g);
  }
}

int lj_profile_lock(void)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    return 1;
  } else {
    return 0;
  }
}

void lj_profile_unlock(void)
{
  ProfileState *ps = &profile_state;
  profile_unlock(ps);
}
#endif

/* -- Profile callbacks --------------------------------------------------- */

/* Callback from profile hook (HOOK_PROFILE already cleared). */
void LJ_FASTCALL lj_profile_interpreter(lua_State *L)
{
  ProfileState *ps = &profile_state;
  global_State *g = G(L);
  uint8_t mask, armbits;
  if (lj_allocprof_active) {
    lj_allocprof_interpreter(L);
    return;
  }
  armbits = (uint8_t)(HOOK_PROFILE |
		      (ps->added_maskline ? LUA_MASKLINE : 0));
  profile_lock(ps);
  mask = (uint8_t)(g->hookmask & ~armbits);
  if (!(mask & HOOK_VMEVENT)) {
    int samples = ps->samples;
    ps->samples = 0;
    g->hookmask = HOOK_VMEVENT;
    lj_dispatch_update(g, 1);
    profile_unlock(ps);
    ps->cb(ps->data, L, samples, ps->vmstate);  /* Invoke user callback. */
    profile_lock(ps);
    mask |= (uint8_t)(g->hookmask & armbits);
  }
  g->hookmask = mask;
  lj_dispatch_update(g, 1);
  profile_unlock(ps);
}

/* Trigger profile hook. Asynchronous call from OS-specific profile timer. */
static void profile_trigger(ProfileState *ps)
{
  global_State *g = ps->g;
  uint8_t mask;
  profile_lock(ps);
  ps->samples++;  /* Always increment number of samples. */
  mask = g->hookmask;
  if (!(mask & (HOOK_PROFILE|HOOK_VMEVENT|HOOK_GC))) {  /* Set profile hook. */
    int st = g->vmstate;
    ps->vmstate = st >= 0 ? 'N' :
		  st == ~LJ_VMST_INTERP ? 'I' :
		  st == ~LJ_VMST_C ? 'C' :
		  st == ~LJ_VMST_GC ? 'G' : 'J';
    g->hookmask = (uint8_t)(mask | HOOK_PROFILE |
			    (ps->added_maskline ? LUA_MASKLINE : 0));
    lj_dispatch_update(g, 1);
  }
  profile_unlock(ps);
}

/* -- OS-specific profile timer handling ---------------------------------- */

#if LJ_PROFILE_SIGPROF

/* SIGPROF handler. */
static void profile_signal(int sig)
{
  UNUSED(sig);
  profile_trigger(&profile_state);
}

/* Start profiling timer. */
static void profile_timer_start(ProfileState *ps)
{
  int interval = ps->interval;
  struct itimerval tm;
  struct sigaction sa;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = interval / 1000;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = (interval % 1000) * 1000;
  setitimer(ITIMER_PROF, &tm, NULL);
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = profile_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPROF, &sa, &ps->oldsa);
}

/* Stop profiling timer. */
static void profile_timer_stop(ProfileState *ps)
{
  struct itimerval tm;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = 0;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = 0;
  setitimer(ITIMER_PROF, &tm, NULL);
  sigaction(SIGPROF, &ps->oldsa, NULL);
}

#elif LJ_PROFILE_PTHREAD

/* POSIX timer thread. */
static void *profile_thread(ProfileState *ps)
{
  int interval = ps->interval;
  struct timespec ts;
  ts.tv_sec = interval / 1000;
  ts.tv_nsec = (interval % 1000) * 1000000;
  while (1) {
    nanosleep(&ts, NULL);
    if (ps->abort) break;
    profile_trigger(ps);
  }
  return NULL;
}

/* Start profiling timer thread. */
static void profile_timer_start(ProfileState *ps)
{
  pthread_mutex_init(&ps->lock, 0);
  ps->abort = 0;
  pthread_create(&ps->thread, NULL, (void *(*)(void *))profile_thread, ps);
}

/* Stop profiling timer thread. */
static void profile_timer_stop(ProfileState *ps)
{
  ps->abort = 1;
  pthread_join(ps->thread, NULL);
  pthread_mutex_destroy(&ps->lock);
}

#elif LJ_PROFILE_WTHREAD

/* Windows timer thread. */
static DWORD WINAPI profile_thread(void *psx)
{
  ProfileState *ps = (ProfileState *)psx;
  int interval = ps->interval;
  if (ps->wmm_tbp) ps->wmm_tbp(interval);
  while (1) {
    Sleep(interval);
    if (ps->abort) break;
    profile_trigger(ps);
  }
  if (ps->wmm_tep) ps->wmm_tep(interval);
  return 0;
}

/* Start profiling timer thread. */
static void profile_timer_start(ProfileState *ps)
{
  if (!ps->wmm) {  /* Load WinMM library on-demand. */
    ps->wmm = LoadLibraryExA("winmm.dll", NULL, 0);
    if (ps->wmm) {
      ps->wmm_tbp = (WMM_TPFUNC)GetProcAddress(ps->wmm, "timeBeginPeriod");
      ps->wmm_tep = (WMM_TPFUNC)GetProcAddress(ps->wmm, "timeEndPeriod");
      if (!ps->wmm_tbp || !ps->wmm_tep) {
	ps->wmm = NULL;
	ps->wmm_tbp = ps->wmm_tep = NULL;
      }
    }
  }
  InitializeCriticalSection(&ps->lock);
  ps->abort = 0;
  ps->thread = CreateThread(NULL, 0, profile_thread, ps, 0, NULL);
}

/* Stop profiling timer thread. */
static void profile_timer_stop(ProfileState *ps)
{
  ps->abort = 1;
  WaitForSingleObject(ps->thread, INFINITE);
  DeleteCriticalSection(&ps->lock);
}

#endif

/* -- Public profiling API ------------------------------------------------ */

int lj_profile_active(void)
{
  return profile_state.g != NULL;
}

/* Start profiling. */
LUA_API void luaJIT_profile_start(lua_State *L, const char *mode,
				  luaJIT_profile_callback cb, void *data)
{
  ProfileState *ps = &profile_state;
  int interval = LJ_PROFILE_INTERVAL_DEFAULT;
  if (lj_allocprof_active) return;
  while (*mode) {
    int m = *mode++;
    switch (m) {
    case 'i':
      interval = 0;
      while (*mode >= '0' && *mode <= '9')
	interval = interval * 10 + (*mode++ - '0');
      if (interval <= 0) interval = 1;
      break;
    /* 'l'/'f' (JIT line/func granularity) are no-op in phase 1: no prof_mode. */
    default:  /* Ignore unknown mode chars. */
      break;
    }
  }
  if (ps->g) {
    luaJIT_profile_stop(L);
    if (ps->g) return;  /* Profiler in use by another VM. */
  }
  ps->g = G(L);
  ps->interval = interval;
  ps->cb = cb;
  ps->data = data;
  ps->samples = 0;
  ps->added_maskline = !(G(L)->hookmask & LUA_MASKLINE);
  ps->saved_count = G(L)->hookcount;
  ps->saved_cstart = G(L)->hookcstart;
  profile_timer_start(ps);
}

/* Stop profiling. */
LUA_API void luaJIT_profile_stop(lua_State *L)
{
  ProfileState *ps = &profile_state;
  global_State *g = ps->g;
  if (G(L) == g) {  /* Only stop profiler if started by this VM. */
    ps->g = NULL;
    profile_timer_stop(ps);
    g->hookmask &= (uint8_t)~(HOOK_PROFILE |
			      (ps->added_maskline ? LUA_MASKLINE : 0));
    g->hookcount = ps->saved_count;
    g->hookcstart = ps->saved_cstart;
    lj_dispatch_update(g, 0);
  }
}

/* Native stack dump: "name@short_src:linedefined" per frame, ';'-separated, top first. fmt ignored. */
LUA_API const char *luaJIT_profile_dumpstack(lua_State *L, const char *fmt,
					     int depth, size_t *len)
{
  static char buf[2048];
  size_t n = 0;
  int level;
  int maxd = depth < 0 ? -depth : depth;
  lua_Debug ar;
  UNUSED(fmt);
  if (maxd <= 0) maxd = 1;
  for (level = 0; level < maxd; level++) {
    const char *src;
    int w;
    if (n >= sizeof(buf) - 64) break;
    if (!lua_getstack(L, level, &ar)) break;
    lua_getinfo(L, "Sln", &ar);
    if (n) buf[n++] = ';';
    src = ar.short_src[0] ? ar.short_src : "?";
    if (ar.name && ar.name[0])
      w = snprintf(buf + n, sizeof(buf) - n, "%s@%s:%d", ar.name, src, ar.linedefined);
    else
      w = snprintf(buf + n, sizeof(buf) - n, "%s:%d", src, ar.linedefined);
    if (w < 0) break;
    n += (size_t)w;
    if (n >= sizeof(buf)) { n = sizeof(buf) - 1; break; }
  }
  buf[n] = '\0';
  *len = n;
  return buf;
}

#endif
