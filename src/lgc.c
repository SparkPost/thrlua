/*
** $Id: lgc.c,v 2.38.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "thrlua.h"

#if USING_DRD
# define INLINE /* not inline */
#else
# define INLINE inline
#endif

/* Perform global traces in parallel, as opposed to having just one thread
 * do it. */
static int USE_TRACE_THREADS = 0;

/* Number of trace threads to use.  Start with 8, requires USE_TRACE_THREADS
 * to be set to 1. */
static int NUM_TRACE_THREADS = 8;

/* Non-signal collector logic.  This behavior is still experimental, but is
 * showing tremendous promise. */
static int NON_SIGNAL_COLLECTOR = 0;

#ifdef LUA_OS_LINUX
# define DEF_LUA_SIG_SUSPEND SIGPWR
# define DEF_LUA_SIG_RESUME  SIGXCPU
#elif defined(LUA_OS_DARWIN)
# define DEF_LUA_SIG_SUSPEND SIGINFO
# define DEF_LUA_SIG_RESUME  SIGXCPU
#else
# define DEF_LUA_SIG_SUSPEND SIGUSR1
# define DEF_LUA_SIG_RESUME  SIGUSR2
#endif

static int LUA_SIG_SUSPEND = DEF_LUA_SIG_SUSPEND;
static int LUA_SIG_RESUME  = DEF_LUA_SIG_RESUME;
static pthread_once_t tls_init = PTHREAD_ONCE_INIT;
pthread_key_t lua_tls_key;

/* we use these to track the threads so that we can stop the world */
static pthread_mutex_t all_threads_lock; /* initialized via pthread_once */
static uint32_t num_threads = 0;
static uint32_t parked_threads = 0;
static TAILQ_HEAD(thr_StateList, thr_State)
  all_threads = TAILQ_HEAD_INITIALIZER(all_threads);
static sigset_t suspend_handler_mask;
static struct ck_stack trace_stack;
static pthread_cond_t trace_cond;
static pthread_mutex_t trace_mtx;

/* This lock is used to synchronize threads trying to enter a barrier
 * in the NON_SIGNAL_COLLECTOR case.  This is only used to signal threads
 * trying to block the collector while a global trace is going on.  The reason
 * we don't use this in the general case (and use the in_barrier/intend_to_stop
 * logic) is because this codepath is very hot, and using the rwlock as the 
 * primary method of synchronization would be more expensive.  So we will 
 * use the intend_to_stop/in_barrier method in normal circumstances, but to avoid
 * busy waiting on the intend_to_stop test we will use sleep the threads on 
 * the rwlock. */
static pthread_rwlock_t trace_rwlock;

static void *trace_thread(void *);
static void trace_heap(GCheap *h);
static uint32_t trace_heaps = 0;


#define BLACKBIT    (1<<0)
#define WEAKKEYBIT  (1<<1)
#define WEAKVALBIT  (1<<2)
#define GREYBIT     (1<<3)
#define FINALBIT    (1<<4)
#define FREEDBIT    (1<<7)

static int local_collection(lua_State *L);
static void global_trace(lua_State *L);

static INLINE int is_black(lua_State *L, GCheader *obj)
{
  return ((obj->marked & BLACKBIT) == L->black);
}

static INLINE int is_grey(GCheader *obj)
{
  return obj->marked & GREYBIT;
}

static INLINE int is_finalized(GCheader *obj)
{
  return obj->marked & FINALBIT;
}

static INLINE int is_aggregate(GCheader *obj)
{
  return obj->tt != LUA_TSTRING;
}

static INLINE int is_free(GCheader *obj)
{
  return obj->marked & FREEDBIT;
}

/** defines GCheap_from_stack to convert a stack entry to a GCheap */
CK_STACK_CONTAINER(GCheap, instack, GCheap_from_stack);

/** defines GCheader_from_stack to convert a stack entry to a
 * GCheader */
CK_STACK_CONTAINER(GCheader, instack, GCheader_from_stack);

/** defines GCheader_from_stack_finalize to convert a to finalize
 * stack entry to a GCHeader */
CK_STACK_CONTAINER(GCheader, finalize_instack, GCheader_from_stack_finalize);

static GCheader *pop_obj(ck_stack_t *stack)
{
  ck_stack_entry_t *ent = ck_stack_pop_npsc(stack);
  GCheader *o;
  if (!ent) return NULL;
  o = GCheader_from_stack(ent);
  lua_assert(&o->instack == ent);
  ent->next = NULL;
  return o;
}

static GCheader *pop_finalize(ck_stack_t *stack)
{
  ck_stack_entry_t *ent = ck_stack_pop_npsc(stack);
  GCheader *o;
  if (!ent) return NULL;
  o = GCheader_from_stack_finalize(ent);
  lua_assert(&o->finalize_instack == ent);
  ent->next = NULL;
  return o;
}

static void push_obj(ck_stack_t *stack, GCheader *o)
{
#if DEBUG_ALLOC
  if (o->instack.next) {
    VALGRIND_PRINTF_BACKTRACE(
      "push stack=%p obj=%p ALREADY IN A STACK!\n", stack, o);
  }
#endif
  lua_assert_obj(o->instack.next == NULL, o);
#if DEBUG_ALLOC
  VALGRIND_PRINTF_BACKTRACE("push stack=%p obj=%p\n", stack, o);
#endif
  ck_stack_push_spnc(stack, &o->instack);
}

static void push_finalize(ck_stack_t *stack, GCheader *o)
{
#if DEBUG_ALLOC
  if (o->finalize_instack.next) {
    VALGRIND_PRINTF_BACKTRACE(
      "push stack=%p obj=%p ALREADY IN A STACK!\n", stack, o);
  }
#endif
  lua_assert_obj(o->finalize_instack.next == NULL, o);
#if DEBUG_ALLOC
  VALGRIND_PRINTF_BACKTRACE("push stack=%p obj=%p\n", stack, o);
#endif
  ck_stack_push_spnc(stack, &o->finalize_instack);
}


static void removeentry(Node *n)
{
  lua_assert(ttisnil(gval(n)));
  if (iscollectable(gkey(n)))
    setttype(key2tval(n), LUA_TDEADKEY);  /* dead key; remove it */
}

static INLINE int is_unknown_xref(lua_State *L, GCheader *o)
{
  if ((ck_pr_load_32(&G(L)->isxref) & 3) == 1) {
    return (ck_pr_load_32(&o->xref) & 2) == 2;
  }
  return (ck_pr_load_32(&o->xref) & 2) == 0;
}

static INLINE int is_not_xref(lua_State *L, GCheader *o)
{
  return ck_pr_load_32(&o->xref) == ck_pr_load_32(&G(L)->notxref);
}

static INLINE void set_xref(lua_State *L, GCheader *lval, GCheader *rval,
  int force)
{
  if (lval->owner != rval->owner) {
    if (!force && ck_pr_load_32(&rval->xref) != ck_pr_load_32(&G(L)->isxref)) {
      ck_pr_inc_32(&rval->owner->owner->xref_count);
    }
    ck_pr_store_32(&rval->xref, ck_pr_load_32(&G(L)->isxref));
  } else if (force && is_unknown_xref(L, rval)) {
    ck_pr_store_32(&rval->xref, ck_pr_load_32(&G(L)->notxref));
  }
}

static INLINE void make_grey(lua_State *L, GCheader *obj)
{
  lua_assert_obj(obj->owner == L->heap, obj);
  if ((obj->marked & GREYBIT) == GREYBIT) return;
  obj->marked |= GREYBIT;
  push_obj(&L->heap->grey, obj);
}

static INLINE void make_black(lua_State *L, GCheader *obj)
{
  obj->marked = (obj->marked & ~(GREYBIT|BLACKBIT)) | L->black;
}

