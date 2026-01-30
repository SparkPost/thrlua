/*
 * Copyright (c) 2010-2020 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 */

#include "thrlua.h"

#define THRLIB_THREAD "thread.thread"
#define THRLIB_MUTEX  "thread.mutex"
#define THRLIB_COND  "thread.cond"
#define THRLIB_RWLOCK "thread.rwlock"

struct thrlib_thread {
  pthread_t osthr;
  int joined;
  lua_State *L;
};

extern void lua_name_thread(char *thread_name);

static int thrlib_traceback(lua_State *L)
{
  // FIXME: write a test for traceback inside a thread
  if (!lua_isstring(L, 1))  {
    /* 'message' not a string? */
    fprintf(stderr, "TRACEBACK: error is not a string\n");
    return 1;  /* keep it intact */
  }
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    fprintf(stderr, "TRACEBACK: can't find debug lib?\n");
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    fprintf(stderr, "TRACEBACK: can't find debug.traceback?\n");
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);  /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);  /* call debug.traceback */
  return 1;
}

static void log_pcall_error (lua_State *L, int st)
{
    static const char *fmt = "thread pcall failed with status %d, error=%s\n";
    const char *msg = lua_tostring(L, -1);

    if (msg == NULL) {
      msg = "<unknown>";
    }
    fprintf(stderr, fmt, st, msg);
    thrlua_log(L, DERROR, fmt, st, msg);
}

static void *thrlib_thread_func(void *arg)
{
  struct thrlib_thread *th = arg;
  int st;
  lua_State *L = th->L;

  /* on entry, stack looks like this:
   * [1] traceback
   * [2] closure
   * [3] closure argument
   * Therefore, we call pcall with 1 arg and a base of 1 (traceback) */

  lua_name_thread("lua-tfunc");
  st = lua_pcall(L, 1, 0, 1);
  if (st != 0) {
    log_pcall_error(L, st);
  }
  lua_settop(L, 0);
  luaC_localgc(L, GCFULL);

  /* we exit leaving a ref to the lua_State; whoever joins will un-pin
   * and inherit that thread */

  return 0;
}

static void *thrlib_detached_thread_func(void *arg)
{
  lua_State *L = arg;
  int st;

  /* on entry, stack looks like this:
   * [1] traceback
   * [2] closure
   * [3] closure argument
   * Therefore, we call pcall with 1 arg and a base of 1 (traceback) */

  lua_name_thread("lua-tfunc-det");
  st = lua_pcall(L, 1, 0, 1);
  if (st != 0) {
    log_pcall_error(L, st);
  }

  lua_settop(L, 0);
  luaC_localgc(L, GCDESTROY);

  lua_delrefthread(L, NULL);
  return 0;
}

static int thrlib_create(lua_State *L)
{
  struct thrlib_thread *th;
  int err;
  int nargs = lua_gettop(L);
  int joinable = 1;
  lua_State *newL;

  luaL_argcheck(L, lua_isfunction(L, 1), 1,
    "function expected");

  if (nargs >= 3) {
    luaL_checktype(L, 3, LUA_TBOOLEAN);
    joinable = lua_toboolean(L, 3);
    lua_pop(L, 1);
  }

  if (joinable) {
    th = lua_newuserdata(L, sizeof(*th));
    memset(th, 0, sizeof(*th));
  }

  newL = lua_newthread(L);

  if (joinable) {
    th->L = newL;
  }

  /* one ref for the OS-level thread */
  ck_pr_inc_32(&newL->gch.ref);

  /* trace function is on the top of the stack */
  lua_pushcfunction(newL, thrlib_traceback);

  /* make a copy of 1st function parameter; put on top of stack */
  lua_pushvalue(L, 1);
  /* move function over to new thread */
  lua_xmove(L, newL, 1);
  /* pop new lua state from this stack */
  lua_pop(L, 1);
  /* copy userdata over to new thread; it will be the first arg
   * to the function */
  lua_pushvalue(L, -1);
  lua_xmove(L, newL, 1);

  if (joinable) {
    luaL_getmetatable(L, THRLIB_THREAD);
    lua_setmetatable(L, -2);

    err = pthread_create(&th->osthr, NULL, thrlib_thread_func, th);
  } else {
    pthread_attr_t attr;
    pthread_t osthr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    err = pthread_create(&osthr, &attr, thrlib_detached_thread_func, newL);

    pthread_attr_destroy(&attr);
  }

  if (err) {
    return luaL_error(L, "thread.create failed: %d %s", err, strerror(err));
  }
  return 1;
}

