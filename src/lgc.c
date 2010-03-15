/*
** $Id: lgc.c,v 2.38.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE
#define DEBUG_ALLOC 0

#include "thrlua.h"

static inline void init_list(GCheader *head)
{
  head->next = head;
  head->prev = head;
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
  unlink_list(obj);
  obj->next = head;
  obj->prev = head->prev;
  obj->prev->next = obj;
  head->prev = obj;
}

static void walk_gch_list(global_State *g, GCheader *list, const char *caption)
{
  GCheader *o;
  printf("%s\n", caption);
  printf("head=%p head->next=%p head->prev=%p\n", list, list->next, list->prev);
  for (o = list->next; o != list; o = o->next) {
    printf(" >> %s at %p ref=%d\n",
      lua_typename(NULL, o->tt), o, o->ref);
  }
}


static void removeentry(Node *n)
{
  lua_assert(ttisnil(gval(n)));
  if (iscollectable(gkey(n)))
    setttype(key2tval(n), LUA_TDEADKEY);  /* dead key; remove it */
}

static inline void o_push(global_State *g, struct gc_stack *stk, GCheader *o)
{
  if (!o) {
    return;
  }
  if (stk->items + 1 >= stk->alloc) {
    unsigned int nsize = stk->alloc ? stk->alloc * 2 : 1024;
    GCheader *p;

    while (nsize < stk->items + 1) {
      nsize *= 2;
    }

    stk->obj = luaM_reallocG(g, stk->obj,
      stk->alloc * sizeof(GCheader*),
      nsize * sizeof(GCheader*));
    stk->alloc = nsize;
  }

  stk->obj[stk->items++] = o;
}

static void o_free(global_State *g, struct gc_stack *stk)
{
  luaM_reallocG(g, stk->obj, stk->alloc * sizeof(GCheader*), 0);
  memset(stk, 0, sizeof(*stk));
}

static inline void o_clear(struct gc_stack *stk)
{
  stk->items = 0;
}

static unsigned int log_or_count(global_State *g, 
  GCheader *o, struct gc_log_buffer *buf)
{
  unsigned int room = 0;

  switch (o->tt) {
    case LUA_TSTRING:
    case LUA_TUPVAL:
    case LUA_TTHREAD:
      /* has no contents; nothing to log */
      return 0;

    case LUA_TUSERDATA:
      {
        Udata *ud = rawgco2u(o);
        if (ud->uv.metatable) {
          if (buf) {
            buf->obj[buf->items++] = (GCheader*)ud->uv.metatable;
          }
          room++;
        }
        if (ud->uv.env) {
          if (buf) {
            buf->obj[buf->items++] = (GCheader*)ud->uv.env;
          }
          room++;
        }
        break;
      }
    case LUA_TTABLE:
      {
        Table *h = gco2h(o);
        int i, weakkey = 0, weakvalue = 0;
        const TValue *mode;

        /* don't need to lock; the caller of the write barrier has a lock */
        if (h->metatable) {
          if (buf) {
            buf->obj[buf->items++] = (GCheader*)h->metatable;
          }
          room++;
        }
        mode = gfasttm(g, gch2h(h->metatable), TM_MODE);
        if (mode && ttisstring(mode)) {
          weakkey = (strchr(svalue(mode), 'k') != NULL);
          weakvalue = (strchr(svalue(mode), 'v') != NULL);
        }
        if (!weakvalue) {
          i = h->sizearray;
          while (i--) {
            if (iscollectable(&h->array[i])) {
              if (buf) {
                buf->obj[buf->items++] = gcvalue(&h->array[i]);
              }
              room++;
            }
          }
        }
        i = sizenode(h);
        while (i--) {
          Node *n = gnode(h, i);

          if (!ttisnil(gval(n))) {
            lua_assert(!ttisnil(gkey(n)));
            if (!weakkey) {
              if (iscollectable(gkey(n))) {
                if (buf) {
                  buf->obj[buf->items++] = gcvalue(gkey(n));
                }
                room++;
              }
            }
            if (!weakvalue) {
              if (iscollectable(gval(n))) {
                if (buf) {
                  buf->obj[buf->items++] = gcvalue(gval(n));
                }
                room++;
              }
            }
          }
        }
        break;
      }
    case LUA_TFUNCTION:
      {
        Closure *cl = gco2cl(o);
        int i;

        if (cl->c.isC) {
          for (i = 0; i < cl->c.nupvalues; i++) {
            if (iscollectable(&cl->c.upvalue[i])) {
              if (buf) {
                buf->obj[buf->items++] = gcvalue(&cl->c.upvalue[i]);
              }
              room++;
            }
          }
        } else {
          lua_assert(cl->l.nupvalues == cl->l.p->nups);
          if (buf) {
            buf->obj[buf->items++] = (GCheader*)cl->l.p;
          }
          room++;
          for (i = 0; i < cl->l.nupvalues; i++) {
            if (buf) {
              buf->obj[buf->items++] = (GCheader*)cl->l.upvals[i];
            }
            room++;
          }
        }
        break;
      }
    case LUA_TPROTO:
      {
        Proto *f = gco2p(o);
        int i;

        if (f->source) {
          if (buf) {
            buf->obj[buf->items++] = (GCheader*)f->source;
          }
          room++;
        }
        for (i = 0; i < f->sizek; i++) {
          if (iscollectable(&f->k[i])) {
            if (buf) {
              buf->obj[buf->items++] = gcvalue(&f->k[i]);
            }
            room++;
          }
        }
        for (i = 0; i < f->sizeupvalues; i++) {
          if (f->upvalues[i]) {
            if (buf) {
              buf->obj[buf->items++] = (GCheader*)f->upvalues[i];
            }
            room++;
          }
        }
        for (i = 0; i < f->sizep; i++) {
          if (f->p[i]) {
            if (buf) {
              buf->obj[buf->items++] = (GCheader*)f->p[i];
            }
            room++;
          }
        }
        for (i = 0; i < f->sizelocvars; i++) {
          if (f->locvars[i].varname) {
            if (buf) {
              buf->obj[buf->items++] = (GCheader*)f->locvars[i].varname;
            }
            room++;
          }
        }
        break;
      }
    default:
      abort();
  }
  return room + 1;
}