static INLINE void mark_object(lua_State *L, GCheader *obj)
{
  register int m;

  if (L->heap != obj->owner) {
    /* external reference */
    ck_pr_store_32(&obj->xref, ck_pr_load_32(&G(L)->isxref));
    return;
  }

  lua_assert_obj(!is_free(obj), obj);
  lua_assert_obj(obj->owner == L->heap, obj);

  m = obj->marked;
  if ((m & GREYBIT) || ((m & BLACKBIT) == L->black)) {
    /** already marked */
    return;
  }

  if (is_aggregate(obj)) {
    make_grey(L, obj);
  } else {
    make_black(L, obj);
  }
}

typedef void (*objfunc_t)(lua_State *, GCheader *, GCheader *);
static void traverse_object(lua_State *L, GCheader *o, objfunc_t objfunc);

static INLINE void traverse_obj(lua_State *L, GCheader *obj, GCheader *rval,
  objfunc_t objfunc)
{
  objfunc(L, obj, rval);
}

static INLINE void traverse_value(lua_State *L, GCheader *obj, TValue *val,
  objfunc_t objfunc)
{
  checkconsistency(val);
  if (iscollectable(val)) {
    traverse_obj(L, obj, gcvalue(val), objfunc);
  }
}

/* NOTE: This is the alternate logic for stopping threads during a 
 * global trace. */
/* called during a global trace; when it returns, all mutators will be in
 * a safe state that blocks them from mutating state */
static INLINE void block_mutators(lua_State *L)
{
  uint32_t pending;
  thr_State *pt;

  /* For the non-signal thread stoppage, take a write lock.  Important that
   * we do this before setting intend to stop, as we acquire the write
   * lock after the test for intend_to_stop.  This doesn't actually mean
   * we are yet safe to start the global trace, as when mutators are blocked
   * the in_barrier test is the way we know somebody is writing.  This is done
   * to avoid extra system calls on this lock in the case when the global
   * trace is not running.  When we release the lock, that allows any threads
   * blocking on the lock to acquire a read lock.  They will immediately
   * release the lock and ensure intend_to_stop is set to 0 _after_ they 
   * set their in_barirer flag to 1. */
  if (NON_SIGNAL_COLLECTOR) {
    pthread_rwlock_wrlock(&trace_rwlock);
  }

  /* advertise our intent to stop everyone; this prevents any mutators
   * from returning from their respective barriers */
  ck_pr_store_32(&G(L)->intend_to_stop, 1);
  ck_pr_fence_memory();

  /* don't leave until we know that all threads are outside a barrier. 
   * No more threads will enter a barrier after this point because 
   * intend_to_stop is set to 1. */
  while (1) {
    pending = 0;
  
    TAILQ_FOREACH(pt, &all_threads, threads) {
      if (pt->dead) {
        continue;
      }
      if (ck_pr_load_32(&pt->in_barrier) == 1) {
        pending++;
      }
    }

    if (!pending) {
      return;
    }

    ck_pr_stall();
  }
}

static INLINE void unblock_mutators(lua_State *L)
{
  ck_pr_store_32(&G(L)->intend_to_stop, 0);
  ck_pr_fence_memory();

  /* For the non-signal collector, release the write lock.  This tells
   * any threads trying to block the collector that they can now wake
   * up and try to enter their write barrier. */
  if (NON_SIGNAL_COLLECTOR) {
    pthread_rwlock_unlock(&trace_rwlock);
  }
}

/* Per-thread collector block recursion counter.  As it is per-thread we
 * don't need to use any threadsafe operators to change or read it */
static pthread_key_t collector_block_recursion_key;

static void free_recursion(void *tofree) {
  free(tofree);
}

static int *get_recursion(void) {
  int *recursion;
  if ((recursion = pthread_getspecific(collector_block_recursion_key)) == NULL) {
    recursion = calloc(1, sizeof(*recursion));
    pthread_setspecific(collector_block_recursion_key, recursion);
  }
  return recursion;
}

static INLINE void block_collector(lua_State *L, thr_State *pt)
{
  int *recursion = get_recursion();

  (*recursion)++;
  if (*recursion > 1) {
    /* Already blocked, do nothing */
    return;
  }

  for (;;) {
    while (ck_pr_load_32(&G(L)->intend_to_stop) == 1) {
      if (!NON_SIGNAL_COLLECTOR) {
        /* we will get suspended momentarily */
        ck_pr_stall();
        ck_pr_fence_memory();
      } else {
        /* We synchronize with the guy trying to trace using the rwlock.  
         * This replaces the 'stall' loop we were doing earlier which
         * can cause a lot of context switching for systems with
         * lots of threads. */
        while (pthread_rwlock_rdlock(&trace_rwlock) == EAGAIN) ; 
        /* Now the global trace is done.  We are good to unlock and
         * proceed.  We will double check that we are safely in our
         * barrier below. */
        pthread_rwlock_unlock(&trace_rwlock);

        /* NOTE: This logic used to also act as a trace thread, but
         * that's been removed because when there is no tracing to do
         * it busy waits until the global trace is done.  That extra
         * context switching actually slows down the system more than
         * necessary, which is why we are usign the rwlock strategy
         * intead. */
      }
    }
    /* tell a possible collector that we're in a write barrier */
    ck_pr_store_32(&pt->in_barrier, 1);
    ck_pr_fence_memory();
    if (ck_pr_load_32(&G(L)->intend_to_stop) == 0) {
      return;
    }
    /* Woops, a global trace started after we set the barrier flag,
     * clear it out and loop again. */
    ck_pr_store_32(&pt->in_barrier, 0);
    ck_pr_fence_memory();
  }
}

static INLINE void unblock_collector(lua_State *L, thr_State *pt)
{
  int *recursion = get_recursion();

  (*recursion)--;
  if (*recursion != 0) {
    /* Another block in play further up the stack, do nothing */
    return;
  }

  ck_pr_store_32(&pt->in_barrier, 0);
  ck_pr_fence_memory();
}

void luaC_blockcollector(lua_State *L) {
  thr_State *pt = luaC_get_per_thread(L);

  block_collector(L, pt);
}

void luaC_unblockcollector(lua_State *L) {
  thr_State *pt = luaC_get_per_thread(L);

  unblock_collector(L, pt);
}

# define GET_PT_FOR_NON_SIGNAL_COLLECTOR() \
  thr_State *pt = luaC_get_per_thread(L)
# define BLOCK_COLLECTOR() do { \
  if (NON_SIGNAL_COLLECTOR) {  \
    block_collector(L, pt); \
  } \
} while(0)
# define UNBLOCK_COLLECTOR() do {\
  if (NON_SIGNAL_COLLECTOR) { \
    unblock_collector(L, pt); \
  } \
} while (0)

void luaC_writebarrierxmove(lua_State *L, TValue **lhs,
                            const TValue *rhs, int num) {
  int i;
  thr_State *pt = luaC_get_per_thread(L);

  // The write barrier will block again, but recursion on collector
  // blocks is allowed
  block_collector(L, pt);
  for (i = 0; i < num; i++) {
    setobj2s(L, (*lhs)++, rhs + i);
  }
  unblock_collector(L, pt);
}

void luaC_writebarrierov(lua_State *L, GCheader *object,
  GCheader **lvalue, const TValue *rvalue)
{
  GET_PT_FOR_NON_SIGNAL_COLLECTOR();
  GCheader *ro = gcvalue(rvalue);

  lua_assert(ro != NULL);
  checkconsistency(rvalue);

  BLOCK_COLLECTOR();
  mark_object(L, ro);
  set_xref(L, object, ro, 0);

  ck_pr_store_ptr(lvalue, ro);
  UNBLOCK_COLLECTOR();
}

void luaC_writebarriervo(lua_State *L, GCheader *object,
  TValue *lvalue, GCheader *rvalue)
{
  thr_State *pt = luaC_get_per_thread(L);

  block_collector(L, pt);
  set_xref(L, object, rvalue, 0);
  mark_object(L, rvalue);

  lvalue->value.gc = rvalue;
  /* RACE: a global trace can trigger here and catch us pants-down
   * wrt ->tt != value->tt; so this section must be protected by blocking
   * the collector */
  lvalue->tt = rvalue->tt;
  unblock_collector(L, pt);
}


