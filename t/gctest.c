/*
 * Test-only module: probes what a host's fault handler sees on each thread,
 * by sending the thread a SIGSEGV with tgkill and recording, in a handler
 * installed with SA_ONSTACK, the value of lua_thread_blocks_others() and
 * whether the handler runs on an alternate signal stack.  A real fault
 * (anything not sent with tgkill) restores the previous handler and returns,
 * so that it faults again and is handled as usual.
 * Not installed.  Linux only; elsewhere the probes return nil.
 */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include "lua.h"
#include "lauxlib.h"

#ifdef __linux__
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static struct sigaction old_sa;

/* written by the handler */
static volatile sig_atomic_t probe_hits;
static volatile sig_atomic_t probe_blocking;
static volatile sig_atomic_t probe_onstack;
static volatile long probe_altstack_size;

static pid_t my_tid(void)
{
  return (pid_t)syscall(SYS_gettid);
}

static int send_probe(pid_t tid)
{
  return (int)syscall(SYS_tgkill, getpid(), tid, SIGSEGV);
}

static void probe_handler(int sig, siginfo_t *si, void *uc)
{
  stack_t ss;

  (void)uc;
  if (si->si_code != SI_TKILL) {
    sigaction(sig, &old_sa, NULL);
    return;
  }
  if (lua_thread_blocks_others()) {
    probe_blocking++;
  }
  if (sigaltstack(NULL, &ss) == 0 && (ss.ss_flags & SS_ONSTACK)) {
    probe_onstack = 1;
    probe_altstack_size = (long)ss.ss_size;
  }
  probe_hits++;
}

static void install(void)
{
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = probe_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, &old_sa);
}

static void uninstall(void)
{
  sigaction(SIGSEGV, &old_sa, NULL);
}

static void reset(void)
{
  probe_hits = 0;
  probe_blocking = 0;
  probe_onstack = 0;
  probe_altstack_size = 0;
}

static void pause_us(long us)
{
  struct timespec ts = { 0, us * 1000L };
  nanosleep(&ts, NULL);
}

/* lua_thread_blocks_others() on the calling thread */
static int gctest_blocks_others(lua_State *L)
{
  lua_pushboolean(L, lua_thread_blocks_others());
  return 1;
}

/* Probes each thread named lua-gtrace; returns a list of
 * { blocks_others = bool, onstack = bool, altstack_size = n } */
static int gctest_probe_trace_threads(lua_State *L)
{
  DIR *d = opendir("/proc/self/task");
  struct dirent *e;
  int n = 0;

  if (!d) {
    return 0;
  }
  lua_newtable(L);
  install();
  while ((e = readdir(d)) != NULL) {
    char path[64], comm[32] = "";
    FILE *f;
    pid_t tid = (pid_t)atoi(e->d_name);
    int waited;

    if (tid <= 0) {
      continue;
    }
    snprintf(path, sizeof(path), "/proc/self/task/%d/comm", (int)tid);
    f = fopen(path, "r");
    if (!f) {
      continue;
    }
    if (!fgets(comm, sizeof(comm), f)) {
      comm[0] = '\0';
    }
    fclose(f);
    if (strncmp(comm, "lua-gtrace", 10) != 0) {
      continue;
    }

    reset();
    if (send_probe(tid) != 0) {
      continue;
    }
    for (waited = 0; !probe_hits && waited < 2000; waited++) {
      pause_us(1000);
    }
    if (!probe_hits) {
      continue;
    }
    lua_newtable(L);
    lua_pushboolean(L, probe_blocking > 0);
    lua_setfield(L, -2, "blocks_others");
    lua_pushboolean(L, probe_onstack);
    lua_setfield(L, -2, "onstack");
    lua_pushnumber(L, (lua_Number)probe_altstack_size);
    lua_setfield(L, -2, "altstack_size");
    lua_rawseti(L, -2, ++n);
  }
  uninstall();
  closedir(d);
  return 1;
}

struct prober {
  pid_t target;
  volatile int stop;
};

static void *prober_thread(void *arg)
{
  struct prober *p = arg;

  while (!p->stop) {
    send_probe(p->target);
    pause_us(50);
  }
  return NULL;
}

/* Calls f() while another thread keeps probing the calling thread; returns
 * how many probes saw lua_thread_blocks_others() set, and how many ran. */
static int gctest_probe_during(lua_State *L)
{
  struct prober p;
  pthread_t thr;
  int status;

  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_settop(L, 1);
  reset();
  install();
  p.target = my_tid();
  p.stop = 0;
  if (pthread_create(&thr, NULL, prober_thread, &p) != 0) {
    uninstall();
    return luaL_error(L, "pthread_create failed");
  }
  status = lua_pcall(L, 0, 0, 0);
  p.stop = 1;
  pthread_join(thr, NULL);
  uninstall();
  if (status != 0) {
    return lua_error(L);
  }
  lua_pushinteger(L, probe_blocking);
  lua_pushinteger(L, probe_hits);
  return 2;
}

static const luaL_Reg funcs[] = {
  { "blocks_others", gctest_blocks_others },
  { "probe_trace_threads", gctest_probe_trace_threads },
  { "probe_during", gctest_probe_during },
  { NULL, NULL }
};
#else
static const luaL_Reg funcs[] = {
  { NULL, NULL }
};
#endif

int luaopen_gctest(lua_State *L)
{
  luaL_register(L, "gctest", funcs);
  return 1;
}
