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

typedef struct stringtable {
  pthread_mutex_t lock;
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

/* some helpers to maintain and operate on object stacks.
 * These are used to maintain the marking stack, the weak table
 * stack and the root stack.
 * We cache memory for these to avoid churn and fragmentation */
struct gc_stack {
  GCheader **obj;
  unsigned int alloc, items;
};

/* The gc log buffer differs from the gc_stack in that we can't
 * realloc the buffer.  Since we can't predict the total amount
 * of space we'll need for the log buffer, we allocate a minimum
 * of 128 entries (or more if we're logging a larger object).
 * If when we log a new item there is not enough room, we'll
 * allocate a new buffer and chain it together.  The chain is
 * reclaimed at the end of a collection cycle */
struct gc_log_buffer {
  struct gc_log_buffer *next;
  unsigned int items;
  unsigned int alloc;
  GCheader *obj[1];
};

/** State used for each OS thread */
typedef struct thr_State {
  /* kept in a list for collection purposes */
  struct thr_State *next, *prev;
  pthread_mutex_t handshake;

  /* head of thread-local heap */
  GCheader olist;
  unsigned int trace;
  unsigned int snoop;
  unsigned int alloc_color;

  /** temporary buffer for string concatentation */
  Mbuffer buff;
  /** string table for thread-local string interning */
  stringtable strt;

  struct gc_stack snoop_buf;
  struct gc_log_buffer *log_buf;

  struct global_State *g;

} thr_State;

/*
** `global state', shared by all threads of this state
*/
struct global_State {
  GCheader gch;

  pthread_key_t tls_key;
  pthread_cond_t gc_cond;
  pthread_t collector_thread;
  pthread_mutex_t collector_lock;
  GCheader the_heap;
  GCheader to_finalize;
  struct gc_stack
    weak_set,
    mark_set;
  thr_State *all_threads;
  lu_byte black;
  lu_byte white;
  lua_Alloc alloc;
  void *allocdata;
  int exiting;

  TString *memerr;
  TValue l_registry;
  TValue l_globals;
  lua_CFunction panic;  /* to be called in unprotected errors */
  struct Table *mt[NUM_TAGS];  /* metatables for basic types */
  TString *tmname[TM_N];  /* array with tag-method names */

  lu_byte gcstate;  /* state of garbage collector */
  lu_mem GCthreshold;
  lu_mem totalbytes;  /* number of bytes currently allocated */
  lu_mem estimate;  /* an estimate of number of bytes actually in use */
  lu_mem gcdept;  /* how much GC is `behind schedule' */
  int gcpause;  /* size of pause between successive GCs */
  int gcstepmul;  /* GC `granularity' */
  struct lua_State *mainthread;
};

LUAI_FUNC thr_State *luaC_init_pt(global_State *g);
static inline thr_State *getpt(global_State *g)
{
  thr_State *pt = pthread_getspecific(g->tls_key);
  if (pt == NULL) {
    return luaC_init_pt(g);
  }
  return pt;
}



/*
** `per thread' state
*/
struct lua_State {
  GCheader gch;
  lu_byte status;
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
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC lua_State *luaE_newthreadG(global_State *g);

#endif

/* vim:ts=2:sw=2:et:
 */