void luaC_writebarriervv(lua_State *L, GCheader *object,
  TValue *lvalue, const TValue *rvalue)
{
  thr_State *pt = luaC_get_per_thread(L);

  block_collector(L, pt);
  if (iscollectable(rvalue)) {
    GCheader *ro = gcvalue(rvalue);
    set_xref(L, object, ro, 0);
    mark_object(L, ro);
  }

  lvalue->value = rvalue->value;
  /* RACE: a global trace can trigger here and catch us pants-down
   * wrt ->tt != value->tt; so this section must be protected by blocking
   * the collector */
  lvalue->tt = rvalue->tt;
  unblock_collector(L, pt);
}

void luaC_writebarrier(lua_State *L, GCheader *object,
  GCheader **lvalue, GCheader *rvalue)
{
  GET_PT_FOR_NON_SIGNAL_COLLECTOR();

  BLOCK_COLLECTOR();
  if (rvalue) {
    set_xref(L, object, rvalue, 0);
    mark_object(L, rvalue);
  }

  ck_pr_store_ptr(lvalue, rvalue);
  UNBLOCK_COLLECTOR();
}

void luaC_writebarrierstr(lua_State *L, unsigned int h,
                          struct stringtable_node *n) {
  stringtable *tb = &L->strt;
  thr_State *pt = luaC_get_per_thread(L);
  struct stringtable_node **oldhash = NULL;
  int oldsize = 0;

  h = lmod(h, tb->size);
  block_collector(L, pt);
  n->next = tb->hash[h];  /* chain new entry */
  tb->hash[h] = n;
  tb->nuse++;
  oldhash = tb->hash;
  oldsize = tb->size;
  unblock_collector(L, pt);

  /* Can't do allocations or frees while blocking the collector */
  if (tb->nuse > cast(uint32_t, tb->size) && tb->size <= MAX_INT/2) {
    int newsize = tb->size * 2;
    struct stringtable_node **newhash = 
      luaM_realloc(L, LUA_MEM_STRING_TABLE, NULL, 0,
                   newsize * sizeof(struct stringtable_node *));

    /* luaS_resize does no allocations, but it needs the collector blocked */
    block_collector(L, pt);
    luaS_resize(L, tb, tb->size*2, newhash);  /* too crowded */
    unblock_collector(L, pt); 
    if (oldhash && oldsize) {
      luaM_realloc(L, LUA_MEM_STRING_TABLE, oldhash,
                   oldsize * sizeof(struct stringtable_node *), 0);
    }
  }
}

/* broken out into a function to allow us to suppress it from drd.
 * If the world is stopped, we don't need (and in fact, MUST NOT,
 * due to risk of deadlock) take locks on items during collection cycles.
 */
static INLINE int is_world_stopped(lua_State *L)
{
  return ck_pr_load_32(&G(L)->stopped);
}


/* traverse object must be async-signal safe when G(L)->stopped is true */
static void traverse_object(lua_State *L, GCheader *o, objfunc_t objfunc)
{
  int i;

  switch (o->tt) {
    case LUA_TSTRING:
      /* has no contents */
      break;

    case LUA_TUSERDATA:
      {
        Udata *ud = rawgco2u(o);
        if (ud->uv.metatable) {
          traverse_obj(L, o, ud->uv.metatable, objfunc);
        }
        if (ud->uv.env) {
          traverse_obj(L, o, ud->uv.env, objfunc);
        }
        if (ud->uv.otherref) {
          traverse_obj(L, o, ud->uv.otherref, objfunc);
        }
        break;
      }
    case LUA_TUPVAL:
      {
        UpVal *uv = gco2uv(o);

        if (!uv->v) {
          /* Not done being setup yet, skip */
          return;
        }
        traverse_value(L, o, uv->v, objfunc);
        break;
      }
    case LUA_TTABLE:
      {
        Table *h = gco2h(o);
        int weakkey = 0, weakvalue = 0;
        const TValue *mode;

        if (!ck_pr_load_uint(&h->initialized)) {
          /* Not done being setup yet, skip */
          return;
        }
        if (!is_world_stopped(L)) luaH_rdlock(L, h);
        if (h->metatable) {
          traverse_obj(L, o, h->metatable, objfunc);
        }
        o->marked &= ~(WEAKKEYBIT|WEAKVALBIT);
        mode = gfasttm(G(L), gch2h(h->metatable), TM_MODE);
        if (mode && ttisstring(mode)) {
          weakkey = (strchr(svalue(mode), 'k') != NULL);
          weakvalue = (strchr(svalue(mode), 'v') != NULL);
          if (weakkey) o->marked |= WEAKKEYBIT;
          if (weakvalue) o->marked |= WEAKVALBIT;
        }
        if (!weakvalue) {
          i = h->sizearray;
          while (i--) {
            traverse_value(L, o, &h->array[i], objfunc);
          }
        }
        i = sizenode(h);
        while (i--) {
          Node *n = gnode(h, i);

          lua_assert(ttype(gkey(n)) != LUA_TDEADKEY || ttisnil(gval(n)));

          if (ttisnil(gval(n))) {
            if (!is_world_stopped(L)) removeentry(n);
          } else {
            lua_assert(!ttisnil(gkey(n)));
            if (!weakkey) {
              traverse_value(L, o, key2tval(n), objfunc);
            }
            if (!weakvalue) {
              traverse_value(L, o, gval(n), objfunc);
            }
          }
        }
        if (!is_world_stopped(L)) luaH_rdunlock(L, h);
        break;
      }

    case LUA_TFUNCTION:
      {
        Closure *cl = gco2cl(o);

        if (cl->c.isC) {
          if (!cl->c.env) {
            /* Not done setting up, return */
            return;
          }
          traverse_obj(L, o, cl->c.env, objfunc);
          for (i = 0; i < cl->c.nupvalues; i++) {
            traverse_value(L, o, &cl->c.upvalue[i], objfunc);
          }
        } else {
          if (!cl->l.env || !cl->l.p) {
            /* Not done being setup yet, skip */
            return;
          }
          lua_assert(cl->l.nupvalues == cl->l.p->nups);
          traverse_obj(L, o, &cl->l.p->gch, objfunc);
          traverse_obj(L, o, cl->l.env, objfunc);
          for (i = 0; i < cl->l.nupvalues; i++) {
            if (!cl->l.upvals[i]) {
              /* Not done setting up, continue */
              continue;
            }
            traverse_obj(L, o, &cl->l.upvals[i]->gch, objfunc);
          }
        }
        break;
      }
    case LUA_TTHREAD:
      {
        lua_State *th = gco2th(o);
        StkId sk, lim;
        UpVal *uv;
        CallInfo *ci;
        int i;
        struct stringtable_node *n;

        if (!th->stack || !th->base_ci) {
          /* Not setup yet, skip for now */
          return;
        }
        if (!is_world_stopped(L)) lua_lock(th);
        traverse_value(L, o, &th->l_gt, objfunc);
        traverse_value(L, o, &th->env, objfunc);
        traverse_value(L, o, &th->tls, objfunc);
        /* the corresponding global state */
        lua_assert(G(L) == th->l_G);
        traverse_obj(L, o, &th->l_G->gch, objfunc);

        lim = th->top;
        for (ci = th->base_ci; ci <= th->ci; ci++) {
          lua_assert(ci->top <= th->stack_last);
          if (lim < ci->top) {
            lim = ci->top;
          }
        }
        for (sk = th->stack; sk < th->top; sk++) {
          traverse_value(L, o, sk, objfunc);
        }
        if (th == L && !is_world_stopped(L)) {
          for (; sk <= lim; sk++) {
            setnilvalue(sk);
          }
        }
        /* open upvalues also */
        for (uv = th->openupval.u.l.next;
            uv != &th->openupval; uv = uv->u.l.next) {
          traverse_obj(L, o, (GCheader*)uv, objfunc);
        }
        /* stringtable */
        for (i = 0; i < th->strt.size; i++) {
          for (n = th->strt.hash[i]; n; n = n->next) {
            traverse_obj(L, o, &n->str->tsv.gch, objfunc);
          }
        }
        if (!is_world_stopped(L)) lua_unlock(th);
        break;
      }
    case LUA_TPROTO:
      {
        Proto *f = gco2p(o);

        if (f->source) {
          traverse_obj(L, o, &f->source->tsv.gch, objfunc);
        }
        for (i = 0; i < f->sizek; i++) {
          traverse_value(L, o, &f->k[i], objfunc);
        }
        for (i = 0; i < f->sizeupvalues; i++) {
          if (f->upvalues[i]) {
            traverse_obj(L, o, f->upvalues[i], objfunc);
          }
        }
        for (i = 0; i < f->sizep; i++) {
          if (f->p[i]) {
            traverse_obj(L, o, &f->p[i]->gch, objfunc);
          }
        }
        for (i = 0; i < f->sizelocvars; i++) {
          if (f->locvars[i].varname) {
            traverse_obj(L, o, f->locvars[i].varname, objfunc);
          }
        }
        break;
      }
    case LUA_TGLOBAL:
      traverse_obj(L, o, &G(L)->memerr->tsv.gch, objfunc);
      for (i = 0; i < NUM_TAGS; i++) {
        if (G(L)->mt[i]) {
          traverse_obj(L, o, &G(L)->mt[i]->gch, objfunc);
        }
      }
      for (i = 0; i < TM_N; i++) {
        if (G(L)->tmname[i]) {
          traverse_obj(L, o, &G(L)->tmname[i]->tsv.gch, objfunc);
        }
      }
      traverse_value(L, o, &G(L)->l_registry, objfunc);
      traverse_value(L, o, &G(L)->ostls, objfunc);
      traverse_value(L, o, &G(L)->l_globals, objfunc);
      traverse_obj(L, o, &G(L)->mainthread->gch, objfunc);
      break;

    default:
#if HAVE_VALGRIND
      VALGRIND_PRINTF_BACKTRACE("marking %s not implemented o=%p\n",
          lua_typename(L, o->tt), o);
#endif
      fprintf(stderr, "marking for tt=%d is not implemented\n", o->tt);
      abort();
  }
}

