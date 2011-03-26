/*
** $Id: lgc.c,v 2.38.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE
#define DEBUG_ALLOC 0

#include "thrlua.h"

#define USING_DRD 1

#if USING_DRD
# define INLINE /* not inline */
#else
# define INLINE inline
#endif

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
static pthread_key_t tls_key;

/* we use these to track the threads so that we can stop the world */
static pthread_mutex_t all_threads_lock; /* initialized via pthread_once */
static uint32_t num_threads = 0;
static uint32_t parked_threads = 0;
static TAILQ_HEAD(thr_StateList, thr_State)
  all_threads = TAILQ_HEAD_INITIALIZER(all_threads);
static sigset_t suspend_handler_mask;


#define BLACKBIT    (1<<0)
#define WEAKKEYBIT  (1<<1)
#define WEAKVALBIT  (1<<2)
#define GREYBIT     (1<<3)
#define FINALBIT    (1<<4)
#define FREEDBIT    (1<<7)

static void local_collection(lua_State *L);
static void global_collection(lua_State *L);

static INLINE int is_black(lua_State *L, GCheader *obj)
{
  return ((obj->marked & BLACKBIT) == L->black);
}

static INLINE int is_grey(GCheader *obj)
{
  return (obj->marked & GREYBIT) == GREYBIT;
}

static INLINE int is_finalized(GCheader *obj)
{
  return (obj->marked & FINALBIT) == FINALBIT;
}

static INLINE int is_aggregate(GCheader *obj)
{
  return obj->tt > LUA_TSTRING;
}

static INLINE int is_free(GCheader *obj)
{
  return (obj->marked & FREEDBIT) == FREEDBIT;
}

/** defines GCheader_from_stack to convert a stack entry to a
 * GCheader */
CK_STACK_CONTAINER(GCheader, instack, GCheader_from_stack);

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

static void removeentry(Node *n)
{
  lua_assert(ttisnil(gval(n)));
  if (iscollectable(gkey(n)))
    setttype(key2tval(n), LUA_TDEADKEY);  /* dead key; remove it */
}

