/*
** $Id: lgc.c,v 2.38.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE
#define DEBUG_ALLOC 0

#include "thrlua.h"

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

static inline void set_xref(lua_State *L, GCheader *lval, GCheader *rval)
{
  if (lval->owner != rval->owner) {
    rval->xref = G(L)->isxref;
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

static inline void mark_value(lua_State *L, TValue *val)
{
  checkconsistency(val);
  if (iscollectable(val)) {
    mark_object(L, gcvalue(val));
  }
}


void luaC_writebarrierov(lua_State *L, GCheader *object,
  GCheader **lvalue, const TValue *rvalue)
{
  GCheader *ro = gcvalue(rvalue);

  if (ro) {
    set_xref(L, object, ro);
    mark_object(L, ro);
  }

  *lvalue = ro;
}


void luaC_writebarriervv(lua_State *L, GCheader *object,
  TValue *lvalue, const TValue *rvalue)
{
  GCheader *ro = iscollectable(rvalue) ? gcvalue(rvalue) : NULL;

  if (ro) {
    set_xref(L, object, ro);
    mark_object(L, ro);
  }

  lvalue->value = rvalue->value;
  lvalue->tt = rvalue->tt;
}

void luaC_writebarrier(lua_State *L, GCheader *object,
  GCheader **lvalue, GCheader *rvalue)
{
  if (rvalue) {
    set_xref(L, object, rvalue);
    mark_object(L, rvalue);
  }

  *lvalue = rvalue;
}

static void blacken_object(lua_State *L, GCheader *o)
{
  int i;
    
  lua_assert((o->marked & FREEDBIT) == 0);
  lua_assert(o->owner == L->heapid);
  o->marked = (o->marked & ~BLACKBIT) | L->black;

  switch (o->tt) {
    case LUA_TSTRING:
      /* has no contents */
      break;

    case LUA_TUSERDATA:
      {
        Udata *ud = rawgco2u(o);
        if (ud->uv.metatable) {
          mark_object(L, (GCheader*)ud->uv.metatable);
        }
        if (ud->uv.env) {
          mark_object(L, (GCheader*)ud->uv.env);
        }
        break;
      }
    case LUA_TUPVAL:
      {
        UpVal *uv = gco2uv(o);
        mark_value(L, uv->v);
        break;
      }
    case LUA_TTABLE:
      {
        Table *h = gco2h(o);
        int weakkey = 0, weakvalue = 0;
        const TValue *mode;

        luaH_rdlock(L, h); /* FIXME: deadlock a possibility */
        if (h->metatable) {
          mark_object(L, h->metatable);
        }
        mode = gfasttm(G(L), gch2h(h->metatable), TM_MODE);
        if (mode && ttisstring(mode)) {
          weakkey = (strchr(svalue(mode), 'k') != NULL);
          weakvalue = (strchr(svalue(mode), 'v') != NULL);
        }
        if (!weakvalue) {
          i = h->sizearray;
          while (i--) {
            mark_value(L, &h->array[i]);
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
              mark_value(L, gval(n));
            }
            if (!weakkey && iscollectable(gkey(n))) {
              mark_object(L, gcvalue(gkey(n)));
            }
          }
        }
        luaH_unlock(L, h);
        if (weakkey || weakvalue) {
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
          mark_object(L, cl->c.env);
          for (i = 0; i < cl->c.nupvalues; i++) {
            mark_value(L, &cl->c.upvalue[i]);
          }
        } else {
//          VALGRIND_PRINTF_BACKTRACE("tracing function %p proto %p\n", o, cl->l.p);
          lua_assert(cl->l.nupvalues == cl->l.p->nups);
          mark_object(L, &cl->l.p->gch);
          mark_object(L, cl->l.env);
          for (i = 0; i < cl->l.nupvalues; i++) {
            mark_object(L, &cl->l.upvals[i]->gch);
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

        lua_lock(th); // FIXME: traversal deadlock?
        mark_value(L, &th->l_gt);
        mark_value(L, &th->env);
        mark_value(L, &th->tls);
        /* the corresponding global state */
        lua_assert(G(L) == th->l_G);
        mark_object(L, (GCheader*)th->l_G);

        lim = th->top;
        for (ci = th->base_ci; ci <= th->ci; ci++) {
          lua_assert(ci->top <= th->stack_last);
          if (lim < ci->top) {
            lim = ci->top;
          }
        }
        for (sk = th->stack; sk < th->top; sk++) {
          mark_value(L, sk);
        }
        for (; sk <= lim; sk++) {
          setnilvalue(sk); // FIXME: only for local thread?
        }
        /* open upvalues also */
        for (uv = th->openupval.u.l.next;
            uv != &th->openupval; uv = uv->u.l.next) {
          mark_object(L, (GCheader*)uv);
        }
        lua_unlock(th);
        break;
      }
    case LUA_TPROTO:
      {
        Proto *f = gco2p(o);

        if (f->source) {
          mark_object(L, &f->source->tsv.gch);
        }
        for (i = 0; i < f->sizek; i++) {
          mark_value(L, &f->k[i]);
        }
        for (i = 0; i < f->sizeupvalues; i++) {
          if (f->upvalues[i]) {
            mark_object(L, f->upvalues[i]);
          }
        }
        for (i = 0; i < f->sizep; i++) {
          if (f->p[i]) {
            mark_object(L, &f->p[i]->gch);
          }
        }
        for (i = 0; i < f->sizelocvars; i++) {
          if (f->locvars[i].varname) {
            mark_object(L, f->locvars[i].varname);
          }
        }
        break;
      }
    case LUA_TGLOBAL:
      mark_object(L, &G(L)->memerr->tsv.gch);
      for (i = 0; i < NUM_TAGS; i++) {
        if (G(L)->mt[i]) {
          mark_object(L, &G(L)->mt[i]->gch);
        }
      }
      for (i = 0; i < TM_N; i++) {
        if (G(L)->tmname[i]) {
          mark_object(L, &G(L)->tmname[i]->tsv.gch);
        }
      }
      mark_value(L, &G(L)->l_registry);
      mark_value(L, &G(L)->ostls);
      mark_value(L, &G(L)->l_globals);
      mark_object(L, &G(L)->mainthread->gch);
      break;

    default:
#if HAVE_VALGRIND
      VALGRIND_PRINTF_BACKTRACE("marking %s not implemented o=%p\n",
          lua_typename(NULL, o->tt), o);
#endif
      fprintf(stderr, "marking for tt=%d is not implemented\n", o->tt);
      abort();
  }
      
  append_list(L->Black, o);
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

