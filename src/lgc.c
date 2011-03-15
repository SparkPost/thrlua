/*
** $Id: lgc.c,v 2.38.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE
#define DEBUG_ALLOC 0

#include "thrlua.h"
#include <semaphore.h>

#ifdef LUA_OS_LINUX
# define DEF_LUA_SIG_SUSPEND SIGPWR
# define DEF_LUA_SIG_RESUME  SIGXCPU
#elif defined(LUA_OS_DARWIN)
# define DEF_LUA_SIG_SUSPEND SIGUSR1
# define DEF_LUA_SIG_RESUME  SIGXCPU
#else
# define DEF_LUA_SIG_SUSPEND SIGUSR1
# define DEF_LUA_SIG_RESUME  SIGUSR2
#endif

static int LUA_SIG_SUSPEND = DEF_LUA_SIG_SUSPEND;
static int LUA_SIG_RESUME  = DEF_LUA_SIG_RESUME;

#ifdef LUA_OS_DARWIN
/* Darwin has mildly retarded semaphores */
struct lua_sem_t {
  pthread_mutex_t lock;
  pthread_cond_t  cond;
  unsigned int value;
};

static int lua_sem_init(struct lua_sem_t *sem, int pshared, unsigned int v)
{
  pthread_mutexattr_t m;

  pthread_mutexattr_init(&m);
  pthread_mutexattr_settype(&m, PTHREAD_MUTEX_ERRORCHECK);
  if (pthread_mutex_init(&sem->lock, &m)) {
    pthread_mutexattr_destroy(&m);
    return -1;
  }
  pthread_mutexattr_destroy(&m);

  if (pthread_cond_init(&sem->cond, NULL)) {
    return -1;
  }
  sem->value = v;
  return 0;
}

static int lua_sem_post(struct lua_sem_t *sem)
{
  int r;

  r = pthread_mutex_lock(&sem->lock);
  if (r) {
    fprintf(stderr, "sem_post lock: %d %s\n", r, strerror(r));
    abort();
  }
  r = pthread_cond_signal(&sem->cond);
  if (r) {
    fprintf(stderr, "sem_post signal: %d %s\n", r, strerror(r));
    abort();
  }
  sem->value++;
  r = pthread_mutex_unlock(&sem->lock);
  if (r) {
    fprintf(stderr, "sem_post unlock: %d %s\n", r, strerror(r));
    abort();
  }
  return 0;
}

static int lua_sem_wait(struct lua_sem_t *sem)
{
  int r;

  r = pthread_mutex_lock(&sem->lock);
  if (r) {
    fprintf(stderr, "sem_wait lock: %d %s\n", r, strerror(r));
    abort();
  }

  do {
    if (sem->value > 0) {
      sem->value--;
      r = pthread_mutex_unlock(&sem->lock);
      if (r) {
        fprintf(stderr, "sem_wait unlock: %d %s\n", r, strerror(r));
        abort();
      }
      return 0;
    }

    r = pthread_cond_wait(&sem->cond, &sem->lock);
    if (r) {
      fprintf(stderr, "sem_wait cond_wait: %d %s\n", r, strerror(r));
      abort();
    }
  } while (1);
}

static int lua_sem_getvalue(struct lua_sem_t *sem, int *value)
{
  int r;

  r = pthread_mutex_lock(&sem->lock);
  if (r) {
    fprintf(stderr, "sem_post lock: %d %s\n", r, strerror(r));
    abort();
  }

  *value = sem->value;

  r = pthread_mutex_unlock(&sem->lock);
  if (r) {
    fprintf(stderr, "sem_post unlock: %d %s\n", r, strerror(r));
    abort();
  }
  return 0;
}
# define sem_t struct lua_sem_t
# define sem_init lua_sem_init
# define sem_post lua_sem_post
# define sem_wait lua_sem_wait
# define sem_getvalue lua_sem_getvalue

#endif

#define BLACKBIT    (1<<0)
#define WEAKKEYBIT  (1<<1)
#define WEAKVALBIT  (1<<2)
#define FREEDBIT    (1<<7)