static void grey_object(lua_State *L, GCheader *lval, GCheader *rval)
{
  mark_object(L, rval);
}

static void blacken_object(lua_State *L, GCheader *o)
{
  lua_assert_obj(!is_free(o), o);
  lua_assert_obj(o->owner == L->heap, o);
  lua_assert_obj(is_grey(o) || is_black(L, o), o);
  make_black(L, o);

  traverse_object(L, o, grey_object);

  if (o->tt == LUA_TTABLE) {
    Table *h = gco2h(o);

    if (o->marked & (WEAKVALBIT|WEAKKEYBIT)) {
      /* remember that it had weak bits, as we will need to fixup the table
       * contents if we collect them */
      push_obj(&L->heap->weak, o);
    }
  }
}

static void propagate(lua_State *L)
{
  GCheader *o;

  while ((o = pop_obj(&L->heap->grey)) != NULL) {
    blacken_object(L, o);
  }
}

static INLINE void lock_all_threads(void)
{
  int r = pthread_mutex_lock(&all_threads_lock);
  if (r) {
    fprintf(stderr, "LOCK(all_threads): %d %s\n", r, strerror(r));
    abort();
  }
}

static INLINE int try_lock_all_threads(void)
{
  int r = pthread_mutex_trylock(&all_threads_lock);
  switch (r) {
    case 0:
      return 1;
    case EBUSY:
      return 0;
    default:
      fprintf(stderr, "LOCK(all_threads): %d %s\n", r, strerror(r));
      abort();
  }
}


static INLINE void unlock_all_threads(void)
{
  int r = pthread_mutex_unlock(&all_threads_lock);
  if (r) {
    fprintf(stderr, "UNLOCK(all_threads): %d %s\n", r, strerror(r));
    abort();
  }
}

/* given the nature of stopping the world, we MUST ensure that we
 * only use async-safe functions in this handler; printf very likely
 * to lead to a deadlock */
static void thread_suspend_requested(int sig)
{
  int save = errno;
  thr_State *pt;
  int r;

  pt = luaC_get_per_thread_raw();

  /* there are some edges where we may no longer have a valid pt; for instance,
   * we may be exiting this thread and have already destroyed the pt. In this
   * case, we actually have nothing to contribute any more, so we simply
   * return from the handler */
  if (!pt) {
    errno = save;
    return;
  }

  ck_pr_store_uint(&pt->wake, 0);
  ck_pr_inc_32(&parked_threads);

  /* signal the thread that is doing the global collection that we
   * are considered stopped */
//  VALGRIND_PRINTF_BACKTRACE("suspend requested\n");
  while (!ck_pr_load_uint(&pt->wake)) {
    sigsuspend(&suspend_handler_mask);
  }

  /* before we can safely resume, all resume threads MUST check back
   * in with the global collector, otherwise another thread may attempt
   * to start a new global sweep while some of the threads are still
   * waking up; this can result in some weird re-entrancy issues that
   * cause threads to get stuck */

  ck_pr_dec_32(&parked_threads);
//  VALGRIND_PRINTF_BACKTRACE("resumed!\n");

  errno = save;
}

/* MUST be async signal safe */
static void thread_resume_requested(int sig)
{
  thr_State *pt;
  int save = errno;

  pt = luaC_get_per_thread_raw();
  if (pt != NULL) {
    ck_pr_store_uint(&pt->wake, 1);
  }

  errno = save;
}

static void prune_dead_thread_states(void)
{
  thr_State *pt, *pttmp;

  if (!try_lock_all_threads()) {
    return;
  }
  TAILQ_FOREACH_SAFE(pt, &all_threads, threads, pttmp) {
    if (pt->dead) {
      TAILQ_REMOVE(&all_threads, pt, threads);

      free(pt);
      continue;
    }
  }
  unlock_all_threads();
}

/* MUST be async signal safe */
static int signal_all_threads(lua_State *L, int sig)
{
  thr_State *pt;
  pthread_t me = pthread_self();
  int nthreads = 0;
  int r;

  ck_pr_store_32(&G(L)->intend_to_stop, sig == LUA_SIG_SUSPEND ? 1 : 0);
  ck_pr_fence_memory();

  TAILQ_FOREACH(pt, &all_threads, threads) {
    if (pthread_equal(me, pt->tid)) {
      /* can't stop myself */
      continue;
    }
    if (pt->dead) {
      continue;
    }
    if (sig == LUA_SIG_SUSPEND) {
      /* wait for thread to leave its barrier */
      while (ck_pr_load_32(&pt->in_barrier) == 1) {
        ck_pr_stall();
        ck_pr_fence_memory();
      }
    }

    r = pthread_kill(pt->tid, sig);
    if (r == 0) {
      nthreads++;
    } else if (r != ESRCH) {
      fprintf(stderr, "signal_all_threads: pthread_kill %s\n", strerror(r));
      abort();
    }
  }

  return nthreads;
}

/* MUST be async signal safe */
static void stop_all_threads(lua_State *L)
{
  signal_all_threads(L, LUA_SIG_SUSPEND);
  while (ck_pr_load_32(&parked_threads) < ck_pr_load_32(&num_threads) - 1) {
    ck_pr_stall();
  }
}

/* caller MUST hold all_threads_lock */
/* MUST be async signal safe */
static void resume_threads(lua_State *L)
{
  signal_all_threads(L, LUA_SIG_RESUME);
  while (ck_pr_load_32(&parked_threads)) {
    ck_pr_stall();
  }
}

static void thread_exited(void *p)
{
  thr_State *thr = p;

  ck_pr_dec_32(&num_threads);

  /* POSIX states that we are only called when p is non-NULL */
  lua_assert(p != NULL);

  if (try_lock_all_threads()) {
    TAILQ_REMOVE(&all_threads, thr, threads);
    unlock_all_threads();
    free(thr);
  } else {
    ck_pr_store_uint(&thr->dead, 1);
  }
}

static void allowed_sigs(sigset_t *set)
{
  sigdelset(set, SIGSEGV);
  sigdelset(set, SIGINT);
  sigdelset(set, SIGQUIT);
  sigdelset(set, SIGABRT);
  sigdelset(set, SIGTERM);
#ifdef SIGBUS
  sigdelset(set, SIGBUS);
#endif
}