static void log_object(global_State *g, thr_State *pt, GCheader *o)
{
  unsigned int room;
  struct gc_log_buffer *buf;
  unsigned int bi;

  /* first determine how much room is needed in the log buffer; we need
   * one for the object itself */
  room = log_or_count(g, o, NULL);
  if (room == 0) {
    return;
  }

  /* do we have enough room in an existing log buffer? */
  for (buf = pt->log_buf; buf; buf = buf->next) {
    if (buf->alloc - buf->items >= room) {
      break;
    }
  }
  if (buf == NULL) {
    /* we didn't have enough room so we need to create a new buffer */
    unsigned int size = 128;

    while (size < room) {
      size *= 2;
    }
    buf = luaM_reallocG(g, NULL, 0,
      sizeof(*buf) + (size - 1) * sizeof(GCheader*));
    buf->next = pt->log_buf;
    buf->items = 0;
    buf->alloc = size;
    pt->log_buf = buf;
  }

  /* record starting offset in buf; we'll need this if we roll back */
  bi = buf->items;

  /* now we're good to log properties */
  log_or_count(g, o, buf);

  /* terminate the log entry with our own object pointer, but set
   * the LSB to indicate that it is a terminator.  No valid pointer
   * address will ever have the LSB set, so we're safe to do such a thing.
   */
  buf->obj[buf->items++] = (GCheader*)(((intptr_t)o) | 0x1);

  /* after all this effort, it may be the case that another thread has
   * already logged this object; make a final check before we commit.
   * FIXME: could CAS this, but may not be needed, assuming that the 
   * modifying thread should already hold a write lock */
  if (o->logptr == NULL) {
    o->logptr = &buf->obj[bi];
  } else {
    /* roll back the log */
    buf->items = bi;
  }
}

void luaC_writebarrierov(global_State *g, GCheader *object,
  GCheader **lvalue, const TValue *rvalue)
{
  thr_State *pt = get_per_thread(g);
  GCheader *ro = gcvalue(rvalue);

  pthread_mutex_lock(&pt->handshake);
  if (pt->trace && object->marked != pt->alloc_color &&
      object->logptr == NULL) {
    log_object(g, pt, (GCheader*)object);
  }
  *lvalue = ro;
  if (pt->snoop && ro != NULL) {
    o_push(g, &pt->snoop_buf, ro);
  }
  pthread_mutex_unlock(&pt->handshake);
}


void luaC_writebarriervv(global_State *g, GCheader *object,
  TValue *lvalue, const TValue *rvalue)
{
  thr_State *pt = get_per_thread(g);
  GCheader *ro = iscollectable(rvalue) ? gcvalue(rvalue) : NULL;

  pthread_mutex_lock(&pt->handshake);
  if (pt->trace && object->marked != pt->alloc_color &&
      object->logptr == NULL) {
    log_object(g, pt, (GCheader*)object);
  }
  lvalue->value = rvalue->value;
  lvalue->tt = rvalue->tt;
  if (pt->snoop && ro != NULL) {
    o_push(g, &pt->snoop_buf, ro);
  }
  pthread_mutex_unlock(&pt->handshake);
}