static INLINE int is_unknown_xref(lua_State *L, GCheader *o)
{
  if ((G(L)->isxref & 3) == 1) {
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
    ck_pr_store_32(&rval->xref, G(L)->isxref);
  } else if (force && is_unknown_xref(L, rval)) {
    ck_pr_store_32(&rval->xref, G(L)->notxref);
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
  if (L->heap != obj->owner) {
    /* external reference */
    ck_pr_store_32(&obj->xref, G(L)->isxref);
    return;
  }

  lua_assert_obj(!is_free(obj), obj);
  lua_assert_obj(obj->owner == L->heap, obj);

  if (is_grey(obj) || is_black(L, obj)) {
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

#define BLOCK_COLLECTOR_USING_SIGNALS 0
static INLINE void block_collector(lua_State *L)
{
#if BLOCK_COLLECTOR_USING_SIGNALS
  sigset_t mask;

  sigfillset(&mask);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);
#else
  thr_State *pt = luaC_get_per_thread();

  for (;;) {
    while (ck_pr_load_32(&G(L)->intend_to_stop) == 1) {
      /* we will get suspended momentarily */
      ck_pr_stall();
    }
    /* tell a possible collector that we're in a write barrier */
    ck_pr_store_32(&pt->in_barrier, 1);
    ck_pr_fence_memory();
    if (ck_pr_load_32(&G(L)->intend_to_stop) == 0)
      break;

    ck_pr_store_32(&pt->in_barrier, 0);
  }
#endif
}

static INLINE void unblock_collector(lua_State *L)
{
#if BLOCK_COLLECTOR_USING_SIGNALS
  sigset_t mask;

  sigfillset(&mask);
  pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
#else
  thr_State *pt = luaC_get_per_thread();

  ck_pr_store_32(&pt->in_barrier, 0);
#endif
}

void luaC_writebarrierov(lua_State *L, GCheader *object,
  GCheader **lvalue, const TValue *rvalue)
{
  GCheader *ro = gcvalue(rvalue);

  lua_assert(ro != NULL);
  checkconsistency(rvalue);

  set_xref(L, object, ro, 0);
  mark_object(L, ro);

  *lvalue = ro;
}


void luaC_writebarriervv(lua_State *L, GCheader *object,
  TValue *lvalue, const TValue *rvalue)
{
  GCheader *ro = iscollectable(rvalue) ? gcvalue(rvalue) : NULL;

  if (ro) {
    set_xref(L, object, ro, 0);
    mark_object(L, ro);
  }

  block_collector(L);
  lvalue->value = rvalue->value;
  /* RACE: a global trace can trigger here and catch us pants-down
   * wrt ->tt != value->tt; so this section must be protected by blocking
   * the collector */
  lvalue->tt = rvalue->tt;
  unblock_collector(L);
}

void luaC_writebarrier(lua_State *L, GCheader *object,
  GCheader **lvalue, GCheader *rvalue)
{
  if (rvalue) {
    set_xref(L, object, rvalue, 0);
    mark_object(L, rvalue);
  }

  *lvalue = rvalue;
}

/* broken out into a function to allow us to suppress it from drd */
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
        break;
      }
    case LUA_TUPVAL:
      {
        UpVal *uv = gco2uv(o);
        traverse_value(L, o, uv->v, objfunc);
        break;
      }
    case LUA_TTABLE:
      {
        Table *h = gco2h(o);
        int weakkey = 0, weakvalue = 0;
        const TValue *mode;

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
        if (!is_world_stopped(L)) luaH_unlock(L, h);
        break;
      }

    case LUA_TFUNCTION:
      {
        Closure *cl = gco2cl(o);

        if (cl->c.isC) {
          traverse_obj(L, o, cl->c.env, objfunc);
          for (i = 0; i < cl->c.nupvalues; i++) {
            traverse_value(L, o, &cl->c.upvalue[i], objfunc);
          }
        } else {
          lua_assert(cl->l.nupvalues == cl->l.p->nups);
          traverse_obj(L, o, &cl->l.p->gch, objfunc);
          traverse_obj(L, o, cl->l.env, objfunc);
          for (i = 0; i < cl->l.nupvalues; i++) {
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
          lua_typename(NULL, o->tt), o);
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

  pt = pthread_getspecific(tls_key);

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

  pt = pthread_getspecific(tls_key);
  ck_pr_store_uint(&pt->wake, 1);

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

#if !BLOCK_COLLECTOR_USING_SIGNALS
  ck_pr_store_32(&G(L)->intend_to_stop, sig == LUA_SIG_SUSPEND ? 1 : 0);
  ck_pr_fence_memory();
#endif

  TAILQ_FOREACH(pt, &all_threads, threads) {
    if (pthread_equal(me, pt->tid)) {
      /* can't stop myself */
      continue;
    }
    if (pt->dead) {
      continue;
    }
#if !BLOCK_COLLECTOR_USING_SIGNALS
    if (sig == LUA_SIG_SUSPEND) {
      /* wait for thread to leave its barrier */
      while (ck_pr_load_32(&pt->in_barrier) == 1) {
        ck_pr_stall();
      }
    }
#endif

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
  while (parked_threads < num_threads - 1) {
    sched_yield();
  }
}

/* caller MUST hold all_threads_lock */
/* MUST be async signal safe */
static int resume_threads(lua_State *L)
{
  signal_all_threads(L, LUA_SIG_RESUME);
  while (parked_threads) {
    sched_yield();
  }
}

static void thread_exited(void *p)
{
  thr_State *thr = p;

  ck_pr_dec_32(&num_threads);

  /* POSIX states that we are only called when p is non-NULL */
  assert(p != NULL);

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

  pt = pthread_getspecific(tls_key);
  if (pt) {
    thread_exited(pt);
  }
}

static void make_tls_key(void)
{
  struct sigaction act;
  pthread_mutexattr_t m;

  atexit(free_last_global_bits);

  pthread_mutexattr_init(&m);
  pthread_mutexattr_settype(&m, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&all_threads_lock, &m);
  pthread_mutexattr_destroy(&m);

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

  pthread_key_create(&tls_key, thread_exited);
}

thr_State *luaC_get_per_thread(void)
{
  thr_State *pt;

  pthread_once(&tls_init, make_tls_key);
  pt = pthread_getspecific(tls_key);
  if (pt == NULL) {
    pt = calloc(1, sizeof(*pt));
    pthread_setspecific(tls_key, pt);
    pt->tid = pthread_self();

    lock_all_threads();
    TAILQ_INSERT_HEAD(&all_threads, pt, threads);
    unlock_all_threads();

    ck_pr_inc_32(&num_threads);
  }
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
  thr_State *pt;

  /* this performs once-init stuff */
  pt = luaC_get_per_thread();

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
  g->allocdata = p->allocdata;
  g->extraspace = p->extraspace;
  g->on_state_create = p->on_state_create;
  g->on_state_finalize = p->on_state_finalize;
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

  o = luaM_realloc(L, objtype, NULL, 0, size);
  memset(o, 0, size);
  o->owner = L->heap;
  o->tt = tt;
  o->marked = !L->black;
  o->xref = G(L)->notxref;
  TAILQ_INSERT_HEAD(&L->heap->objects, o, allocd);
  make_grey(L, o);

  return o;
}

void *luaC_newobj(lua_State *L, enum lua_obj_type tt)
{
  GCheader *o;

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
      TAILQ_INSERT_HEAD(&n->heap->objects, &n->gch, allocd);
      make_grey(n, &n->gch);
      o = &n->gch;
      o->marked = !n->black;

      n->gch.xref = G(L)->notxref;

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

  TAILQ_FOREACH_SAFE(steal, &th->heap->objects, allocd, tmp) {
    TAILQ_REMOVE(&th->heap->objects, steal, allocd);

    steal->owner = L->heap;
    steal->instack.next = NULL;

    TAILQ_INSERT_HEAD(&L->heap->objects, steal, allocd);
  }
  TAILQ_REMOVE(&G(L)->all_heaps, th->heap, heaps);
  unlock_all_threads();

  free(th->heap);
  th->heap = NULL;
}

static void reclaim_object(lua_State *L, GCheader *o)
{
  TAILQ_REMOVE(&L->heap->objects, o, allocd);
  o->marked |= FREEDBIT;

  switch (o->tt) {
    case LUA_TPROTO:
      luaF_freeproto(L, gco2p(o));
      break;
    case LUA_TFUNCTION:
      {
        size_t size;

        Closure *c = gco2cl(o);
#if HAVE_VALGRIND && DEBUG_ALLOC
        if (c->c.isC) {
          VALGRIND_PRINTF("reclaiming C function %s %p\n", c->c.fname, c->c.f);
        } else {
          VALGRIND_PRINTF("reclaiming lua function %p proto=%p\n", o, c->l.p);
        }
#elif 0
        if (c->c.isC) {
          printf("reclaiming C function %s %p\n", c->c.fname, c->c.f);
        } else {
          printf("reclaiming lua function %p proto=%p xref=%x (isxref=%x notxref=%x)\n", o, c->l.p, o->xref, G(L)->isxref, G(L)->notxref);
        }
#endif
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

static void reclaim_white(lua_State *L, int final_close)
{
  GCheader *o, *tmp;

  TAILQ_FOREACH_SAFE(o, &L->heap->objects, allocd, tmp) {

    if (is_black(L, o)) continue;

#if HAVE_VALGRIND && DEBUG_ALLOC
    VALGRIND_PRINTF_BACKTRACE(
      "reclaim %s at %p (marked=%x isxref=%d)\n",
      lua_typename(NULL, o->tt), o, o->marked,
      !is_not_xref(L, o));
#endif

    lua_assert(o->owner == L->heap);
    lua_assert(final_close == 1 || is_not_xref(L, o));
    lua_assert(o->ref == 0);

    reclaim_object(L, o);
  }
}

static void call_finalize(lua_State *L, GCheader *o)
{
  if (o->tt == LUA_TUSERDATA && !is_finalized(o)) {
    Udata *ud;
    const TValue *tm;

    o->marked |= FINALBIT;

    ud = rawgco2u(o);
    tm = gfasttm(G(L), gch2h(ud->uv.metatable), TM_GC);
    if (tm) {
      /* FIXME: prevent GC, debug hooks during finalizer */
      setobj2s(L, L->top, tm);
      setuvalue(L, L->top + 1, ud);
      L->top += 2;
      lua_lock(L);
      LUAI_TRY_BLOCK(L) {
        luaD_call(L, L->top - 2, 0);
      } LUAI_TRY_CATCH(L) {
      } LUAI_TRY_END(L);
      lua_unlock(L);
    }
  }
}

static void run_finalize(lua_State *L)
{
  GCheader *o;

  lua_assert(CK_STACK_FIRST(&L->heap->grey) == NULL);

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
      mark_object(L, o);
      call_finalize(L, o);
    }
  }
}


static void finalize_all_local(lua_State *L)
{
#if 0
  move_matching_objects_to(L, L->Black, &L->Finalize, is_finalizable_on_close);
  move_matching_objects_to(L, L->White, &L->Finalize, is_finalizable_on_close);
  move_matching_objects_to(L, &L->Grey, &L->Finalize, is_finalizable_on_close);
  move_matching_objects_to(L, &L->Weak, &L->Finalize, is_finalizable_on_close);
#endif
  run_finalize(L);
}

static void whitelist_non_root(lua_State *L)
{
#if 0
  move_matching_objects_to(L, L->Black, L->White, is_non_root_on_close);
  move_matching_objects_to(L, &L->Grey, L->White, is_non_root_on_close);
  move_matching_objects_to(L, &L->Weak, L->White, is_non_root_on_close);
  unlink_list(&L->gch);
  unlink_list(&G(L)->gch);
#endif
}

static void check_references(lua_State *L)
{
  GCheader *o;

  lua_assert(CK_STACK_FIRST(&L->heap->grey) == NULL);

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
    luaH_unlock(L, h);
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

static void local_collection(lua_State *L)
{
  if (L->in_gc) {
    return; // happens during finalizers
  }
//  printf("LOCAL marked=%x is_blac=%d\n", L->gch.marked, is_black(L, &L->gch));
  L->in_gc = 1;

  luaE_flush_stringtable(L);

  /* mark roots */
  make_grey(L, &L->gch);

  /* trace and make things grey or black */
  propagate(L);
  /* grey any externally referenced white objects */
  check_references(L);
  /* may have made more greys */
  propagate(L);

  /* run any finalizers; may turn some objects grey again */
  run_finalize(L);
  /* may have made more greys */
  propagate(L);

  /* at this point, anything in the White set is garbage */

  /* remove collected weak values from weak tables */
  fixup_weak_refs(L);

  /* and now we can free whatever is left in White */
  reclaim_white(L, 0);

  /* White is the new Black */
  L->black = !L->black;

  sanity_check_mark_status(L);

  /* revise threshold for next run */
  L->thresh = L->gcestimate / 100 * G(L)->gcpause;

  L->in_gc = 0;
}

static void global_trace_obj(lua_State *L, GCheader *lval, GCheader *rval)
{
  int recurse = is_unknown_xref(L, rval);

  set_xref(L, lval, rval, 1);

  if (recurse) {
    traverse_object(L, rval, global_trace_obj);
  }
}

/* Global collection must only use async-signal safe functions,
 * or it will lead to a deadlock (especially in printf) */
static void global_collection(lua_State *L)
{
  lua_State *l;
  GCheap *h;

//  VALGRIND_PRINTF_BACKTRACE("stopping world\n");
  if (!try_lock_all_threads()) {
    return;
  }
  stop_all_threads(L);
//  VALGRIND_PRINTF_BACKTRACE("STOP'd threads; setting stopped flag, flipping xref\n");

#ifdef ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN
  ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
#endif

  ck_pr_store_32(&G(L)->stopped, 1);

  /* flip sense of definitive xref bit */
  if (G(L)->isxref == 1) {
    G(L)->isxref = 3;
    G(L)->notxref = 2;
  } else {
    G(L)->isxref = 1;
    G(L)->notxref = 0;
  }

  /* now trace all objects and fix the xref bit */
  TAILQ_FOREACH(h, &G(L)->all_heaps, heaps) {
    GCheader *o;

    TAILQ_FOREACH(o, &h->objects, allocd) {
      global_trace_obj(h->owner, &h->owner->gch, o);
    }
  }

  ck_pr_store_32(&G(L)->stopped, 0);

#ifdef ANNOTATE_IGNORE_READS_AND_WRITES_END
  ANNOTATE_IGNORE_READS_AND_WRITES_END();
#endif

  resume_threads(L);

  unlock_all_threads();
//  VALGRIND_PRINTF_BACKTRACE("started world\n");
}

#define RANDOM_GC 1

void luaC_checkGC(lua_State *L)
{
  if (L->gcestimate >= L->thresh) {
    local_collection(L);
  }
}

void luaC_localgc (lua_State *L)
{
  local_collection(L);
}

void luaC_fullgc (lua_State *L)
{
  global_collection(L);
  local_collection(L);
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
  lua_pop(L, lua_gettop(L));
  global_collection(L);
  local_collection(L);

  /* force all finalizers to run */
  TAILQ_FOREACH(h, &G(L)->all_heaps, heaps) {
    TAILQ_FOREACH(o, &h->objects, allocd) {
      call_finalize(h->owner, o);
    }
  }

  /* now everything is garbage */
  TAILQ_FOREACH_SAFE(h, &G(L)->all_heaps, heaps, htmp) {
    TAILQ_FOREACH_SAFE(o, &h->objects, allocd, n) {
      reclaim_object(L, o);
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