static void free_last_global_bits(void)
{
  thr_State *pt;

  pt = luaC_get_per_thread_raw();
  if (pt) {
    thread_exited(pt);
  }
}

static int is_bool_env_true(const char *env) {
  if (env &&
      (!strcasecmp(env, "true") || !strcasecmp(env, "on") ||
       !strcasecmp(env, "1") || !strcasecmp(env, "enabled") ||
       !strcasecmp(env, "yes"))) {
    return 1;
  }

  return 0;
}

static void make_tls_key(void)
{
  struct sigaction act;
  pthread_mutexattr_t m;
  pthread_attr_t ta;
  int i;
  const char *use_trace_threads = getenv("LUA_USE_TRACE_THREADS");
  const char *num_trace_threads = getenv("LUA_NUM_TRACE_THREADS");
  const char *non_signal_collector = getenv("LUA_NON_SIGNAL_COLLECTOR");

  if (use_trace_threads && is_bool_env_true(use_trace_threads)) {
    USE_TRACE_THREADS = 1;
  }

  if (USE_TRACE_THREADS && num_trace_threads && isdigit(*num_trace_threads)) {
    NUM_TRACE_THREADS = atoi(num_trace_threads);
  }

  if (non_signal_collector && is_bool_env_true(non_signal_collector)) {
    NON_SIGNAL_COLLECTOR = 1;
  }


  atexit(free_last_global_bits);

  pthread_mutexattr_init(&m);
#if 0
  pthread_mutexattr_settype(&m, PTHREAD_MUTEX_ERRORCHECK);
#endif
  pthread_mutex_init(&all_threads_lock, &m);
  pthread_mutexattr_destroy(&m);
  pthread_rwlock_init(&trace_rwlock, NULL);

  if (USE_TRACE_THREADS && NUM_TRACE_THREADS) {
    /* spin up GC tracing threads */
    ck_stack_init(&trace_stack);
    pthread_cond_init(&trace_cond, NULL);
    pthread_mutex_init(&trace_mtx, NULL);
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
    for (i = 0; i < NUM_TRACE_THREADS; i++) {
      pthread_t t;
      pthread_create(&t, &ta, trace_thread, NULL);
    }
    pthread_attr_destroy(&ta);
  }

  memset(&act, 0, sizeof(act));
  act.sa_flags = SA_RESTART;
  sigfillset(&act.sa_mask);
  allowed_sigs(&act.sa_mask);

  act.sa_handler = thread_suspend_requested;
  if (sigaction(LUA_SIG_SUSPEND, &act, NULL) != 0) {
    fprintf(stderr,
      "failed to prepare thread suspend handler for signal %d: %s\n",
      LUA_SIG_SUSPEND,
      strerror(errno));
    abort();
  }

  act.sa_handler = thread_resume_requested;
  if (sigaction(LUA_SIG_RESUME, &act, NULL) != 0) {
    fprintf(stderr,
      "failed to prepare thread resume handler for signal %d: %s\n",
      LUA_SIG_RESUME,
      strerror(errno));
    abort();
  }

  sigfillset(&suspend_handler_mask);
  allowed_sigs(&suspend_handler_mask);
  sigdelset(&suspend_handler_mask, LUA_SIG_RESUME);

  pthread_key_create(&lua_tls_key, thread_exited);
  pthread_key_create(&collector_block_recursion_key, free_recursion);
}

thr_State *luaC_get_per_thread_(void)
{
  thr_State *pt;

  pthread_once(&tls_init, make_tls_key);

  pt = calloc(1, sizeof(*pt));
  pthread_setspecific(lua_tls_key, pt);
  pt->tid = pthread_self();

  lock_all_threads();
  TAILQ_INSERT_HEAD(&all_threads, pt, threads);
  unlock_all_threads();

  ck_pr_inc_32(&num_threads);
  return pt;
}

static inline void sum_usage(struct lua_memtype_alloc_info *dest,
  const struct lua_memtype_alloc_info *src)
{
  dest->bytes += src->bytes;
  dest->allocs += src->allocs;
}

int64_t luaC_count(lua_State *L)
{
  uint32_t vers;
  GCheap *h;
  int64_t tot = 0;

  lock_all_threads();
  TAILQ_FOREACH(h, &G(L)->all_heaps, heaps) {
    int64_t v;
    do {
      vers = ck_sequence_read_begin(&h->owner->memlock);
      v = h->owner->mem.bytes;
    } while (ck_sequence_read_retry(&h->owner->memlock, vers));
    tot += v;
  }
  unlock_all_threads();
  return tot;
}

void lua_mem_get_usage(lua_State *L, struct lua_mem_usage_data *data,
  enum lua_mem_info_scope scope)
{
  uint32_t vers;
  GCheap *h;
  int i;

  memset(data, 0, sizeof(*data));

  if (scope == LUA_MEM_SCOPE_LOCAL) {
    do {
      vers = ck_sequence_read_begin(&L->memlock);
      memcpy(&data->global, &L->mem, sizeof(L->mem));
      memcpy(&data->bytype, L->memtype, sizeof(L->memtype));
    } while (ck_sequence_read_retry(&L->memlock, vers));

    return;
  }

  lock_all_threads();
  TAILQ_FOREACH(h, &G(L)->all_heaps, heaps) {
    struct lua_memtype_alloc_info mem;
    struct lua_memtype_alloc_info memtype[LUA_MEM__MAX];

    do {
      vers = ck_sequence_read_begin(&h->owner->memlock);
      memcpy(&mem, &h->owner->mem, sizeof(mem));
      memcpy(memtype, h->owner->memtype, sizeof(memtype));
    } while (ck_sequence_read_retry(&h->owner->memlock, vers));

    sum_usage(&data->global, &mem);
    for (i = 0; i < LUA_MEM__MAX; i++) {
      sum_usage(&data->bytype[i], &memtype[i]);
    }
  }
  unlock_all_threads();
}

static void init_heap(lua_State *L, GCheap *h)
{
  TAILQ_INIT(&h->objects);
  ck_stack_init(&h->grey);
  ck_stack_init(&h->weak);
  ck_stack_init(&h->to_free);
  ck_stack_init(&h->to_finalize);
  h->owner = L;

  lock_all_threads();
  TAILQ_INSERT_HEAD(&G(L)->all_heaps, h, heaps);
  unlock_all_threads();
}

static GCheap *new_heap(lua_State *L)
{
  GCheap *h = calloc(1, sizeof(*h));

  init_heap(L, h);
  return h;
}


global_State *luaC_newglobal(struct lua_StateParams *p)
{
  global_State *g;
  pthread_condattr_t cattr;
  pthread_mutexattr_t mattr;
  lua_State *L;

  /* this performs once-init stuff */
  pthread_once(&tls_init, make_tls_key);

  g = p->allocfunc(p->allocdata, LUA_MEM_GLOBAL_STATE, NULL, 0,
        sizeof(*g) + sizeof(lua_State) + p->extraspace);
  if (!g) {
    return NULL;
  }
  memset(g, 0, sizeof(*g));
  g->gch.tt = LUA_TGLOBAL;
  g->alloc = p->allocfunc;
  g->gcstepmul = LUAI_GCMUL;
  g->gcpause = LUAI_GCPAUSE;
  g->global_trace_thresh = 10;
  g->global_trace_xref_thresh = 300;
  g->allocdata = p->allocdata;
  g->extraspace = p->extraspace;
  g->on_state_create = p->on_state_create;
  g->on_state_finalize = p->on_state_finalize;
  g->loadfunc = p->loadfunc;
  g->isxref = 1; /* g->notxref is implicitly set to 0 by memset above */

  L = (lua_State*)(g + 1);
  g->mainthread = L;
  memset(L, 0, sizeof(*L) + g->extraspace);
  L->heap = &g->gheap;
  L->gch.tt = LUA_TTHREAD;
  L->gch.owner = L->heap;
  G(L) = g;
  ck_sequence_init(&L->memlock);

  TAILQ_INIT(&g->all_heaps);

  init_heap(L, L->heap);
  TAILQ_INSERT_HEAD(&L->heap->objects, &g->gch, allocd);
  TAILQ_INSERT_HEAD(&L->heap->objects, &L->gch, allocd);
  g->gch.owner = L->heap;

  return g;
}