static inline int is_black(lua_State *L, GCheader *obj)
{
  return ((obj->marked & BLACKBIT) == L->black);
}

static inline int isaggregate(GCheader *obj)
{
  return obj->tt > LUA_TSTRING;
}

static inline void init_list(GCheader *head)
{
  head->next = head;
  head->prev = head;
}

static inline void prep_list(GCheader *head)
{
  if (head->next == NULL) {
    init_list(head);
  }
}

static inline void unlink_list(GCheader *obj)
{
  if (obj->next) {
    obj->next->prev = obj->prev;
  }
  if (obj->prev) {
    obj->prev->next = obj->next;
  }
  obj->next = NULL;
  obj->prev = NULL;
}

static inline void append_list(GCheader *head, GCheader *obj)
{
  if (head->next == NULL) init_list(head);
  unlink_list(obj);
  obj->next = head;
  obj->prev = head->prev;
  obj->prev->next = obj;
  head->prev = obj;
}

static void walk_gch_list(global_State *g, GCheader *list, const char *caption)
{
  GCheader *o;
#if HAVE_VALGRIND && DEBUG_ALLOC
  if (list->next == list) return;
  VALGRIND_PRINTF("%s\n", caption);
  VALGRIND_PRINTF("head=%p head->next=%p head->prev=%p\n", list, list->next, list->prev);
  for (o = list->next; o != list; o = o->next) {
    VALGRIND_PRINTF(" >> %s at %p ref=%d marked=%x\n",
      lua_typename(NULL, o->tt), o, o->ref, o->marked);
  }
#else
  if (list->next == list) return;
  printf("%s\n", caption);
  printf("head=%p head->next=%p head->prev=%p\n", list, list->next, list->prev);
  for (o = list->next; o != list; o = o->next) {
    printf(" >> %s at %p owner=%x ref=%d mark=%x xref=%x\n",
      lua_typename(NULL, o->tt), o, o->owner, o->ref, o->marked, o->xref);
  }
#endif
}

static void removeentry(Node *n)
{
  lua_assert(ttisnil(gval(n)));
  if (iscollectable(gkey(n)))
    setttype(key2tval(n), LUA_TDEADKEY);  /* dead key; remove it */
}

static inline int is_unknown_xref(lua_State *L, GCheader *o)
{
  if ((G(L)->isxref & 3) == 1) {
    return (o->xref & 2) == 2;
  }
  return (o->xref & 2) == 0;
}

static inline void set_xref(lua_State *L, GCheader *lval, GCheader *rval,
  int force)
{
  if (lval->owner != rval->owner) {
    rval->xref = G(L)->isxref;
  } else if (force && is_unknown_xref(L, rval)) {
    rval->xref = G(L)->notxref;
  }
}

static inline void mark_object(lua_State *L, GCheader *obj)
{
  if (L->heapid != obj->owner) {
    /* external reference */
    obj->xref = G(L)->isxref;
    return;
  }

  lua_assert_obj((obj->marked & FREEDBIT) == 0, obj);
#if 0
  if (obj == &L->gch) {
    printf("enter: mark main lua_State %p marked=%x black=%x isblack=%d\n",
      L, L->gch.marked, L->black, is_black(L, obj));
  }
#endif

  if (!is_black(L, obj)) {
    obj->marked = (obj->marked & ~BLACKBIT) | L->black;
    if (isaggregate(obj)) {
      append_list(&L->Grey, obj);
    } else {
      append_list(L->Black, obj);
    }
  }
  lua_assert(is_black(L, obj));
}

typedef void (*objfunc_t)(lua_State *, GCheader *, GCheader *);
static void traverse_object(lua_State *L, GCheader *o, objfunc_t objfunc);

static inline void traverse_obj(lua_State *L, GCheader *obj, GCheader *rval,
  objfunc_t objfunc)
{
  objfunc(L, obj, rval);
}