static int thread_join(lua_State *L)
{
  struct thrlib_thread *th = luaL_checkudata(L, 1, THRLIB_THREAD);
  if (!th->joined) {
    void *retval = NULL;
    pthread_join(th->osthr, &retval);
    th->joined = 1;

    /* we inherit this thread */
    luaC_inherit_thread(L, th->L);
    ck_pr_dec_32(&th->L->gch.ref);
    th->L = NULL;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int thread_gc(lua_State *L)
{
  return thread_join(L);
}

static const luaL_Reg thread_funcs[] = {
  {"__gc", thread_gc },
  {"join", thread_join },
  {NULL, NULL}
};


static int thrlib_sleep(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  n = sleep(n);
  lua_pushinteger(L, n);
  return 1;
}

static int thrlib_mutex_new(lua_State *L)
{
  pthread_mutexattr_t mattr;
  int nargs = lua_gettop(L);
  pthread_mutex_t *mtx = lua_newuserdata(L, sizeof(*mtx));

  pthread_mutexattr_init(&mattr);
  LUAI_TRY_BLOCK(L) {
    lua_Integer t = PTHREAD_MUTEX_ERRORCHECK;

    if (nargs) {
      t = luaL_checkinteger(L, 1);
      switch (t) {
        case PTHREAD_MUTEX_NORMAL:
        case PTHREAD_MUTEX_RECURSIVE:
        case PTHREAD_MUTEX_ERRORCHECK:
          break;
        default:
          luaL_argcheck(L, 0, 1,
            "specify one of thread.MUTEX_NORMAL, "
            "thread.MUTEX_RECURSIVE or thread.MUTEX_ERRORCHECK");
      }
    }
    pthread_mutexattr_settype(&mattr, t);

    pthread_mutex_init(mtx, &mattr);
  } LUAI_TRY_FINALLY(L) {
    pthread_mutexattr_destroy(&mattr);
  } LUAI_TRY_END(L);

  luaL_getmetatable(L, THRLIB_MUTEX);
  lua_setmetatable(L, -2);

  return 1;
}

/**
 * Diagnostics for pthread_* mutex/wrlock return codes.
 *
 * @param[in] L Lua thread
 * @param[in] what String describing lock, e.g.: "mutex"
 * @param[in] op String describing operation, e.g.: "unlock"
 * @param[in] ret Return code from pthread_* functions
 *
 * @return Number of values pushed onto Lua stack
 *
 * @internal
 */
static int handle_lock_return(lua_State *L, const char *what, const char *op, int ret)
{
  switch (ret) {
    case 0:
      lua_pushboolean(L, 1);
      return 1;

    case EINVAL:
    case EDEADLK:
    case EPERM:
      return luaL_error(L, "failed to %s %s: %d %s",
        op, what, ret, strerror(ret));

    default:
      lua_pushinteger(L, ret);
      return 1;
  }
}

static int thrlib_mutex_lock(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);
  int ret;

  ret = pthread_mutex_lock(mtx);

  return handle_lock_return(L, "mutex", "lock", ret);
}

static int thrlib_mutex_trylock(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);
  int ret;

  ret = pthread_mutex_trylock(mtx);

  return handle_lock_return(L, "mutex", "trylock", ret);
}

static int thrlib_mutex_unlock(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);
  int ret;

  ret = pthread_mutex_unlock(mtx);

  return handle_lock_return(L, "mutex", "unlock", ret);
}

static int thrlib_mutex_gc(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);

  pthread_mutex_destroy(mtx);

  return 0;
}

struct thrlib_cond {
  pthread_cond_t cond;
  pthread_mutex_t *mtx;
  pthread_mutex_t mymtx;
  void *mtxref;
};

static int thrlib_cond_gc(lua_State *L)
{
  struct thrlib_cond *c = luaL_checkudata(L, 1, THRLIB_COND);

  pthread_cond_destroy(&c->cond);
  if (c->mtxref) {
    lua_delrefobj(L, c->mtxref);
  } else {
    pthread_mutex_destroy(&c->mymtx);
  }

  return 0;
}