void luaC_writebarrier(global_State *g, GCheader *object,
  GCheader **lvalue, GCheader *rvalue)
{
  thr_State *pt = get_per_thread(g);

  pthread_mutex_lock(&pt->handshake);
  if (pt->trace && object->marked != pt->alloc_color &&
      object->logptr == NULL) {
    log_object(g, pt, (GCheader*)object);
  }
  *lvalue = rvalue;
  if (pt->snoop && rvalue != NULL) {
    o_push(g, &pt->snoop_buf, (GCheader*)rvalue);
  }
  pthread_mutex_unlock(&pt->handshake);
}

static inline void mark_value(global_State *g, TValue *val)
{
  checkconsistency(val);
  if (iscollectable(val)) {
    o_push(g, &g->mark_set, gcvalue(val));
  }
}

static void mark_object(global_State *g, GCheader *o)
{
  int i;

  if (o->marked == g->white) {
    if (o->logptr) {
      /* use the logged snapshot */
      GCheader **log = o->logptr;

      while (1) {
        GCheader *v = *log;
        if (((intptr_t)v) & 0x1) {
          /* terminated with my own object pointer */
          break;
        }
        o_push(g, &g->mark_set, v);
        log++;
      }

    } else {
      /* not changed since collection started; use
       * object values themselves */


      switch (o->tt) {
        case LUA_TSTRING:
          /* has no contents */
          break;

        case LUA_TUSERDATA:
          {
            Udata *ud = rawgco2u(o);
            if (ud->uv.metatable) {
              o_push(g, &g->mark_set, (GCheader*)ud->uv.metatable);
            }
            if (ud->uv.env) {
              o_push(g, &g->mark_set, (GCheader*)ud->uv.env);
            }
            break;
          }
        case LUA_TUPVAL:
          {
            UpVal *uv = gco2uv(o);
            mark_value(g, uv->v);
            break;
          }
        case LUA_TTABLE:
          {
            Table *h = gco2h(o);
            int weakkey = 0, weakvalue = 0;
            const TValue *mode;

            luaH_rdlock(g, h);
            if (h->metatable) {
              o_push(g, &g->mark_set, h->metatable);
            }
            mode = gfasttm(g, gch2h(h->metatable), TM_MODE);
            if (mode && ttisstring(mode)) {
              weakkey = (strchr(svalue(mode), 'k') != NULL);
              weakvalue = (strchr(svalue(mode), 'v') != NULL);
              if (weakkey || weakvalue) {
                o_push(g, &g->weak_set, (GCheader*)h);
              }
            }
            i = h->sizearray;
            while (i--) {
              /* strings are never weak */
              if (!weakvalue || ttisstring(&h->array[i])) {
                mark_value(g, &h->array[i]);
              }
            }
            i = sizenode(h);
            while (i--) {
              Node *n = gnode(h, i);

              if (!ttisnil(gkey(n))) {
                /* strings are never weak */
                if (!weakkey || ttisstring(gkey(n))) {
                  mark_value(g, key2tval(n));
                }
              }
              if (!ttisnil(gval(n))) {
                /* strings are never weak */
                if (!weakvalue || ttisstring(gval(n))) {
                  mark_value(g, gval(n));
                }
              }
            }
            luaH_unlock(g, h);
            break;
          }

        case LUA_TFUNCTION:
          {
            Closure *cl = gco2cl(o);

            if (cl->c.isC) {
              o_push(g, &g->mark_set, cl->c.env);
              for (i = 0; i < cl->c.nupvalues; i++) {
                mark_value(g, &cl->c.upvalue[i]);
              }
            } else {
              lua_assert(cl->l.nupvalues == cl->l.p->nups);
              o_push(g, &g->mark_set, &cl->l.p->gch);
              o_push(g, &g->mark_set, cl->l.env);
              for (i = 0; i < cl->l.nupvalues; i++) {
                o_push(g, &g->mark_set, &cl->l.upvals[i]->gch);
              }
            }
            break;
          }
        case LUA_TTHREAD:
          {
            lua_State *th = gco2th(o);
            StkId sk;
            UpVal *uv;

            lua_lock(th);
            mark_value(g, &th->l_gt);
            mark_value(g, &th->env);
            mark_value(g, &th->tls);
            /* the corresponding global state */
            lua_assert(g == th->l_G);
            o_push(g, &g->mark_set, (GCheader*)th->l_G);
            for (sk = th->stack; sk < th->top; sk++) {
              mark_value(g, sk);
            }
            /* open upvalues also */
            for (uv = th->openupval.u.l.next;
                uv != &th->openupval; uv = uv->u.l.next) {
              o_push(g, &g->mark_set, (GCheader*)uv);
            }
            lua_unlock(th);
            break;
          }
        case LUA_TPROTO:
          {
            Proto *f = gco2p(o);

            if (f->source) {
              o_push(g, &g->mark_set, &f->source->tsv.gch);
            }
            for (i = 0; i < f->sizek; i++) {
              mark_value(g, &f->k[i]);
            }
            for (i = 0; i < f->sizeupvalues; i++) {
              if (f->upvalues[i]) {
                o_push(g, &g->mark_set, f->upvalues[i]);
              }
            }
            for (i = 0; i < f->sizep; i++) {
              if (f->p[i]) {
                o_push(g, &g->mark_set, &f->p[i]->gch);
              }
            }
            for (i = 0; i < f->sizelocvars; i++) {
              if (f->locvars[i].varname) {
                o_push(g, &g->mark_set, f->locvars[i].varname);
              }
            }
            break;
          }
        case LUA_TGLOBAL:
          o_push(g, &g->mark_set, &g->memerr->tsv.gch);
          for (i = 0; i < NUM_TAGS; i++) {
            if (g->mt[i]) {
              o_push(g, &g->mark_set, &g->mt[i]->gch);
            }
          }
          for (i = 0; i < TM_N; i++) {
            if (g->tmname[i]) {
              o_push(g, &g->mark_set, &g->tmname[i]->tsv.gch);
            }
          }
          mark_value(g, &g->l_registry);
          mark_value(g, &g->l_globals);
          o_push(g, &g->mark_set, &g->mainthread->gch);
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
    o->marked = g->black;
  }
}

static inline int iscollected(global_State *g, const TValue *o, int iskey)
{
  if (!iscollectable(o)) {
    return 0;
  }
  if (ttisstring(o)) {
    /* string are 'values' and are never weak */
    return 0;
  }
  if (g->white == gcvalue(o)->marked) {
    return 1;
  }
  /* NOTE: we differ from stock lua in that we don't preserve a weak
   * finalized udata key. */
  return 0;
}


/** walks the to_finalize list and finalizes and frees the objects
 * contained therein.
 */
static void finalize(global_State *g)
{
  GCheader *o;
  lua_State *L = NULL;
  int called_finalizer = 0;
  unsigned int size;

  pthread_mutex_lock(&g->collector_lock);
  /* first we make passes to find userdata that need finalizing; we
   * do those first before freeing things, as udata may refer to tables
   * that we're about to finalize in this batch */
  for (o = g->to_finalize.next; o != &g->to_finalize; o = o->next) {
    if (o->tt == LUA_TUSERDATA && !gco2u(o)->finalized) {
      Udata *ud;
      const TValue *tm;

      called_finalizer++;

      /* NOTE: we differ from stock lua in that we destroy the udata
       * in an unspecified order and will free the udata immediately
       * after invoking its __gc method, rather than on the next
       * collection cycle */
      ud = rawgco2u(o);
      ud->uv.finalized = 1;
      tm = gfasttm(g, gch2h(ud->uv.metatable), TM_GC);
      if (tm) {
        if (!L) {
          /* create a lua state to execute the finalizer */
          L = luaE_newthreadG(g);
          L->allowhook = 0;
        }

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

#if DEBUG_ALLOC
  walk_gch_list(g, &g->to_finalize, "to_finalize");
#endif
  while (g->to_finalize.next != &g->to_finalize) {
    o = g->to_finalize.next;
    lua_assert(o != NULL);
    unlink_list(o);

#if HAVE_VALGRIND && DEBUG_ALLOC
    VALGRIND_PRINTF_BACKTRACE("finalize %s at %p\n",
      lua_typename(NULL, o->tt), o);
#endif

    switch (o->tt) {
      case LUA_TPROTO:
        luaF_freeproto(g, gco2p(o));
        break;
      case LUA_TFUNCTION:
        {
          Closure *c = gco2cl(o);
          size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
            sizeLclosure(c->l.nupvalues);
          luaM_freememG(g, c, size);
          break;
        }
      case LUA_TUPVAL:
        luaF_freeupval(g, gco2uv(o));
        break;
      case LUA_TTABLE:
        luaH_free(g, gco2h(o));
        break;
      case LUA_TTHREAD:
        luaE_freethread(g, gco2th(o));
        break;
      case LUA_TSTRING:
        luaM_freememG(g, o, sizestring(gco2ts(o)));
        break;
      case LUA_TGLOBAL:
        /* skip; someone else will clear this out */
        break;
      case LUA_TUSERDATA:
        luaM_freememG(g, o, sizeudata(gco2u(o)));
        break;

      default:
#if HAVE_VALGRIND
        VALGRIND_PRINTF_BACKTRACE("finalize %s not implemented\n",
          lua_typename(NULL, o->tt));
#endif
        lua_assert(0);
    }
  }
  pthread_mutex_unlock(&g->collector_lock);
}

/** collects garbage.
The theory of operation is via sliding views.

When this function is called, it initiates a collection cycle.  The cycle has a
number of stages and operates in a fashion that does not cause all threads to
block.

The first stage is to enable snooping across the threads that are or have
executed lua code.  Snooping tracks the rvalue of assignments to heap objects;
these objects are treated as roots by the collection algorithm.

The second stage is to enable tracing across the threads.  Heap objects that
are modified while tracing it turned on have their properties logged so that we
have a snapshot of the state prior to modification in the collection cycle.

The third stage is to invert the sense of black and white and is equivalent to
walking over all objects and marking them white.

The fourth stage is to determine the root objects; root objects are considered
black and are thus not collectable in this cycle.  In this implementation, root
objects are those that have a non-zero reference count; in other words, they
are explicitly referenced from C code.  Each thread maintains a list of heap
objects that it has allocated; the collector merges these into its global
"Heap" list and turns off snooping for each thread.

The fifth stage is to pull the snoop buffers of each thread; we are safe to do
so because we turned off snooping for each thread in the prior stage.  Objects
in the snoop buffer are conservatively regarded as roots on the grounds that
they were recently used.

The sixth stage is to walk the root objects and the objects they reference,
recursively, marking them black (non collectable).  During marking, if an
object is white and was traced to the trace buffer, instead of walking its
properties we walk the snapshot of its properties.  This is so that we walk a
consistent sliding view of the heap.

The seventh stage is to turn off tracing in each thread, as we have no further
need for trace data in this collection cycle.

At this point, we have a list of Heap objects that we can walk. The objects
that we walked as part of the marking stage are black and can stick around; the
rest are white and are placed into a finalization list.

The next step is to reset the trace buffers and tracing state of the
outstanding objects and threads.

And then the collection cycle is complete.  We return 0 in the case that there
were roots, signalling collect_all that it should stop looping, but otherwise
return the number of potentially collectable items.

This function does not free the collectable objects; use the finalize()
function to operate on the finalization list.

Weak tables: during the mark phase, any tables that we find that have weak
references are pulled out of the main heap list and placed on a weak list.
After marking is done, the weak list is walked and the weak tables are updated
to remove the references to the white objects.
*/

static unsigned int collect(global_State *g)
{
  thr_State *pt;
  unsigned int heap_size = 0;
  unsigned int swept = 0;
  GCheader *o, *nptr;
  int i;
  unsigned int nroots = 0;

  pthread_mutex_lock(&g->collector_lock);

  /* Stage 1: enable snoop */
  for (pt = g->all_threads; pt; pt = pt->next) {
    pthread_mutex_lock(&pt->handshake);
    pt->snoop = 1;
    pthread_mutex_unlock(&pt->handshake);
  }

  /* Stage 2: enable tracing */
  for (pt = g->all_threads; pt; pt = pt->next) {
    pthread_mutex_lock(&pt->handshake);
    pt->trace = 1;
    pthread_mutex_unlock(&pt->handshake);
  }

  /* Stage 3: white is the new black */
  g->black = 1 - g->black;
  g->white = 1 - g->white;
  
  /* prep the root set */
  o_clear(&g->mark_set);

  /* Stage 4: gather roots from threads */
  for (pt = g->all_threads; pt; pt = pt->next) {
    pthread_mutex_lock(&pt->handshake);
    /* update color for the thread */
    pt->alloc_color = g->black;
    pt->snoop = 0;

    /* take its object list */
    while (pt->olist.next != &pt->olist) {
      append_list(&g->the_heap, pt->olist.next);
    }
    pthread_mutex_unlock(&pt->handshake);

    /* its string table is also considered a root, but when we
     * are exiting, we want to clear it out */
    pthread_mutex_lock(&pt->strt.lock);
    for (i = 0; i < pt->strt.size; i++) {
      struct stringtable_node *n;

      if (g->exiting) {
        while (pt->strt.hash[i]) {
          n = pt->strt.hash[i];
          pt->strt.hash[i] = n->next;
          g->alloc(g->allocdata, n, sizeof(*n), 0);
        }
      } else {
        n = pt->strt.hash[i];
        while (n) {
          o_push(g, &g->mark_set, &n->str->tsv.gch);
//if (g->exiting) printf("+root strt %s %p\n", lua_typename(NULL, n->str->tsv.gch.tt), n->str);
          n = n->next;
        }
      }
    }
    pthread_mutex_unlock(&pt->strt.lock);
  }

  /* Identify roots in the heap; they have a non-zero ref count */
//printf("walking heap looking for roots\n");
  for (o = g->the_heap.next; o != &g->the_heap; o = o->next) {
    heap_size++;
    if (o->ref > 0) {
      o_push(g, &g->mark_set, o);
//if (g->exiting) printf("+root heap %s %p (next=%p prev=%p)\n", lua_typename(NULL, o->tt), o, o->next, o->prev);
    }
  }

  /* Stage 5: pull snoop buffers.  Since all threads now have snooping turned
   * off, we are the only thread that will be looking at them; no need to
   * lock the handshake lock */
  for (pt = g->all_threads; pt; pt = pt->next) {
    for (i = 0; i < pt->snoop_buf.items; i++) {
      o_push(g, &g->mark_set, pt->snoop_buf.obj[i]);
//if (g->exiting) printf("+root snoop %s %p\n", lua_typename(NULL, pt->snoop_buf.obj[i]->tt), pt->snoop_buf.obj[i]);
    }
    o_clear(&pt->snoop_buf);
  }

  /* Stage 6: mark objects black; seed the marking stack with the set
   * of root objects */
  nroots = g->mark_set.items;
  while (g->mark_set.items) {
    o = g->mark_set.obj[--g->mark_set.items];
    mark_object(g, o);
  }

  /* Stage 7: turn off tracing */
  for (pt = g->all_threads; pt; pt = pt->next) {
    pthread_mutex_lock(&pt->handshake);
    pt->trace = 0;
    pthread_mutex_unlock(&pt->handshake);
  }

  /* Stage 8: walk the heap; all white objects are garbage */
  o = g->the_heap.next;
  while (o != &g->the_heap) {
    if (o->marked == g->white) {
      swept++;

      /* move to finalize list */
      nptr = o->next;
      append_list(&g->to_finalize, o);
      o = nptr;
    } else {
      o = o->next;
    }
  }

  /* Stage 9: clean up collection/trace state.
   * Tracing is turned off; no locks are needed */
  for (pt = g->all_threads; pt; pt = pt->next) {
    /* for each logged object, clear its logptr.
     * We also want to free log chains, leaving one spare for
     * future logging */
    struct gc_log_buffer *spare = NULL;

    while (pt->log_buf) {
      struct gc_log_buffer *buf = pt->log_buf;
      pt->log_buf = buf->next;

      for (i = 0; i < buf->items; i++) {
        o = (GCheader*)(((intptr_t)buf->obj[i]) & ~0x1);
        if (o) {
          o->logptr = NULL;
        }
      }

      if (spare == NULL && buf->alloc == 128) {
        spare = buf;
        spare->items = 0;
      } else {
        luaM_reallocG(g, buf,
          sizeof(*buf) + (buf->alloc - 1) * sizeof(GCheader*), 0);
      }
    }
    pt->log_buf = spare;
  }

  /* Stage 10: fixup weak table references to remove refs to white objects */
  for (i = 0; i < g->weak_set.items; i++) {
    Table *h = gco2h(g->weak_set.obj[i]);
    int j, weakkey = 0, weakvalue = 0;
    const TValue *mode;

    luaH_wrlock(g, h);
    if (h->metatable) {
      o_push(g, &g->mark_set, (GCheader*)h->metatable);
    }
    mode = gfasttm(g, gch2h(h->metatable), TM_MODE);
    if (mode && ttisstring(mode)) {
      weakkey = (strchr(svalue(mode), 'k') != NULL);
      weakvalue = (strchr(svalue(mode), 'v') != NULL);
    }
    if (!weakvalue) {
      j = h->sizearray;
      while (j--) {
        if (iscollected(g, &h->array[j], 0)) {
          setnilvalue(&h->array[j]);
        }
      }
    }
    j = sizenode(h);
    while (j--) {
      Node *n = gnode(h, j);

      if (!ttisnil(gval(n)) && (
          iscollected(g, key2tval(n), 1) ||
          iscollected(g, gval(n), 0))) {
        setnilvalue(gval(n));
        removeentry(n);
      }
    }
    luaH_unlock(g, h);
  }
  o_clear(&g->weak_set);

  /* Stage 11: if string tables are getting large, shrink them down */
  for (pt = g->all_threads; pt; pt = pt->next) {
    pthread_mutex_lock(&pt->strt.lock);
    if (pt->strt.nuse < cast(uint32_t, pt->strt.size/4) &&
        pt->strt.size > MINSTRTABSIZE*2)
      luaS_resize(g, &pt->strt, pt->strt.size/2);  /* table is too big */
    pthread_mutex_unlock(&pt->strt.lock);
  }

  /* collection cycle complete */
  pthread_mutex_unlock(&g->collector_lock);

  /* return 0 to indicate that collect_all does not need to loop;
   * it means that there are roots */
//printf("nroots=%d heap_size=%d swept=%d\n", nroots, heap_size, swept);
  return nroots ? 0 : heap_size - swept;
}

static void collect_all(global_State *g)
{
  int heap = 0;
  do {
    if (heap) {
      sleep(1);
    }
    heap = collect(g);
    finalize(g);
  } while (heap);
}

void luaC_fullgc (lua_State *L)
{
  lua_unlock(L);
  collect_all(G(L));
  lua_lock(L);
}

/** The collector thread collects every second or when woken up
 * to collect */
static void *collector(void *ptr)
{
  global_State *g = ptr;

  while (!g->exiting) {
    struct timespec deadline;
    int ret;

    memset(&deadline, 0, sizeof(deadline));
#ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#else
    deadline.tv_sec = time(NULL);
#endif
    deadline.tv_sec += 10;
    pthread_mutex_lock(&g->collector_lock);
    ret = pthread_cond_timedwait(&g->gc_cond, &g->collector_lock, &deadline);
    if (ret == 0 || ret == ETIMEDOUT) {
      pthread_mutex_unlock(&g->collector_lock);
    }
    if (g->exiting) {
      /* let the exiting thread do the final collection */
      return 0;
    }
    collect_all(g);
  }
  return 0;
}

static void tls_dtor(void *ptr)
{
  thr_State *pt = ptr;
  global_State *g = pt->g;
  int i;
  struct stringtable_node *n;

  pthread_mutex_lock(&g->collector_lock);
  if (pt->prev) {
    pt->prev->next = pt->next;
  } else {
    g->all_threads = pt->next;
  }
  if (pt->next) {
    pt->next->prev = pt->prev;
  }
  /* take its object list */
  while (pt->olist.next != &pt->olist) {
    append_list(&g->the_heap, pt->olist.next);
  }
  pthread_mutex_unlock(&g->collector_lock);

  pthread_mutex_destroy(&pt->handshake);
  luaZ_freebuffer(g, &pt->buff);
  for (i = 0; i < pt->strt.size; i++) {
    while (pt->strt.hash[i]) {
      n = pt->strt.hash[i];
      pt->strt.hash[i] = n->next;
      luaM_freeG(g, n);
    }
  }
  luaM_freearrayG(g, pt->strt.hash, pt->strt.size, TString *);
  if (pt->log_buf) {
    luaM_reallocG(g, pt->log_buf,
        sizeof(*pt->log_buf) + (pt->log_buf->alloc - 1)
        * sizeof(GCheader*), 0);
  }
  o_free(g, &pt->snoop_buf);
  luaM_freeG(g, pt);
}

thr_State *luaC_init_pt(global_State *g)
{
  thr_State *pt;
  pthread_mutexattr_t mattr;

  pt = g->alloc(g->allocdata, NULL, 0, sizeof(*pt));
  memset(pt, 0, sizeof(*pt));
  pt->g = g;
  init_list(&pt->olist);

  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&pt->handshake, &mattr);
  pthread_mutex_init(&pt->strt.lock, &mattr);
  pthread_mutexattr_destroy(&mattr);

  pthread_setspecific(g->tls_key, pt);
  luaZ_initbuffer(NULL, &pt->buff);

  pt->strt.hash = g->alloc(g->allocdata, NULL, 0,
                    MINSTRTABSIZE * sizeof(struct stringtable_node*));
  memset(pt->strt.hash, 0, MINSTRTABSIZE * sizeof(struct stringtable_node*));
  pt->strt.size = MINSTRTABSIZE;
  
  pthread_mutex_lock(&g->collector_lock);
  /* new objects are created in the current black */
  pt->alloc_color = g->black;
  pt->next = g->all_threads;
  if (pt->next) {
    pt->next->prev = pt;
  }
  g->all_threads = pt;
  pthread_mutex_unlock(&g->collector_lock);

  return pt;
}

global_State *luaC_newglobal(lua_Alloc alloc, void *ud)
{
  global_State *g;
  pthread_condattr_t cattr;
  pthread_mutexattr_t mattr;

  g = alloc(ud, NULL, 0, sizeof(*g));
  if (!g) {
    return NULL;
  }
  memset(g, 0, sizeof(*g));
  g->gch.tt = LUA_TGLOBAL;
 
  pthread_key_create(&g->tls_key, tls_dtor);

  pthread_condattr_init(&cattr);
#ifdef CLOCK_MONOTONIC
  pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
#endif
  pthread_cond_init(&g->gc_cond, &cattr);
  pthread_condattr_destroy(&cattr);

  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&g->collector_lock, &mattr);
  pthread_mutexattr_destroy(&mattr);

  g->black = 0;
  /* color is implicitly black as we are initialized to zero */
  g->white = 1;
  g->alloc = alloc;
  g->allocdata = ud;

  init_list(&g->to_finalize);
  /* we are the original object on the heap */
  init_list(&g->the_heap);
  append_list(&g->the_heap, &g->gch);

  pthread_create(&g->collector_thread, NULL, collector, g);

  return g;
}

void *luaC_newobj(global_State *g, lu_byte tt)
{
  GCheader *o;
  thr_State *pt = get_per_thread(g);

  switch (tt) {
#define NEWIMPL(a, b) \
    case a: \
      pthread_mutex_lock(&pt->handshake); \
      o = luaM_reallocG(g, NULL, 0, sizeof(b)); \
      memset(o, 0, sizeof(b)); \
      o->tt = a; \
      o->marked = pt->alloc_color; \
      append_list(&pt->olist, o); \
      pthread_mutex_unlock(&pt->handshake); \
      break
    NEWIMPL(LUA_TUPVAL, UpVal);
    NEWIMPL(LUA_TPROTO, Proto);
    NEWIMPL(LUA_TTABLE, Table);
    NEWIMPL(LUA_TTHREAD, lua_State);
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

void *luaC_newobjv(global_State *g, lu_byte tt, size_t size)
{
  GCheader *o = NULL;
  thr_State *pt = get_per_thread(g);

  switch (tt) {
#undef NEWIMPL
#define NEWIMPL(a, b) \
    case a: \
      pthread_mutex_lock(&pt->handshake); \
      o = luaM_reallocG(g, NULL, 0, size); \
      memset(o, 0, size); \
      o->tt = a; \
      o->marked = pt->alloc_color; \
      append_list(&pt->olist, o); \
      pthread_mutex_unlock(&pt->handshake); \
      break
    NEWIMPL(LUA_TFUNCTION, Closure);
    NEWIMPL(LUA_TSTRING, TString);
    NEWIMPL(LUA_TUSERDATA, TString);
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

/* The semantics of lua_close are to free up everything associated
 * with the lua_State, including others states and globals */
LUA_API void lua_close (lua_State *L)
{
  pthread_t ct;
  global_State *g = G(L);
  GCheader *o, *n;
  Table *reg;
  thr_State *pt = get_per_thread(g);
  unsigned int udata;
  
  /* only the main thread can be closed */
  lua_assert(L == G(L)->mainthread);

  /* keep a ref on the global state */
  scpt_atomic_inc(&g->gch.ref);

  /* persuade collector to exit */
  g->exiting = 1;
  pthread_mutex_lock(&g->collector_lock);
  pthread_cond_signal(&g->gc_cond);
  pthread_mutex_unlock(&g->collector_lock);
  /* wait for collector to exit */
  pthread_join(g->collector_thread, NULL);
  collect_all(g);

//  printf("**** all collectable\n");
  /* now everything is collectable; walk the heap and finalize all userdata */
  do {
    udata = 0;
    o = g->the_heap.prev;
    while (o != &g->the_heap) {
      if (o->tt == LUA_TUSERDATA) {
        n = o->prev;
        udata++;
        append_list(&g->to_finalize, o);
        o = n;
      } else {
        o = o->prev;
      }
    }
    finalize(g);
  } while (udata);

  /* at this point, everything is garbage */
  unlink_list(&g->gch);
  while (g->the_heap.next != &g->the_heap) {
    append_list(&g->to_finalize, g->the_heap.next);
  }
  while (pt->olist.next != &pt->olist) {
    append_list(&g->to_finalize, pt->olist.next);
  }
  finalize(g);
#if DEBUG_ALLOC
  walk_gch_list(g, &g->the_heap, "the heap");
  walk_gch_list(g, &pt->olist, "olist");
#endif

  pthread_setspecific(g->tls_key, NULL);
  tls_dtor(pt);
  o_free(g, &g->mark_set);
  o_free(g, &g->weak_set);
  g->alloc(g->allocdata, g, sizeof(*g), 0);
}


/* vim:ts=2:sw=2:et:
 */