static inline void traverse_value(lua_State *L, GCheader *obj, TValue *val,
  objfunc_t objfunc)
{
  checkconsistency(val);
  if (iscollectable(val)) {
    traverse_obj(L, obj, gcvalue(val), objfunc);
  }
}

void luaC_writebarrierov(lua_State *L, GCheader *object,
  GCheader **lvalue, const TValue *rvalue)
{
  GCheader *ro = gcvalue(rvalue);

  if (ro) {
    set_xref(L, object, ro, 0);
    mark_object(L, ro);
  }

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

  lvalue->value = rvalue->value;
  lvalue->tt = rvalue->tt;
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
          traverse_obj(L, o, (GCheader*)ud->uv.metatable, objfunc);
        }
        if (ud->uv.env) {
          traverse_obj(L, o, (GCheader*)ud->uv.env, objfunc);
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

        if (!G(L)->stopped) luaH_rdlock(L, h);
        if (h->metatable) {
          traverse_obj(L, o, h->metatable, objfunc);
        }
        mode = gfasttm(G(L), gch2h(h->metatable), TM_MODE);
        if (mode && ttisstring(mode)) {
          weakkey = (strchr(svalue(mode), 'k') != NULL);
          weakvalue = (strchr(svalue(mode), 'v') != NULL);
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
            removeentry(n);
          } else {
            lua_assert(!ttisnil(gkey(n)));
            if (!weakvalue) {
              traverse_value(L, o, gval(n), objfunc);
            }
            if (!weakkey && iscollectable(gkey(n))) {
              traverse_obj(L, o, gcvalue(gkey(n)), objfunc);
            }
          }
        }
        if (!G(L)->stopped) luaH_unlock(L, h);
        if (weakkey || weakvalue) { // FIXME
          /* instead of falling through to moving this to the Black list, put
           * it on the weak list */
          append_list(&L->Weak, o);
          return;
        }
        /* fall through to regular Black list */
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

        if (!G(L)->stopped) lua_lock(th);
        traverse_value(L, o, &th->l_gt, objfunc);
        traverse_value(L, o, &th->env, objfunc);
        traverse_value(L, o, &th->tls, objfunc);
        /* the corresponding global state */
        lua_assert(G(L) == th->l_G);
        traverse_obj(L, o, (GCheader*)th->l_G, objfunc);

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
        for (; sk <= lim; sk++) {
          setnilvalue(sk); // FIXME: only for local thread?
        }
        /* open upvalues also */
        for (uv = th->openupval.u.l.next;
            uv != &th->openupval; uv = uv->u.l.next) {
          traverse_obj(L, o, (GCheader*)uv, objfunc);
        }
        if (!G(L)->stopped) lua_unlock(th);
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
  int i;

  lua_assert((o->marked & FREEDBIT) == 0);
  lua_assert(o->owner == L->heapid);
  o->marked = (o->marked & ~BLACKBIT) | L->black;

  traverse_object(L, o, grey_object);

  switch (o->tt) {
    case LUA_TTABLE: /* FIXME: speed up weak table detection */
      {
        Table *h = gco2h(o);
        int weakkey = 0, weakvalue = 0;
        const TValue *mode;

        if (!G(L)->stopped) luaH_rdlock(L, h); /* FIXME: deadlock a possibility */
        mode = gfasttm(G(L), gch2h(h->metatable), TM_MODE);
        if (mode && ttisstring(mode)) {
          weakkey = (strchr(svalue(mode), 'k') != NULL);
          weakvalue = (strchr(svalue(mode), 'v') != NULL);
        }

        if (!G(L)->stopped) luaH_unlock(L, h);
        if (weakkey || weakvalue) {
          /* instead of falling through to moving this to the Black list, put
           * it on the weak list */
          append_list(&L->Weak, o);
          break;
        }
        /* regular Black list */
        append_list(L->Black, o);
        break;
      }
    default:
      append_list(L->Black, o);
  }

}