static GCheader *new_obj(lua_State *L, enum lua_obj_type tt,
  enum lua_memtype objtype, size_t size)
{
  GCheader *o;
  thr_State *pt = luaC_get_per_thread(L);

  o = luaM_realloc(L, objtype, NULL, 0, size);
  memset(o, 0, size);
  o->owner = L->heap;
  o->tt = tt;
  o->marked = !L->black;
  o->xref = ck_pr_load_32(&G(L)->notxref);
  make_grey(L, o);
  /* The collector can be walking our heap, which isn't safe.  So block it
   * while we're adding to it */
  block_collector(L, pt);
  TAILQ_INSERT_HEAD(&L->heap->objects, o, allocd);
  unblock_collector(L, pt);

  return o;
}

void *luaC_newobj(lua_State *L, enum lua_obj_type tt)
{
  GCheader *o;
  thr_State *pt = luaC_get_per_thread(L);

  switch (tt) {
#define NEWIMPL(a, b, objtype) \
    case a: \
      lua_lock(L); \
      o = new_obj(L, tt, objtype, sizeof(b)); \
      lua_unlock(L); \
      break
    NEWIMPL(LUA_TUPVAL, UpVal, LUA_MEM_UPVAL);
    NEWIMPL(LUA_TPROTO, Proto, LUA_MEM_PROTO);
    NEWIMPL(LUA_TTABLE, Table, LUA_MEM_TABLE);
    case LUA_TTHREAD:
    {
      lua_State *n;

      lua_lock(L);
      n = luaM_realloc(L, LUA_MEM_THREAD, NULL, 0,
          sizeof(lua_State) + G(L)->extraspace);
      memset(n, 0, sizeof(lua_State) + G(L)->extraspace);
      n->gch.tt = LUA_TTHREAD;
      G(n) = G(L);
      /* threads own themselves. their creators hold a ref */
      n->heap = new_heap(n);
      n->gch.owner = n->heap;
      ck_sequence_init(&n->memlock);
      block_collector(L, pt);
      TAILQ_INSERT_HEAD(&n->heap->objects, &n->gch, allocd);
      unblock_collector(L, pt);
      make_grey(n, &n->gch);
      o = &n->gch;
      o->marked = !n->black;

      n->gch.xref = ck_pr_load_32(&G(L)->notxref);

      lua_unlock(L);
      break;
    }
    case LUA_TGLOBAL:
    default:
      lua_assert(0);
      abort();
      return NULL;
  }
#if HAVE_VALGRIND && DEBUG_ALLOC
  VALGRIND_PRINTF_BACKTRACE("new %s at %p\n",
    lua_typename(NULL, o->tt), o);
#endif
  return o;
}

void *luaC_newobjv(lua_State *L, enum lua_obj_type tt, size_t size)
{
  GCheader *o = NULL;

  switch (tt) {
#undef NEWIMPL
#define NEWIMPL(a, b, objtype) \
    case a: \
      lua_lock(L); \
      o = new_obj(L, tt, objtype, size); \
      lua_unlock(L); \
      break
    NEWIMPL(LUA_TFUNCTION, Closure, LUA_MEM_FUNCTION);
    NEWIMPL(LUA_TSTRING, TString, LUA_MEM_STRING);
    NEWIMPL(LUA_TUSERDATA, Udata, LUA_MEM_USERDATA);
    default:
      lua_assert(0);
      abort();
      return NULL;
  }
#if HAVE_VALGRIND && DEBUG_ALLOC
  VALGRIND_PRINTF_BACKTRACE("new %s of size %d at %p\n",
    lua_typename(NULL, tt), size, o);
#endif
  return o;
}

void luaC_inherit_thread(lua_State *L, lua_State *th)
{
  int i;
  GCheader *steal, *tmp;

  if (th->heap == NULL) {
    // already done
    return;
  }

  /* when a thread is reclaimed, the executing thread
   * needs to steal its contents */
  lock_all_threads();

  ck_sequence_write_begin(&th->memlock);
  ck_sequence_write_begin(&L->memlock);
  L->gcestimate += th->gcestimate;
  sum_usage(&L->mem, &th->mem);
  for (i = 0; i < LUA_MEM__MAX; i++) {
    sum_usage(&L->memtype[i], &th->memtype[i]);
  }

  ck_sequence_write_end(&L->memlock);
  ck_sequence_write_end(&th->memlock);
  luaE_flush_stringtable(th);

  TAILQ_FOREACH_SAFE(steal, &th->heap->objects, allocd, tmp) {
    TAILQ_REMOVE(&th->heap->objects, steal, allocd);

    steal->owner = L->heap;
    steal->instack.next = NULL;

    TAILQ_INSERT_HEAD(&L->heap->objects, steal, allocd);

    make_grey(L, steal);
  }
  TAILQ_REMOVE(&G(L)->all_heaps, th->heap, heaps);
  unlock_all_threads();

  free(th->heap);
  th->heap = NULL;

  ck_pr_inc_32(&G(L)->need_global_trace);
}

static void reclaim_object(lua_State *L, GCheader *o, int remove_from_heap)
{
  if (remove_from_heap) {
    TAILQ_REMOVE(&L->heap->objects, o, allocd);
  }
  o->marked |= FREEDBIT;

  switch (o->tt) {
    case LUA_TPROTO:
      luaF_freeproto(L, gco2p(o));
      break;
    case LUA_TFUNCTION:
      {
        size_t size;

        Closure *c = gco2cl(o);
        size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
          sizeLclosure(c->l.nupvalues);
        luaM_freemem(L, LUA_MEM_FUNCTION, c, size);
        break;
      }
    case LUA_TUPVAL:
      luaF_freeupval(L, gco2uv(o));
      break;
    case LUA_TTABLE:
      luaH_free(L, gco2h(o));
      break;
    case LUA_TTHREAD:
      {
        lua_State *th = gco2th(o);

        if (th == G(L)->mainthread) {
          return;
        }
        luaC_inherit_thread(L, th);

        luaE_freethread(L, th);
        break;
      }
    case LUA_TSTRING:
      luaM_freemem(L, LUA_MEM_STRING, o, sizestring(gco2ts(o)));
      break;
    case LUA_TGLOBAL:
      /* skip; someone else will clear this out */
      break;
    case LUA_TUSERDATA:
      luaM_freemem(L, LUA_MEM_USERDATA, o, sizeudata(gco2u(o)));
      break;

    default:
#if HAVE_VALGRIND
      VALGRIND_PRINTF_BACKTRACE("reclaim %s not implemented\n",
          lua_typename(NULL, o->tt));
#endif
      lua_assert(0);
  }
}

static void free_deferred_white(lua_State *L)
{
  GCheader *o;

  while ((o = pop_obj(&L->heap->to_free)) != NULL) {
    reclaim_object(L, o, 0);
  }
}

static int reclaim_white(lua_State *L, int final_close)
{
  GCheader *o, *tmp;
  int reclaimed = 0;
  
  /* Collector is already blocked in this case, no need to block again */
  TAILQ_FOREACH_SAFE(o, &L->heap->objects, allocd, tmp) {

    if (is_black(L, o)) continue;

#if HAVE_VALGRIND && DEBUG_ALLOC
    VALGRIND_PRINTF_BACKTRACE(
      "reclaim %s at %p (marked=%x isxref=%d)\n",
      lua_typename(NULL, o->tt), o, o->marked,
      !is_not_xref(L, o));
#endif

    lua_assert_obj(!is_grey(o) || (o->marked & FINALBIT), o);
    lua_assert_obj(o->owner == L->heap, o);
    lua_assert_obj(final_close == 1 || is_not_xref(L, o), o);
    lua_assert_obj(o->ref == 0, o);

    /* Don't actually reclaim yet, just remove from the heap and queue
     * up for reclamation after we unblock the collector */
    TAILQ_REMOVE(&L->heap->objects, o, allocd);
    push_obj(&L->heap->to_free, o);
    reclaimed++;
  }

  return reclaimed;
}