static int thrlib_cond_broadcast(lua_State *L)
{
  struct thrlib_cond *c = luaL_checkudata(L, 1, THRLIB_COND);
  int res;

  res = pthread_cond_broadcast(&c->cond);
  if (res == 0) {
    return 0;
  }

  luaL_error(L, "cond:broadcast failed %s", strerror(res));
  return 0;
}

static int thrlib_cond_wait(lua_State *L)
{
  int nargs = lua_gettop(L);
  struct thrlib_cond *c = luaL_checkudata(L, 1, THRLIB_COND);
  int res;
  struct timespec ts;

#define NANOSECONDS_PER_SECOND 1000000000

  if (nargs > 1) {
    struct timeval tv;
    lua_Number n = luaL_checknumber(L, 2);

    ts.tv_sec = floor(n);
    ts.tv_nsec = (n - ts.tv_sec) * NANOSECONDS_PER_SECOND;

    gettimeofday(&tv, NULL);
    ts.tv_nsec += tv.tv_usec * 1000;
    while (ts.tv_nsec > NANOSECONDS_PER_SECOND) {
      ts.tv_sec++;
      ts.tv_nsec -= NANOSECONDS_PER_SECOND;
    }
    ts.tv_sec += tv.tv_sec;

    res = pthread_cond_timedwait(&c->cond, c->mtx, &ts);

    switch (res) {
      case ETIMEDOUT:
        lua_pushboolean(L, 0);
        return 1;
      case 0:
        lua_pushboolean(L, 1);
        return 1;
      default:
        luaL_error(L, "cond:wait failed %s", strerror(res));
        return 0;
    }
  }

  /* not timed */

  res = pthread_cond_wait(&c->cond, c->mtx);

  if (res == 0) {
    return 0;
  }

  luaL_error(L, "cond:wait failed %s", strerror(res));
  return 0;
}

static int thrlib_cond_signal(lua_State *L)
{
  struct thrlib_cond *c = luaL_checkudata(L, 1, THRLIB_COND);
  int res;

  res = pthread_cond_signal(&c->cond);
  if (res == 0) {
    return 0;
  }

  luaL_error(L, "cond:signal failed %s", strerror(res));
  return 0;
}

static int thrlib_cond_lock(lua_State *L)
{
  struct thrlib_cond *c = luaL_checkudata(L, 1, THRLIB_COND);
  int ret;

  ret = pthread_mutex_lock(c->mtx);

  return handle_lock_return(L, "mutex", "lock", ret);
}

static int thrlib_cond_unlock(lua_State *L)
{
  struct thrlib_cond *c = luaL_checkudata(L, 1, THRLIB_COND);
  int ret;

  ret = pthread_mutex_unlock(c->mtx);

  return handle_lock_return(L, "mutex", "unlock", ret);
}

static int thrlib_cond_new(lua_State *L)
{
  int nargs = lua_gettop(L);
  struct thrlib_cond *c = lua_newuserdata(L, sizeof(*c));

  memset(c, 0, sizeof(*c));
  pthread_cond_init(&c->cond, NULL);

  if (nargs) {
    pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);

    c->mtxref = lua_addrefobj(L, 1);
    c->mtx = mtx;

  } else {
    pthread_mutex_init(&c->mymtx, NULL);
    c->mtx = &c->mymtx;
  }

  luaL_getmetatable(L, THRLIB_COND);
  lua_setmetatable(L, -2);

  return 1;
}

static int thrlib_rwlock_rdlock(lua_State *L)
{
  pthread_rwlock_t *rwlock = luaL_checkudata(L, 1, THRLIB_RWLOCK);
  int ret = pthread_rwlock_rdlock(rwlock);
  return handle_lock_return(L, "rwlock", "rdlock", ret);
}

static int thrlib_rwlock_tryrdlock(lua_State *L)
{
  pthread_rwlock_t *rwlock = luaL_checkudata(L, 1, THRLIB_RWLOCK);
  int ret = pthread_rwlock_tryrdlock(rwlock);
  return handle_lock_return(L, "rwlock", "tryrdlock", ret);
}