static inline int iscollected(lua_State *L, const TValue *val, int iskey)
{
  if (!iscollectable(val)) {
    return 0;
  }
#if 0
  if (ttisstring(val)) {
    /* strings are 'values' and are never weak */
    return 0;
  }
#endif
  if (!is_black(L, gcvalue(val))) {
    return 1;
  }
  /* FIXME: we differ from stock lua in that we don't preserve a weak
   * finalized udata key. */
  return 0;
}

void luaC_fullgc (lua_State *L)
{
}

static pthread_once_t tls_init = PTHREAD_ONCE_INIT;
static pthread_key_t tls_key;
static pthread_mutex_t all_threads_lock; /* initialized via pthread_once */
static thr_State *all_threads = NULL;
static sem_t world_sem_inst;
static sem_t *world_sem = &world_sem_inst;
static sigset_t suspend_handler_mask;

static inline void lock_all_threads(void)
{
  int r = pthread_mutex_lock(&all_threads_lock);
  if (r) {
    fprintf(stderr, "LOCK(all_threads): %d %s\n", r, strerror(r));
    abort();
  }
}

static inline int try_lock_all_threads(void)
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


static inline void unlock_all_threads(void)
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

  pt->wake = 0;

  /* signal the thread that is doing the global collection that we
   * are considered stopped */
  r = sem_post(world_sem);
  VALGRIND_PRINTF_BACKTRACE("suspend requested\n");
  while (!pt->wake) {
    sigsuspend(&suspend_handler_mask);
  }

  /* before we can safely resume, all resume threads MUST check back
   * in with the global collector, otherwise another thread may attempt
   * to start a new global sweep while some of the threads are still
   * waking up; this can result in some weird re-entrancy issues that
   * cause threads to get stuck */
  sem_post(world_sem);

  VALGRIND_PRINTF_BACKTRACE("resumed!\n");

  errno = save;
}

/* MUST be async signal safe */
static void thread_resume_requested(int sig)
{
  thr_State *pt;
  int save = errno;

  pt = pthread_getspecific(tls_key);
  pt->wake = 1;

  errno = save;
}

/* MUST be async signal safe */
static void wait_for_thread_checkin(const char *label, int nthreads)
{
  int i, r;

  /* wait for them all to check in */
  for (i = 0; i < nthreads; i++) {
    do {
      VALGRIND_PRINTF_BACKTRACE("%s waiting on thread %d\n", label, i);
      r = sem_wait(world_sem);
      VALGRIND_PRINTF_BACKTRACE("%s woke up on thread %d\n", label, i);
      if (r == 0) {
        break;
      }
      if (errno != EINTR) {
        fprintf(stderr, "wait_for_thread_checkin: %s sem_wait: %s\n",
          label, strerror(errno));
        abort();
      }
    } while (1);
  }
}

/* MUST be async signal safe */
static pthread_t *stop_all_threads(int *howmany)
{
  thr_State *pt;
  pthread_t me = pthread_self();
  int nthreads = 0;
  int r, i;
  pthread_t *threads = NULL;

  for (pt = all_threads; pt; pt = pt->next) {
    nthreads++;
  }

  threads = malloc(nthreads * sizeof(*threads));

  for (i = 0, pt = all_threads; pt; pt = pt->next) {
    if (pthread_equal(me, pt->tid)) {
      /* can't stop myself */
      nthreads--;
      continue;
    }
    VALGRIND_PRINTF_BACKTRACE("pthread_kill %p LUA_SIG_SUSPEND\n", (void*)pt->tid);
    r = pthread_kill(pt->tid, LUA_SIG_SUSPEND);
    VALGRIND_PRINTF_BACKTRACE("pthread_kill %p LUA_SIG_SUSPEND => %d\n",
      (void*)pt->tid, r);
    if (r == ESRCH) {
      nthreads--;
    } else if (r) {
      fprintf(stderr, "signal_all_threads: pthread_kill %s\n", strerror(r));
      abort();
    }
    threads[i++] = pt->tid;
  }
  *howmany = i;

  wait_for_thread_checkin("STOP", nthreads);

  VALGRIND_PRINTF_BACKTRACE("STOP'd %d threads\n", nthreads);

  return threads;
}

