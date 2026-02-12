/*
 * Copyright (c) 2010-2011 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 */

#ifndef THRLUA_H
#define THRLUA_H

#define _GNU_SOURCE
#include "luaconf.h"

#define LUA_ASSERTIONS 0
#define USING_DRD 0
#define DEBUG_ALLOC 0

#if HAVE__OPT_MSYS_3RDPARTY_INCLUDE_VALGRIND_VALGRIND_H
#include </opt/msys/3rdParty/include/valgrind/memcheck.h>
#include </opt/msys/3rdParty/include/valgrind/valgrind.h>
#include </opt/msys/3rdParty/include/valgrind/drd.h>
#define HAVE_VALGRIND 1
#elif HAVE__USR_LOCAL_INCLUDE_VALGRIND_VALGRIND_H
#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>
#include <valgrind/drd.h>
#define HAVE_VALGRIND 1
#endif

#if LUA_ASSERTIONS && HAVE_VALGRIND
struct GCheader;
LUAI_FUNC void lua_assert_fail(const char *expr, struct GCheader *obj, const char *file, int line);



# define lua_assert_obj(expr, __obj) \
  ((expr)                           \
   ? (void) (0)                     \
   : (lua_assert_fail (#expr, __obj, __FILE__, __LINE__), \
      (void) (0)))

# define lua_assert(expr)	lua_assert_obj(expr, 0)

#elif LUA_ASSERTIONS
# define lua_assert assert
# define lua_assert_obj(expr, __obj) assert(expr)
#else
# define lua_assert(expr) 0
# define lua_assert_obj(expr, __obj) 0
#endif


#ifndef MIN
#define MIN(x, y)               (((x) < (y)) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y)               (((x) > (y)) ? (x) : (y))
#endif


#include <setjmp.h>
#include <locale.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#if defined(LUA_DL_DLOPEN)
#include <dlfcn.h>
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

struct global_State;
typedef struct global_State global_State;

#include "llimits.h"
#include "lobject.h"
#include "ltable.h"
#include "lzio.h"
#include "llex.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lcode.h"
#include "ltm.h"
#include "lstate.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lstring.h"
#include "lundump.h"
#include "lvm.h"

/* Macros to set values */
static inline void setnilvalue(TValue *obj)
{
  obj->tt = LUA_TNIL;
}

static inline void setnvalue(TValue *obj, lua_Number n)
{
  obj->tt = LUA_TNUMBER;
  obj->value.n = n;
}

static inline void setpvalue(TValue *obj, void *ud)
{
  obj->tt = LUA_TLIGHTUSERDATA;
  obj->value.p = ud;
}

static inline void setbvalue(TValue *obj, int b)
{
  obj->tt = LUA_TBOOLEAN;
  obj->value.b = b;
}

#if 0
static inline void setobj(lua_State *L, TValue *obj1, const TValue *obj2)
{
  obj1->value = obj2->value;
  obj1->tt = obj2->tt;
  checkliveness(G(L), obj1);
}
#endif

static inline void setobj(lua_State *L, TValue *obj1, const TValue *obj2)
{
  luaC_writebarriervv(L, &L->gch, obj1, obj2);
  checkliveness(G(L), obj1);
}

static inline void setsvalue(lua_State *L, TValue *obj, TString *str)
{
  luaC_writebarriervo(L, &L->gch, obj, &str->tsv.gch);
  checkliveness(G(L), obj);
}

static inline void setuvalue(lua_State *L, TValue *obj, Udata *ud)
{
  luaC_writebarriervo(L, &L->gch, obj, &ud->uv.gch);
  checkliveness(G(L), obj);
}

static inline void setthvalue(lua_State *L, TValue *obj, lua_State *thr)
{
  luaC_writebarriervo(L, &L->gch, obj, &thr->gch);
  checkliveness(G(L), obj);
}

static inline void setclvalue(lua_State *L, TValue *obj, Closure *cl)
{
  luaC_writebarriervo(L, &L->gch, obj, &cl->gch);
  checkliveness(G(L), obj);
}

static inline void sethvalue(lua_State *L, TValue *obj, Table *t)
{
  luaC_writebarriervo(L, &L->gch, obj, &t->gch);
  checkliveness(G(L), obj);
}

static inline void setptvalue(lua_State *L, TValue *obj, Proto *p)
{
  luaC_writebarriervo(L, &L->gch, obj, &p->gch);
  checkliveness(G(L), obj);
}

/*
** different types of sets, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to table */
#define setobj2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

static inline void setttype(TValue *obj, lu_byte tt)
{
#if HAVE_VALGRIND && 0
  VALGRIND_PRINTF_BACKTRACE("changing type on value %p from %s to %s\n",
    obj, lua_typename(NULL, ttype(obj)), lua_typename(NULL, tt));
#endif
  ttype(obj) = tt;
}


#define getstr(ts)	cast(const char *, ((TString*)ts) + 1)
#define svalue(o)       getstr(rawtsvalue(o))

/* chain list of long jump buffers */
struct lua_longjmp {
  struct lua_longjmp *previous;
  luai_jmpbuf b;
  volatile int status;  /* error code */
  const char *file;
  unsigned int line;
};

#if LUA_OS_DARWIN
# define LUA_ASMNAME(x) x
#else
# define LUA_ASMNAME(x) _##x
#endif

#if LUA_ARCH_X86_64
# define lua_do_setjmp  LUA_ASMNAME(lua_setjmp_amd64)
# define lua_do_longjmp LUA_ASMNAME(lua_longjmp_amd64)
#elif LUA_ARCH_I386
# define lua_do_setjmp  LUA_ASMNAME(lua_setjmp_i386)
# define lua_do_longjmp LUA_ASMNAME(lua_longjmp_i386)
#elif LUA_ARCH_AARCH64
# define lua_do_setjmp  LUA_ASMNAME(lua_setjmp_aarch64)
# define lua_do_longjmp LUA_ASMNAME(lua_longjmp_aarch64)
#endif
#ifdef lua_do_setjmp
extern int lua_do_setjmp(luai_jmpbuf env);
extern void lua_do_longjmp(luai_jmpbuf env, int val)
  LUA_NORETURN;
#else
# define lua_do_setjmp  setjmp
# define lua_do_longjmp longjmp
#endif

/* NOTE: undefined (certainly bad!) behavior will result if the function
 * returns from within the TRY/FINALLY block handling */
#define LUAI_TRY_BLOCK(L) do { \
  struct lua_longjmp lj; \
  lj.status = 0; \
  lj.previous = (L)->errorJmp; \
  (L)->errorJmp = &lj; \
  lj.file = __FILE__; \
  lj.line = __LINE__; \
  if (lua_do_setjmp(lj.b) == 0) {

#define LUAI_TRY_FINALLY(L) \
  }

#define LUAI_TRY_END(L) \
  (L)->errorJmp = lj.previous; \
  if (lj.status) { \
    /* Log the error and re-throw path for diagnostics. */ \
    thrlua_log((L), DCRITICAL, \
      "LUAI_TRY_END: error status=%d in TRY block at %s:%d, " \
      "re-throwing with %s outer handler, L=%p", \
      lj.status, lj.file, lj.line, \
      lj.previous ? "an" : "NO", (void *)(L)); \
    luaD_throw((L), lj.status); \
  } \
} while(0)

#define LUAI_TRY_CATCH(L) \
  } \
  if (lj.status -= lj.status)

#ifdef __cplusplus
extern "C" {
#endif

LUAI_FUNC void luaA_pushobject (lua_State *L, const TValue *o);
LUAI_FUNC Table *luaA_getcurrenv (lua_State *L);


#ifdef __cplusplus
}
#endif

#endif

/* vim:ts=2:sw=2:et:
 */