static void make_tls_key(void)
{
  pthread_key_create(&tls_key, free);
}

thr_State *luaC_get_per_thread(void)
{
  thr_State *pt;

  pthread_once(&tls_init, make_tls_key);
  pt = pthread_getspecific(tls_key);
  if (pt == NULL) {
    pt = calloc(1, sizeof(*pt));
    pthread_setspecific(tls_key, pt);
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
      /* FIXME: maintain a separate list of lua_State */
      n = luaM_reallocG(G(L), NULL, 0, sizeof(lua_State) + G(L)->extraspace);
      memset(n, 0, sizeof(lua_State) + G(L)->extraspace);
      n->gch.tt = LUA_TTHREAD;
      n->heapid = scpt_atomic_inc(&G(L)->nextheapid);
      n->gch.owner = n->heapid;
      n->gch.marked = 1; /* white wrt. its own list */
      n->gch.xref = G(L)->isxref;
      o = &n->gch;
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

    lua_assert(o->owner == L->heapid);
    lua_assert(final_close == 1 || o->xref == G(L)->notxref);
    lua_assert(o->ref == 0);
    unlink_list(o);

#if HAVE_VALGRIND && DEBUG_ALLOC
    VALGRIND_PRINTF_BACKTRACE("reclaim %s at %p (marked=%x)\n",
      lua_typename(NULL, o->tt), o, o->marked);
#endif
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

    if (o->ref || o->xref == G(L)->isxref) {
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

void luaC_checkGC(lua_State *L)
{
  local_collection(L);
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