static int thrlib_rwlock_wrlock(lua_State *L)
{
  pthread_rwlock_t *rwlock = luaL_checkudata(L, 1, THRLIB_RWLOCK);
  int ret = pthread_rwlock_wrlock(rwlock);
  return handle_lock_return(L, "rwlock", "wrlock", ret);
}

static int thrlib_rwlock_trywrlock(lua_State *L)
{
  pthread_rwlock_t *rwlock = luaL_checkudata(L, 1, THRLIB_RWLOCK);
  int ret = pthread_rwlock_trywrlock(rwlock);
  return handle_lock_return(L, "rwlock", "trywrlock", ret);
}

static int thrlib_rwlock_unlock(lua_State *L)
{
  pthread_rwlock_t *rwlock = luaL_checkudata(L, 1, THRLIB_RWLOCK);
  int ret = pthread_rwlock_unlock(rwlock);
  return handle_lock_return(L, "rwlock", "unlock", ret);
}

static int thrlib_rwlock_new(lua_State *L)
{
  pthread_rwlock_t *rwlock = lua_newuserdata(L, sizeof(*rwlock));

  /* XXX: support attributes like in thrlib_mutex_new() */
  pthread_rwlock_init(rwlock, NULL);

  luaL_getmetatable(L, THRLIB_RWLOCK);
  lua_setmetatable(L, -2);

  return 1;
}

static int thrlib_rwlock_gc(lua_State *L)
{
  pthread_rwlock_t *rwlock = luaL_checkudata(L, 1, THRLIB_RWLOCK);
  pthread_rwlock_destroy(rwlock);
  return 0;
}

static const luaL_Reg mutex_funcs[] = {
  {"lock", thrlib_mutex_lock },
  {"unlock", thrlib_mutex_unlock },
  {"trylock", thrlib_mutex_trylock },
  {"__gc", thrlib_mutex_gc },
  {NULL, NULL}
};

static const luaL_Reg cond_funcs[] = {
  {"acquire", thrlib_cond_lock },
  {"release", thrlib_cond_unlock },
  {"broadcast", thrlib_cond_broadcast },
  {"signal", thrlib_cond_signal },
  {"wait", thrlib_cond_wait },
  {"__gc", thrlib_cond_gc },
  {NULL, NULL}
};

static const luaL_Reg rwlock_funcs[] = {
  {"rdlock", thrlib_rwlock_rdlock },
  {"wrlock", thrlib_rwlock_wrlock },
  {"tryrdlock", thrlib_rwlock_tryrdlock },
  {"trywrlock", thrlib_rwlock_trywrlock },
  {"unlock", thrlib_rwlock_unlock },
  {"__gc", thrlib_rwlock_gc },
  {NULL, NULL}
};

static const luaL_Reg thrlib[] = {
  {"create", thrlib_create },
  {"sleep", thrlib_sleep },
  {"mutex", thrlib_mutex_new },
  {"condition", thrlib_cond_new },
  {"rwlock", thrlib_rwlock_new },
  {NULL, NULL}
};

LUALIB_API int luaopen_thread(lua_State *L)
{
  /* OS thread metatable */
  luaL_newmetatable(L, THRLIB_THREAD);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, thread_funcs);

  /* mutex metatable */
  luaL_newmetatable(L, THRLIB_MUTEX);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, mutex_funcs);

  /* cond metatable */
  luaL_newmetatable(L, THRLIB_COND);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, cond_funcs);

  /* rwlock metatable */
  luaL_newmetatable(L, THRLIB_RWLOCK);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, rwlock_funcs);

  luaL_register(L, LUA_THREADLIBNAME, thrlib);

  /* thread.MUTEX_NORMAL through thread.MUTEX_ERRORCHECK */
  lua_pushinteger(L, PTHREAD_MUTEX_NORMAL);
  lua_setfield(L, -2, "MUTEX_NORMAL");
  lua_pushinteger(L, PTHREAD_MUTEX_RECURSIVE);
  lua_setfield(L, -2, "MUTEX_RECURSIVE");
  lua_pushinteger(L, PTHREAD_MUTEX_ERRORCHECK);
  lua_setfield(L, -2, "MUTEX_ERRORCHECK");

  return 1;
}

/* vim:ts=2:sw=2:et:
 */
