/*
** $Id: lstate.h,v 2.24.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

struct lua_longjmp;  /* defined in ldo.c */


/* table of globals */
#define gt(L)	(&L->l_gt)

/* registry */
#define registry(L)	(&G(L)->l_registry)


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_CI_SIZE           8

#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/** the string table holds weak references to string objects
 * to avoid creating many copies of the same string object over and over */

struct stringtable_node {
  TString *str;
  struct stringtable_node *next;
};

// Lock: containing lua_State
typedef struct stringtable {
  struct stringtable_node **hash;
  uint32_t nuse;  /* number of elements */
  uint32_t size;
} stringtable;


/*
** informations about a call
*/
typedef struct CallInfo {
  StkId base;  /* base for this function */
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  const Instruction *savedpc;
  int nresults;  /* expected number of results from this function */
  int tailcalls;  /* number of tail calls lost under this entry */
} CallInfo;



#define curr_func(L)	(clvalue(L->ci->func))
#define ci_func(ci)	(clvalue((ci)->func))
#define f_isLua(ci)	(!ci_func(ci)->c.isC)
#define isLua(ci)	(ttisfunction((ci)->func) && f_isLua(ci))

/** State used for each OS thread.
 * Allocated via malloc when first required, stored in thread-local-storage.
 * The TLS destructor will dispose of the contents.
 * When accessing _OSTLS, the key to the global->tls table (to locate the
 * table of per-OS-thread values) is computed via lua_pushlightuserdata(thr_State).
 * The contents of thr_State are opaque to the collector; do not reference
 * collectable objects from here.
 */
struct thr_State {
  /* so we can find all threads that have run lua */
  struct thr_State *prev, *next;
  /* so that we can stop this thread later */
  pthread_t tid;
  /* so that a suspended thread knows to wake up */
  unsigned int wake;
  /* so that we can avoid deadlock during thread destruction */
  unsigned int dead;
};
typedef struct thr_State thr_State;

/*
** `global state', shared by all threads of this state
*/
struct global_State {
  GCheader gch;

  /** xref bits are safe to read so long as you have locked at least one
   * lua_State */
  uint8_t isxref;
  uint8_t notxref;
  /* if true, the world is stopped */
  lu_byte stopped;

  lua_Alloc alloc;
  void *allocdata;
  int exiting;
  scpt_atomic_t nextheapid;

  TString *memerr;
  TValue l_registry;
  TValue l_globals;
  lua_CFunction panic;  /* to be called in unprotected errors */
  struct Table *mt[NUM_TAGS];  /* metatables for basic types */
  TString *tmname[TM_N];  /* array with tag-method names */

  /* if not nil, encapsulates os-thread local storage. Keys are the udata
   * associated with thr_States */
  TValue ostls;

  struct lua_State *mainthread;
  /** size of additional space to allocate after each lua_State.
   * An application can use lua_get_extra to obtain a pointer to this
   * extra space */
  unsigned int extraspace;
  /** called when each lua_State is allocated */
  void (*on_state_create)(lua_State *L);
  /** called when each lua_State is finalized */
  void (*on_state_finalize)(lua_State *L);
};

LUAI_FUNC thr_State *luaC_get_per_thread(void);

/*
** `per thread' state
*/
struct lua_State {
  GCheader gch;

  struct lua_State *prev, *next;

  scpt_atomic_t heapid;

  lu_byte status;
  int in_gc;

  /** the current value of black */
  lu_byte black;

  /** list of objects with white status */
  GCheader *White;
  /** list of objects with black status */
  GCheader *Black;
  GCheader B0, B1;
  /** list of objects with grey status */
  GCheader Grey;
  /** list of white objects pending finalization */
  GCheader Finalize;
  /** list of Black tables with weak references */
  GCheader Weak;

  pthread_mutex_t lock;
  StkId top;  /* first free slot in the stack */
  StkId base;  /* base of current function */
  global_State *l_G;
  CallInfo *ci;  /* call info for current function */
  const Instruction *savedpc;  /* `savedpc' of current function */
  StkId stack_last;  /* last free slot in the stack */
  StkId stack;  /* stack base */
  CallInfo *end_ci;  /* points after end of ci array*/
  CallInfo *base_ci;  /* array of CallInfo's */
  int stacksize;
  int size_ci;  /* size of array `base_ci' */
  unsigned short nCcalls;  /* number of nested C calls */
  unsigned short baseCcalls;  /* nested C calls when resuming coroutine */
  lu_byte hookmask;
  lu_byte allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  TValue l_gt;  /* table of globals */
  TValue env;  /* temporary place for environments */
  UpVal openupval;  /* list of open upvalues in this stack */
  struct lua_longjmp *errorJmp;  /* current error recover point */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */

  /** string table for thread-local string interning */
  stringtable strt;

  /** temporary buffer for string concatentation */
  Mbuffer buff;

  /* for suspend/resume */
  int (*on_suspend)(lua_State *thr, void *ptr);
  int (*arrange_resume)(lua_State *thr, void *ptr);
  void *suspend_resume_ptr;
  int (*on_resume)(lua_State *thr, void *ptr);
  void *on_resume_ptr;

  /* if not nil, encapsulates thread local storage */
  TValue tls;
};


#define G(L)	(L->l_G)

/* macros to convert a GCheader into a specific value */
#define rawgco2ts(o)	check_exp((o)->tt == LUA_TSTRING, (TString*)(o))
#define gco2ts(o)	(&rawgco2ts(o)->tsv)
#define rawgco2u(o)	check_exp((o)->tt == LUA_TUSERDATA, (Udata*)(o))
#define gco2u(o)	(&rawgco2u(o)->uv)
#define gco2cl(o)	check_exp((o)->tt == LUA_TFUNCTION, (Closure*)(o))
#define gco2h(o)	check_exp((o)->tt == LUA_TTABLE, (Table*)(o))
#define gco2p(o)	check_exp((o)->tt == LUA_TPROTO, (Proto*)(o))
#define gco2uv(o)	check_exp((o)->tt == LUA_TUPVAL, (UpVal*)(o))
#define ngcotouv(o) \
	check_exp((o) == NULL || (o)->tt == LUA_TUPVAL, (UpVal*)(o))
#define gco2th(o)	check_exp((o)->tt == LUA_TTHREAD, (lua_State*)(o))

/* macro to convert any Lua object into a GCObject */
#define obj2gco(v)	(cast(GCObject *, (v)))
#define gch2h(o)	check_exp((o) == NULL || (o)->tt == LUA_TTABLE, (Table*)(o))

LUAI_FUNC lua_State *luaE_newthread (lua_State *L);
LUAI_FUNC void luaE_freethread (global_State *g, lua_State *L1);
LUAI_FUNC void luaE_flush_stringtable(lua_State *L);
LUAI_FUNC lua_State *luaE_newthreadG(global_State *g);

#endif

/* vim:ts=2:sw=2:et:
 */