/* caller MUST hold all_threads_lock */
/* MUST be async signal safe */
static int resume_threads(pthread_t *threads, int nthreads)
{
  int r, i;

  VALGRIND_PRINTF_BACKTRACE("resume %d threads\n", nthreads);

  for (i = 0; i < nthreads; i++) {
    r = pthread_kill(threads[i], LUA_SIG_RESUME);
    if (r == ESRCH) {
      nthreads--;
    } else if (r) {
      fprintf(stderr, "signal_all_threads: pthread_kill %s\n", strerror(r));
      abort();
    }
  }

  wait_for_thread_checkin("RESUME", nthreads);

  free(threads);
}

static void thread_exited(void *p)
{
  thr_State *thr = p;

  lock_all_threads();
  if (all_threads == thr) {
    all_threads = thr->next;
  }
  if (thr->next) {
    thr->next->prev = thr->prev;
  }
  if (thr->prev) {
    thr->prev->next = thr->next;
  }
  unlock_all_threads();
  free(thr);
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

static void make_tls_key(void)
{
  struct sigaction act;
  pthread_mutexattr_t m;

  pthread_mutexattr_init(&m);
  pthread_mutexattr_settype(&m, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&all_threads_lock, &m);
  pthread_mutexattr_destroy(&m);

  if (sem_init(world_sem, 0, 0) != 0) {
    fprintf(stderr, "failed to init semaphore: %s\n", strerror(errno));
    abort();
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
    pt->next = all_threads;
    if (pt->next) {
      pt->next->prev = pt;
    }
    all_threads = pt;
    unlock_all_threads();
  }
  return pt;
}

global_State *luaC_newglobal(struct lua_StateParams *p)
{
  global_State *g;
  pthread_condattr_t cattr;
  pthread_mutexattr_t mattr;

  g = p->allocfunc(p->allocdata, NULL, 0, sizeof(*g));
  if (!g) {
    return NULL;
  }
  memset(g, 0, sizeof(*g));
  g->gch.tt = LUA_TGLOBAL;
  g->alloc = p->allocfunc;
  g->allocdata = p->allocdata;
  g->extraspace = p->extraspace;
  g->on_state_create = p->on_state_create;
  g->on_state_finalize = p->on_state_finalize;
  g->isxref = 1; /* g->notxref is implicitly set to 0 by memset above */

  return g;
}

int LUAI_EXTRASPACE = 0;

void *luaC_newobj(lua_State *L, lu_byte tt)
{
  GCheader *o;

  switch (tt) {
#define NEWIMPL(a, b) \
    case a: \
      lua_lock(L); \
      o = luaM_reallocG(G(L), NULL, 0, sizeof(b)); \
      memset(o, 0, sizeof(b)); \
      o->owner = L->heapid; \
      o->tt = a; \
      o->marked = !L->black; \
      mark_object(L, o); \
      lua_unlock(L); \
      break
    NEWIMPL(LUA_TUPVAL, UpVal);
    NEWIMPL(LUA_TPROTO, Proto);
    NEWIMPL(LUA_TTABLE, Table);
    case LUA_TTHREAD:
    {
      lua_State *n;

      lua_lock(L);
      n = luaM_reallocG(G(L), NULL, 0, sizeof(lua_State) + G(L)->extraspace);
      memset(n, 0, sizeof(lua_State) + G(L)->extraspace);
      n->gch.tt = LUA_TTHREAD;
      n->heapid = scpt_atomic_inc(&G(L)->nextheapid);
      n->gch.owner = n->heapid;
      n->gch.marked = 1; /* white wrt. its own list */
      n->gch.xref = G(L)->isxref;
      o = &n->gch;

      /* maintain a separate list of lua_State */
      lock_all_threads();
      /* insert after the main thread */
      n->next = G(L)->mainthread->next;
      if (n->next) {
        n->next->prev = n;
      }
      n->prev = G(L)->mainthread;
      n->prev->next = n;
      unlock_all_threads();

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

void *luaC_newobjv(lua_State *L, lu_byte tt, size_t size)
{
  GCheader *o = NULL;

  switch (tt) {
#undef NEWIMPL
#define NEWIMPL(a, b) \
    case a: \
      lua_lock(L); \
      o = luaM_reallocG(G(L), NULL, 0, size); \
      memset(o, 0, size); \
      o->owner = L->heapid; \
      o->tt = a; \
      o->marked = !L->black; \
      mark_object(L, o); \
      lua_unlock(L); \
      break
    NEWIMPL(LUA_TFUNCTION, Closure);
    NEWIMPL(LUA_TSTRING, TString);
    NEWIMPL(LUA_TUSERDATA, Udata);
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

static int is_finalizable_on_close(lua_State *L, GCheader *o)
{
  /* Note: we ignore pinned refs in this case */

  if (o->xref == G(L)->isxref) {
    return 0;
  }

  if (o->tt == LUA_TUSERDATA && !gco2u(o)->finalized) {
    return 1;
  }
  return 0;
}

static int is_non_root_on_close(lua_State *L, GCheader *o)
{
  if (o == &L->gch || o == &G(L)->gch) {
    return 0;
  }
  return 1;
}

static void move_matching_objects_to(lua_State *L, GCheader *src, GCheader *target,
  int (*func)(lua_State *L, GCheader *o))
{
  GCheader *o, *next;

  next = src->next;
  if (next == NULL) return;
  while (next != src) {
    o = next;
    next = o->next;

    lua_assert(o->owner == L->heapid);

    if (func(L, o)) {
      append_list(target, o);
    }
  }
}

static void reclaim_white(lua_State *L, int final_close)
{
  GCheader *o;
//  if (L->heapid != 0) walk_gch_list(G(L), L->White, "Will reclaim");

  while (L->White->next != L->White) {

    o = L->White->next;

#if HAVE_VALGRIND && DEBUG_ALLOC
    VALGRIND_PRINTF_BACKTRACE(
      "reclaim %s at %p (marked=%x xref=%d isxref=%d)\n",
      lua_typename(NULL, o->tt), o, o->marked,
      o->xref, G(L)->isxref);
#endif

    lua_assert(o->owner == L->heapid);
    lua_assert(final_close == 1 || o->xref == G(L)->notxref);
    lua_assert(o->ref == 0);
    unlink_list(o);

    o->marked |= FREEDBIT;

    switch (o->tt) {
      case LUA_TPROTO:
        luaF_freeproto(G(L), gco2p(o));
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
          luaM_freememG(G(L), c, size);
          break;
        }
      case LUA_TUPVAL:
        luaF_freeupval(G(L), gco2uv(o));
        break;
      case LUA_TTABLE:
        luaH_free(G(L), gco2h(o));
        break;
      case LUA_TTHREAD:
        lua_assert(gco2th(o) != G(L)->mainthread);
        luaE_freethread(G(L), gco2th(o));
        break;
      case LUA_TSTRING:
        luaM_freememG(G(L), o, sizestring(gco2ts(o)));
        break;
      case LUA_TGLOBAL:
        /* skip; someone else will clear this out */
        break;
      case LUA_TUSERDATA:
        luaM_freememG(G(L), o, sizeudata(gco2u(o)));
        break;

      default:
#if HAVE_VALGRIND
        VALGRIND_PRINTF_BACKTRACE("reclaim %s not implemented\n",
          lua_typename(NULL, o->tt));
#endif
        lua_assert(0);
    }
  }
}


static void run_finalize(lua_State *L)
{
  while (L->Finalize.next != &L->Finalize) {
    GCheader *o = L->Finalize.next;
    Udata *ud;
    const TValue *tm;

    lua_assert(o->owner == L->heapid);
    unlink_list(o);

    ud = rawgco2u(o);
    ud->uv.finalized = 1;
    tm = gfasttm(G(L), gch2h(ud->uv.metatable), TM_GC);
    if (tm) {
      /* FIXME: prevent GC, debug hooks during finalizer */
      VALGRIND_PRINTF_BACKTRACE("finalizing udata %p\n", ud);

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
    /* make it grey; will be freed next cycle */
    mark_object(L, o);

  }
}


static void finalize_all_local(lua_State *L)
{
  move_matching_objects_to(L, L->Black, &L->Finalize, is_finalizable_on_close);
  move_matching_objects_to(L, L->White, &L->Finalize, is_finalizable_on_close);
  move_matching_objects_to(L, &L->Grey, &L->Finalize, is_finalizable_on_close);
  move_matching_objects_to(L, &L->Weak, &L->Finalize, is_finalizable_on_close);
  run_finalize(L);
}

static void whitelist_non_root(lua_State *L)
{
  move_matching_objects_to(L, L->Black, L->White, is_non_root_on_close);
  move_matching_objects_to(L, &L->Grey, L->White, is_non_root_on_close);
  move_matching_objects_to(L, &L->Weak, L->White, is_non_root_on_close);
  unlink_list(&L->gch);
  unlink_list(&G(L)->gch);
}

static void propagate(lua_State *L)
{
  GCheader *o;

  while (L->Grey.next != &L->Grey) {
    o = L->Grey.next;

    blacken_object(L, o);
  }
}

static void check_references(lua_State *L)
{
  GCheader *o, *next;

  next = L->White->next;
  while (next != L->White) {
    o = next;
    next = o->next;

    lua_assert(!is_black(L, o));
    lua_assert(o->owner == L->heapid);

    /* anything explicitly ref'd from C, or that might be
     * ref'd externally is grey */
    if (o->ref || o->xref != G(L)->notxref) {
      mark_object(L, o);
      continue;
    }

    if (o->tt == LUA_TUSERDATA && !gco2u(o)->finalized) {
      append_list(&L->Finalize, o);
      continue;
    }
  }
}

static void fixup_weak_refs(lua_State *L)
{
  GCheader *o;

  while (L->Weak.next != &L->Weak) {
    Table *h;
    int j, weakkey = 0, weakvalue = 0;
    const TValue *mode;

    /* Weak entries are Black, so move them over */
    o = L->Weak.next;
    lua_assert(o->owner == L->heapid);
    append_list(L->Black, o);

    h = gco2h(o);
    luaH_wrlock(L, h);

    mode = gfasttm(G(L), gch2h(h->metatable), TM_MODE);
    if (mode && ttisstring(mode)) {
      weakkey = (strchr(svalue(mode), 'k') != NULL);
      weakvalue = (strchr(svalue(mode), 'v') != NULL);
    }
    if (weakvalue) {
      j = h->sizearray;
      while (j--) {
        if (iscollected(L, &h->array[j], 0)) {
          setnilvalue(&h->array[j]);
        }
      }
    }
    if (weakkey) {
      j = sizenode(h);
      while (j--) {
        Node *n = gnode(h, j);

        if (!ttisnil(gval(n)) && (
              iscollected(L, key2tval(n), 1) ||
              iscollected(L, gval(n), 0))) {
          setnilvalue(gval(n));
          removeentry(n);
        }
      }
    }
    luaH_unlock(L, h);
  }
}

static void sanity_check_mark_status(lua_State *L)
{
  GCheader *o;

  /* These lists must be empty */
  lua_assert(L->Finalize.next == &L->Finalize);
  lua_assert(L->Weak.next == &L->Weak);
  lua_assert(L->Black->next == L->Black);
  lua_assert(L->Grey.next == &L->Grey);

  for (o = L->White->next; o != L->White; o = o->next) {
    lua_assert((o->marked & FREEDBIT) == 0);
    lua_assert(!is_black(L, o));
    lua_assert(o->owner == L->heapid);
  }
}

static void local_collection(lua_State *L)
{
  if (L->in_gc) {
    return; // happens during finalizers
  }
  L->in_gc = 1;

  prep_list(&L->B0);
  prep_list(&L->B1);
  prep_list(&L->Grey);
  prep_list(&L->Finalize);
  prep_list(&L->Weak);

  luaE_flush_stringtable(L);

  /* mark roots */
  L->gch.marked = (L->gch.marked & ~BLACKBIT) | (!L->black);
  mark_object(L, &L->gch);
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

  lua_assert(L->White->next == L->White); // White list should be empty now

  /* White is the new Black */
  L->black = !L->black;
  if (L->Black == &L->B0) {
    L->Black = &L->B1;
    L->White = &L->B0;
  } else {
    L->Black = &L->B0;
    L->White = &L->B1;
  }

  sanity_check_mark_status(L);
//  if (L->heapid > 0) walk_gch_list(G(L), L->White, "live objects after collection");

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

static void global_trace(lua_State *L, GCheader *list)
{
  GCheader *obj;

  if (list->next == NULL) {
    return;
  }

  for (obj = list->next; obj != list; obj = obj->next) {
    global_trace_obj(L, &L->gch, obj);
  }
}

/* Global collection must only use async-signal safe functions,
 * or it will lead to a deadlock (especially in printf) */
static void global_collection(lua_State *L)
{
  lua_State *l;
  pthread_t *threads;
  int howmany;

  VALGRIND_PRINTF_BACKTRACE("stopping world\n");
  if (!try_lock_all_threads()) {
    return;
  }
  threads = stop_all_threads(&howmany);
  VALGRIND_PRINTF_BACKTRACE("STOP'd threads; setting stopped flag, flipping xref\n");
  G(L)->stopped = 1;

  /* flip sense of definitive xref bit */
  if (G(L)->isxref == 1) {
    G(L)->isxref = 3;
    G(L)->notxref = 2;
  } else {
    G(L)->isxref = 1;
    G(L)->notxref = 0;
  }

  /* now trace all objects and fix the xref bit */
  for (l = G(L)->mainthread; l; l = l->next) {
    VALGRIND_PRINTF_BACKTRACE("traverse lua_State=%p\n", l);
    /* force a trace of the thread itself */
    l->gch.xref = G(L)->isxref;
    traverse_object(l, &l->gch, global_trace_obj);

    global_trace(l, &l->Grey);
    global_trace(l, l->White);
    global_trace(l, l->Black);
    global_trace(l, &l->Weak);
  }

  G(L)->stopped = 0;

  resume_threads(threads, howmany);

  unlock_all_threads();
  VALGRIND_PRINTF_BACKTRACE("started world\n");
}

void luaC_checkGC(lua_State *L)
{
//  if (L->heapid) return;
  local_collection(L);
  if (!L->in_gc) {
    global_collection(L);
  }
}

/* The semantics of lua_close are to free up everything associated
 * with the lua_State, including others states and globals */
LUA_API void lua_close (lua_State *L)
{
  pthread_t ct;
  global_State *g = G(L);
  GCheader *o, *n;
  Table *reg;
  unsigned int udata;

  /* only the main thread can be closed */
  lua_assert(L == G(L)->mainthread);

  /* finalize everything that needs it */
  finalize_all_local(L);
  whitelist_non_root(L);
  /* kill-em-all! */
  reclaim_white(L, 1);

  luaE_freethread(G(L), L);

  g->alloc(g->allocdata, g, sizeof(*g), 0);
}

void lua_assert_fail(const char *expr, GCheader *obj, const char *file, int line)
{
  if (obj) {
    VALGRIND_PRINTF_BACKTRACE(
      "Assertion %s failed\nobj=%p owner=%x tt=%d ref=%d marked=%x xref=%x\n",
      expr, obj, obj->owner, obj->tt, obj->ref, obj->marked, obj->xref);
  } else {
    VALGRIND_PRINTF_BACKTRACE("Assertion %s failed\n", expr);
  }
  fprintf(stderr, "Assertion %s failed at %s:%d\n", expr, file, line);
  abort();
}

/* vim:ts=2:sw=2:et:
 */