static void call_finalize(lua_State *L, GCheader *o)
{
  if (o->tt == LUA_TUSERDATA && !is_finalized(o)) {
    Udata *ud;
    const TValue *tm;
    thr_State *pt = luaC_get_per_thread(L);

    o->marked |= FINALBIT;

    ud = rawgco2u(o);
    tm = gfasttm(G(L), gch2h(ud->uv.metatable), TM_GC);
    if (tm) {
      lu_byte hook = L->allowhook;

      /* turn off hooks during finalizer */
      L->allowhook = 0;

      lua_lock(L);
      /* Need to block the collector to muck with the stack like this */
      block_collector(L, pt);

      setobj2s(L, L->top, tm);
      setuvalue(L, L->top + 1, ud);
      L->top += 2;
      unblock_collector(L, pt);
      LUAI_TRY_BLOCK(L) {
        luaD_call(L, L->top - 2, 0);
      } LUAI_TRY_CATCH(L) {
      } LUAI_TRY_END(L);
      L->allowhook = hook;
      lua_unlock(L);
    }
  }
}

static void run_finalize(lua_State *L)
{
  GCheader *o;

  lua_assert(CK_STACK_FIRST(&L->heap->grey) == NULL);

  /* Collector is already blocked, no need to block again */
  TAILQ_FOREACH(o, &L->heap->objects, allocd) {
    lua_assert(o->owner == L->heap);

    if (is_black(L, o) || o->ref || !is_not_xref(L, o)) {
      continue;
    }

    if (o->tt == LUA_TUSERDATA && !is_finalized(o)) {
      lua_assert(!is_finalized(o));

      /* make it grey; will be freed next cycle.
       * Need to make it grey because it is an aggregate and
       * may reference an env or a metatable. If we leave it
       * white here, those will be deemed as white on this sweep,
       * but because we blacken the aggregate, when we later trace,
       * we will still have references to the now-collected env
       * and metatables */
      make_grey(L, o);

      /* Don't actually finalize until we unblock the gc */
      push_finalize(&L->heap->to_finalize, o);
    }
  }
}

static void finalize_deferred(lua_State *L)
{
  GCheader *o;

  while ((o = pop_finalize(&L->heap->to_finalize)) != NULL) {
    call_finalize(L, o);
  }
}

static void check_references(lua_State *L)
{
  GCheader *o;

  lua_assert(CK_STACK_FIRST(&L->heap->grey) == NULL);

  /* Collector is already blocked, no need to block again */
  TAILQ_FOREACH(o, &L->heap->objects, allocd) {
    if (is_black(L, o)) continue;

    lua_assert(o->owner == L->heap);

    /* anything explicitly ref'd from C, or that might be
     * ref'd externally is grey */
    if (o->ref || !is_not_xref(L, o)) {
      mark_object(L, o);
      continue;
    }
  }
}

/* Indicates whether a key or value can be cleared from a weak table.
 * Non-collectable objects are never removed.
 * Strings behave as "values" and are never removed.
 * userdata being finalized: keep them in keys, but not in values.
 * other objects: if collected, have to remove them.
 */
static int iscleared(lua_State *L, const TValue *o, int iskey)
{
  if (!iscollectable(o)) return 0;

  if (ttisstring(o)) {
    mark_object(L, gcvalue(o));
    return 0;
  }
  if (!is_black(L, gcvalue(o))) {
    /* it's white! */
    return 1;
  }
  if (ttisuserdata(o) && !iskey && is_finalized(gcvalue(o))) {
    return 1;
  }
  return 0;
}

static void fixup_weak_refs(lua_State *L)
{
  GCheader *o;

  while ((o = pop_obj(&L->heap->weak)) != NULL) {
    Table *h;
    int j;

    lua_assert(o->owner == L->heap);
    lua_assert(o->marked & (WEAKVALBIT|WEAKKEYBIT));

    h = gco2h(o);
    luaH_wrlock(L, h);

    if ((o->marked & WEAKVALBIT) == WEAKVALBIT) {
      j = h->sizearray;
      while (j--) {
        if (iscleared(L, &h->array[j], 0)) {
          setnilvalue(&h->array[j]);
        }
      }
    }
    j = sizenode(h);
    while (j--) {
      Node *n = gnode(h, j);

      if (!ttisnil(gval(n)) && (
            iscleared(L, key2tval(n), 1) ||
            iscleared(L, gval(n), 0))) {
        setnilvalue(gval(n));
        removeentry(n);
      }
    }
    luaH_wrunlock(L, h);
  }
}

static void sanity_check_mark_status(lua_State *L)
{
  GCheader *o;
  GCheap *h;

  /* These lists must be empty */
  lua_assert(CK_STACK_FIRST(&L->heap->weak) == NULL);
  lua_assert(CK_STACK_FIRST(&L->heap->grey) == NULL);

  TAILQ_FOREACH(o, &L->heap->objects, allocd) {
    lua_assert_obj(o->owner == L->heap, o);
    lua_assert_obj(!is_black(L, o), o);
  }
}

static int local_collection(lua_State *L)
{
  int reclaimed;
  int i;
  struct stringtable_node *n;
  thr_State *pt = luaC_get_per_thread(L);
  struct stringtable_node *tofree = NULL;

  if (L->in_gc) {
    return 0; // happens during finalizers
  }
//  printf("LOCAL marked=%x is_blac=%d\n", L->gch.marked, is_black(L, &L->gch));
  L->in_gc = 1;

  /* The global collector walks our structures, which is not safe to do in a 
   * multi-threaded environment.  Prevent the global collector from running
   * while we are in this function and manipulating our string tables or heap */
  block_collector(L, pt);

  /* prune out excess string table entries.
   * We don't want to be too aggressive, as we'd like to see some benefit
   * from string interning.
   * First pass is to find strings that are large and flush them out */
  for (i = 0; i < L->strt.size; i++) {
    struct stringtable_node *p;

    if (!L->strt.hash[i]) continue;

    p = NULL;
    n = L->strt.hash[i];
    while (n) {
      if (n->str->tsv.len < 128) {
        p = n;
        n = n->next;
        continue;
      }
      /* "too big" */
      if (p) {
        p->next = n->next;
      } else {
        L->strt.hash[i] = n->next;
      }
      n->next = tofree;
      tofree = n;
      L->strt.nuse--;
      if (p) {
        n = p->next;
      } else {
        n = L->strt.hash[i];
      }
    }
  }

  /* Second pass:
   * We remove the head of each chain and repeat until we're
   * below our threshold */
  for (i = 0; L->strt.nuse > 128 && i < L->strt.size; i++) {
    if (L->strt.hash[i]) {
      n = L->strt.hash[i];
      L->strt.hash[i] = n->next;
      n->next = tofree;
      tofree = n;
      L->strt.nuse--;
    }
  }

  /* mark roots */
  make_grey(L, &L->gch);

  while (CK_STACK_FIRST(&L->heap->grey) != NULL) {
    /* trace and make things grey or black */
    propagate(L);
    /* grey any externally referenced white objects */
    check_references(L);
  }

  /* run any finalizers; may turn some objects grey again */
  run_finalize(L);

  while (CK_STACK_FIRST(&L->heap->grey) != NULL) {
    /* trace and make things grey or black */
    propagate(L);
    /* grey any externally referenced white objects */
    check_references(L);
  }

  /* at this point, anything in the White set is garbage */

  /* remove collected weak values from weak tables */
  fixup_weak_refs(L);

  /* and now we can free whatever is left in White.  Note that we're still 
   * blocked here so we are pulling white out of the heap and placing them
   * in another list that will free them when we unblock the collector. */
  reclaimed = reclaim_white(L, 0);

  /* White is the new Black */
  L->black = !L->black;

  sanity_check_mark_status(L);

  /* Now we can un-block the global collector, as we are done with our string
   * tables and our heap. */
  unblock_collector(L, pt);

  /* Finalize deferred objects */
  finalize_deferred(L);

  /* Free any objects that were white */
  free_deferred_white(L);

  /* Free any deferred stringtable nodes */
  while (tofree) {
    n = tofree;
    tofree = tofree->next;
    luaM_freemem(L, LUA_MEM_STRING_TABLE_NODE, n, sizeof(*n));
  }

  /* revise threshold for next run */
  L->thresh = L->gcestimate / 100 * G(L)->gcpause;

  L->in_gc = 0;
  return reclaimed;
}

static void global_trace_obj(lua_State *L, GCheader *lval, GCheader *rval)
{
  int recurse = is_unknown_xref(L, rval);

  set_xref(L, lval, rval, 1);

  if (recurse) {
    traverse_object(L, rval, global_trace_obj);
  }
}

static void trace_heap(GCheap *h)
{
  GCheader *o;

  ck_pr_store_32(&h->owner->xref_count, 0);
  TAILQ_FOREACH(o, &h->objects, allocd) {
    global_trace_obj(h->owner, &h->owner->gch, o);
  }

  /* one less to trace */
  ck_pr_dec_32(&trace_heaps);
}

static void *trace_thread(void *unused)
{
  sigset_t set;

  sigfillset(&set);
  pthread_sigmask(SIG_SETMASK, &set, NULL);

  while (1) {
    struct ck_stack_entry *ent;

    pthread_mutex_lock(&trace_mtx);
    pthread_cond_wait(&trace_cond, &trace_mtx);
    pthread_mutex_unlock(&trace_mtx);

    while (1) {
      ent = ck_stack_pop_upmc(&trace_stack);

      if (!ent) {
        break;
      }
      trace_heap(GCheap_from_stack(ent));
    }
  }
}

/* Global collection must only use async-signal safe functions,
 * or it will lead to a deadlock (especially in printf) */
static void global_trace(lua_State *L)
{
  lua_State *l;
  GCheap *h;

//  VALGRIND_PRINTF_BACKTRACE("stopping world\n");
  if (!try_lock_all_threads()) {
    return;
  }

  if (NON_SIGNAL_COLLECTOR) {
    block_mutators(L);
  }
  else {
    stop_all_threads(L);
  }

//  VALGRIND_PRINTF_BACKTRACE("STOP'd threads; setting stopped flag, flipping xref\n");

#ifdef ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN
  ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
#endif

  ck_pr_store_32(&G(L)->stopped, 1);

  /* flip sense of definitive xref bit */
  if (ck_pr_load_32(&G(L)->isxref) == 1) {
    ck_pr_store_32(&G(L)->isxref, 3);
    ck_pr_store_32(&G(L)->notxref, 2);
  } else {
    ck_pr_store_32(&G(L)->isxref, 1);
    ck_pr_store_32(&G(L)->notxref, 0);
  }

  if (USE_TRACE_THREADS) {
    /* now trace all objects and fix the xref bit */
    TAILQ_FOREACH(h, &G(L)->all_heaps, heaps) {
      ck_pr_inc_32(&trace_heaps);
      ck_stack_push_upmc(&trace_stack, &h->instack);
      /* let consumers know they have things to do */
      pthread_cond_broadcast(&trace_cond);
    }

    /* we are a consumer too */
    while (1) {
      struct ck_stack_entry *ent = ck_stack_pop_upmc(&trace_stack);
    
      if (!ent) {
        break;
      }
      trace_heap(GCheap_from_stack(ent));
    }

    /* we couldn't get any more heaps, now we wait for the pending
     * number of heaps to return to zero, indicating that the trace_threads
     * are all done */
    while (ck_pr_load_32(&trace_heaps) != 0) {
        ck_pr_stall();
    }
  }
  else {
    TAILQ_FOREACH(h, &G(L)->all_heaps, heaps) {
      GCheader *o;

      /* Zero out the new xref count */
      ck_pr_store_32(&h->owner->xref_count, 0);
      TAILQ_FOREACH(o, &h->objects, allocd) {
        global_trace_obj(h->owner, &h->owner->gch, o);
      }
    }
  }

  /* all heaps are traced */

  ck_pr_store_32(&G(L)->need_global_trace, 0);
  ck_pr_store_32(&G(L)->stopped, 0);

#ifdef ANNOTATE_IGNORE_READS_AND_WRITES_END
  ANNOTATE_IGNORE_READS_AND_WRITES_END();
#endif

  if (NON_SIGNAL_COLLECTOR) {
    unblock_mutators(L);
  }
  else {
    resume_threads(L);
  }

  unlock_all_threads();
//  VALGRIND_PRINTF_BACKTRACE("started world\n");
}

void luaC_checkGC(lua_State *L)
{
  if (L->gcestimate >= L->thresh) {
    if ((ck_pr_load_32(&G(L)->need_global_trace) > G(L)->global_trace_thresh) ||
        (ck_pr_load_32(&L->xref_count) > G(L)->global_trace_xref_thresh)) {
      global_trace(L);
    }
    local_collection(L);
  }
}

int luaC_localgc (lua_State *L, int greedy)
{
  int reclaimed = 0;
  int x = 0;

  if ((ck_pr_load_32(&G(L)->need_global_trace) > G(L)->global_trace_thresh) ||
      (ck_pr_load_32(&L->xref_count) > G(L)->global_trace_xref_thresh)) {
    global_trace(L);
  }
  if (!greedy) {
    return local_collection(L);
  }
  do {

    while ((x = local_collection(L)) > 0) {
      reclaimed += x;
    }

    /* we may now find that some of the greys are now
     * white, so do another pass */
    x = local_collection(L);
    if (x) {
      reclaimed += x;
    }
  } while (x);

  return reclaimed;
}

int luaC_fullgc (lua_State *L)
{
  global_trace(L);
  /* We do a step garbage collection here, as opposed to a full one.  New objects
   * are pushed into the global thread when threads are destroyed, and a step
   * garbage collection is sufficient to reclaim enough memory.  This has been
   * run in production and memory is stable.  The reason for not doing a full
   * cycle is that it's relatively time-consuming. */
  return luaC_localgc(L, 0);
}

/* The semantics of lua_close are to free up everything associated
 * with the lua_State, including others states and globals */
LUA_API void lua_close (lua_State *L)
{
  global_State *g = G(L);
  GCheader *o, *n;
  GCheap *h, *htmp;

  /* only the main thread can be closed */
  lua_assert(L == G(L)->mainthread);
  lua_assert(!is_free(&L->gch));
  L->gch.ref = 1;

  /* attempt a graceful first pass */
  lua_settop(L, 0);
  global_trace(L);
  local_collection(L);

  /* Don't think we need to block the collector here */

  /* force all finalizers to run */
  TAILQ_FOREACH(h, &G(L)->all_heaps, heaps) {
    TAILQ_FOREACH(o, &h->objects, allocd) {
      call_finalize(h->owner, o);
    }
  }

  /* now everything is garbage */
  TAILQ_FOREACH_SAFE(h, &G(L)->all_heaps, heaps, htmp) {
    TAILQ_FOREACH_SAFE(o, &h->objects, allocd, n) {
      reclaim_object(L, o, 1);
    }
  }

  luaE_freethread(L, L);

  g->alloc(g->allocdata, LUA_MEM_GLOBAL_STATE, g,
    sizeof(*g) + sizeof(lua_State) + g->extraspace, 0);
}

void lua_assert_fail(const char *expr, GCheader *obj, const char *file, int line)
{
#ifdef VALGRIND_PRINTF_BACKTRACE
  if (obj) {
    VALGRIND_PRINTF_BACKTRACE(
      "Assertion %s failed\nobj=%p owner=%x tt=%d ref=%d marked=%x xref=%x\n",
      expr, obj, obj->owner, obj->tt, obj->ref, obj->marked, obj->xref);
  } else {
    VALGRIND_PRINTF_BACKTRACE("Assertion %s failed\n", expr);
  }
#endif
  fprintf(stderr, "Assertion %s failed at %s:%d\n", expr, file, line);
  abort();
}

/* vim:ts=2:sw=2:et:
 */
