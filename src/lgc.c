/*
** $Id: lgc.c,v 2.38.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "thrlua.h"

#define GCSTEPSIZE	1024u
#define GCSWEEPMAX	40
#define GCSWEEPCOST	10
#define GCFINALIZECOST	100


#define maskmarks	cast_byte(~(bitmask(BLACKBIT)|WHITEBITS))

#define makewhite(g,x)	\
   ((x)->gch.marked = cast_byte(((x)->gch.marked & maskmarks) | luaC_white(g)))

#define white2gray(x)	reset2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)
#define black2gray(x)	resetbit((x)->gch.marked, BLACKBIT)

#define stringmark(s)	reset2bits((s)->tsv.marked, WHITE0BIT, WHITE1BIT)


#define isfinalized(u)		testbit((u)->marked, FINALIZEDBIT)
#define markfinalized(u)	l_setbit((u)->marked, FINALIZEDBIT)


#define KEYWEAK         bitmask(KEYWEAKBIT)
#define VALUEWEAK       bitmask(VALUEWEAKBIT)



#define markvalue(g,o) { checkconsistency(o); \
  if (iscollectable(o) && iswhite(gcvalue(o))) reallymarkobject(g,gcvalue(o)); }

#define markobject(g,t) { if (iswhite(obj2gco(t))) \
		reallymarkobject(g, obj2gco(t)); }


#define setthreshold(g)  (g->GCthreshold = (g->estimate/100) * g->gcpause)


static void removeentry (Node *n) {
  lua_assert(ttisnil(gval(n)));
  if (iscollectable(gkey(n)))
    setttype(gkey(n), LUA_TDEADKEY);  /* dead key; remove it */
}


static void reallymarkobject (global_State *g, GCObject *o) {
  lua_assert(iswhite(o) && !isdead(g, o));
  white2gray(o);
  switch (o->gch.tt) {
    case LUA_TSTRING: {
      return;
    }
    case LUA_TUSERDATA: {
      Table *mt = gco2u(o)->metatable;
      gray2black(o);  /* udata are never gray */
      if (mt) markobject(g, mt);
      markobject(g, gco2u(o)->env);
      return;
    }
    case LUA_TUPVAL: {
      UpVal *uv = gco2uv(o);
      markvalue(g, uv->v);
      if (uv->v == &uv->u.value)  /* closed? */
        gray2black(o);  /* open upvalues are never black */
      return;
    }
    case LUA_TFUNCTION: {
      gco2cl(o)->c.gclist = g->gray;
      g->gray = o;
      break;
    }
    case LUA_TTABLE: {
      gco2h(o)->gclist = g->gray;
      g->gray = o;
      break;
    }
    case LUA_TTHREAD: {
      gco2th(o)->gclist = g->gray;
      g->gray = o;
      break;
    }
    case LUA_TPROTO: {
      gco2p(o)->gclist = g->gray;
      g->gray = o;
      break;
    }
    default: lua_assert(0);
  }
}


static void marktmu (global_State *g) {
  GCObject *u = g->tmudata;
  if (u) {
    do {
      u = u->gch.next;
      makewhite(g, u);  /* may be marked, if left from previous GC */
      reallymarkobject(g, u);
    } while (u != g->tmudata);
  }
}


/* move `dead' udata that need finalization to list `tmudata' */
size_t luaC_separateudata (lua_State *L, int all) {
  global_State *g = G(L);
  size_t deadmem = 0;
  GCObject **p = &g->mainthread->next;
  GCObject *curr;
  while ((curr = *p) != NULL) {
    if (!(iswhite(curr) || all) || isfinalized(gco2u(curr)))
      p = &curr->gch.next;  /* don't bother with them */
    else if (fasttm(L, gco2u(curr)->metatable, TM_GC) == NULL) {
      markfinalized(gco2u(curr));  /* don't need finalization */
      p = &curr->gch.next;
    }
    else {  /* must call its gc method */
      deadmem += sizeudata(gco2u(curr));
      markfinalized(gco2u(curr));
      *p = curr->gch.next;
      /* link `curr' at the end of `tmudata' list */
      if (g->tmudata == NULL)  /* list is empty? */
        g->tmudata = curr->gch.next = curr;  /* creates a circular list */
      else {
        curr->gch.next = g->tmudata->gch.next;
        g->tmudata->gch.next = curr;
        g->tmudata = curr;
      }
    }
  }
  return deadmem;
}


static int traversetable (global_State *g, Table *h) {
  int i;
  int weakkey = 0;
  int weakvalue = 0;
  const TValue *mode;
  if (h->metatable)
    markobject(g, h->metatable);
  mode = gfasttm(g, h->metatable, TM_MODE);
  if (mode && ttisstring(mode)) {  /* is there a weak mode? */
    weakkey = (strchr(svalue(mode), 'k') != NULL);
    weakvalue = (strchr(svalue(mode), 'v') != NULL);
    if (weakkey || weakvalue) {  /* is really weak? */
      h->marked &= ~(KEYWEAK | VALUEWEAK);  /* clear bits */
      h->marked |= cast_byte((weakkey << KEYWEAKBIT) |
                             (weakvalue << VALUEWEAKBIT));
      h->gclist = g->weak;  /* must be cleared after GC, ... */
      g->weak = obj2gco(h);  /* ... so put in the appropriate list */
    }
  }
  if (weakkey && weakvalue) return 1;
  if (!weakvalue) {
    i = h->sizearray;
    while (i--)
      markvalue(g, &h->array[i]);
  }
  i = sizenode(h);
  while (i--) {
    Node *n = gnode(h, i);
    lua_assert(ttype(gkey(n)) != LUA_TDEADKEY || ttisnil(gval(n)));
    if (ttisnil(gval(n)))
      removeentry(n);  /* remove empty entries */
    else {
      lua_assert(!ttisnil(gkey(n)));
      if (!weakkey) markvalue(g, gkey(n));
      if (!weakvalue) markvalue(g, gval(n));
    }
  }
  return weakkey || weakvalue;
}


/*
** All marks are conditional because a GC may happen while the
** prototype is still being created
*/
static void traverseproto (global_State *g, Proto *f) {
  int i;
  if (f->source) stringmark(f->source);
  for (i=0; i<f->sizek; i++)  /* mark literals */
    markvalue(g, &f->k[i]);
  for (i=0; i<f->sizeupvalues; i++) {  /* mark upvalue names */
    if (f->upvalues[i])
      stringmark(f->upvalues[i]);
  }
  for (i=0; i<f->sizep; i++) {  /* mark nested protos */
    if (f->p[i])
      markobject(g, f->p[i]);
  }
  for (i=0; i<f->sizelocvars; i++) {  /* mark local-variable names */
    if (f->locvars[i].varname)
      stringmark(f->locvars[i].varname);
  }
}



static void traverseclosure (global_State *g, Closure *cl) {
  markobject(g, cl->c.env);
  if (cl->c.isC) {
    int i;
    for (i=0; i<cl->c.nupvalues; i++)  /* mark its upvalues */
      markvalue(g, &cl->c.upvalue[i]);
  }
  else {
    int i;
    lua_assert(cl->l.nupvalues == cl->l.p->nups);
    markobject(g, cl->l.p);
    for (i=0; i<cl->l.nupvalues; i++)  /* mark its upvalues */
      markobject(g, cl->l.upvals[i]);
  }
}


static void checkstacksizes (lua_State *L, StkId max) {
  int ci_used = cast_int(L->ci - L->base_ci);  /* number of `ci' in use */
  int s_used = cast_int(max - L->stack);  /* part of stack in use */
  if (L->size_ci > LUAI_MAXCALLS)  /* handling overflow? */
    return;  /* do not touch the stacks */
  if (4*ci_used < L->size_ci && 2*BASIC_CI_SIZE < L->size_ci)
    luaD_reallocCI(L, L->size_ci/2);  /* still big enough... */
  condhardstacktests(luaD_reallocCI(L, ci_used + 1));
  if (4*s_used < L->stacksize &&
      2*(BASIC_STACK_SIZE+EXTRA_STACK) < L->stacksize)
    luaD_reallocstack(L, L->stacksize/2);  /* still big enough... */
  condhardstacktests(luaD_reallocstack(L, s_used));
}


static void traversestack (global_State *g, lua_State *l) {
  StkId o, lim;
  CallInfo *ci;
  markvalue(g, gt(l));
  lim = l->top;
  for (ci = l->base_ci; ci <= l->ci; ci++) {
    lua_assert(ci->top <= l->stack_last);
    if (lim < ci->top) lim = ci->top;
  }
  for (o = l->stack; o < l->top; o++)
    markvalue(g, o);
  for (; o <= lim; o++)
    setnilvalue(o);
  checkstacksizes(l, lim);
}


/*
** traverse one gray object, turning it to black.
** Returns `quantity' traversed.
*/
static l_mem propagatemark (global_State *g) {
  GCObject *o = g->gray;
  lua_assert(isgray(o));
  gray2black(o);
  switch (o->gch.tt) {
    case LUA_TTABLE: {
      Table *h = gco2h(o);
      g->gray = h->gclist;
      if (traversetable(g, h))  /* table is weak? */
        black2gray(o);  /* keep it gray */
      return sizeof(Table) + sizeof(TValue) * h->sizearray +
                             sizeof(Node) * sizenode(h);
    }
    case LUA_TFUNCTION: {
      Closure *cl = gco2cl(o);
      g->gray = cl->c.gclist;
      traverseclosure(g, cl);
      return (cl->c.isC) ? sizeCclosure(cl->c.nupvalues) :
                           sizeLclosure(cl->l.nupvalues);
    }
    case LUA_TTHREAD: {
      lua_State *th = gco2th(o);
      g->gray = th->gclist;
      th->gclist = g->grayagain;
      g->grayagain = o;
      black2gray(o);
      traversestack(g, th);
      return sizeof(lua_State) + sizeof(TValue) * th->stacksize +
                                 sizeof(CallInfo) * th->size_ci;
    }
    case LUA_TPROTO: {
      Proto *p = gco2p(o);
      g->gray = p->gclist;
      traverseproto(g, p);
      return sizeof(Proto) + sizeof(Instruction) * p->sizecode +
                             sizeof(Proto *) * p->sizep +
                             sizeof(TValue) * p->sizek + 
                             sizeof(int) * p->sizelineinfo +
                             sizeof(LocVar) * p->sizelocvars +
                             sizeof(TString *) * p->sizeupvalues;
    }
    default: lua_assert(0); return 0;
  }
}


static size_t propagateall (global_State *g) {
  size_t m = 0;
  while (g->gray) m += propagatemark(g);
  return m;
}


/*
** The next function tells whether a key or value can be cleared from
** a weak table. Non-collectable objects are never removed from weak
** tables. Strings behave as `values', so are never removed too. for
** other objects: if really collected, cannot keep them; for userdata
** being finalized, keep them in keys, but not in values
*/
static int iscleared (const TValue *o, int iskey) {
  if (!iscollectable(o)) return 0;
  if (ttisstring(o)) {
    stringmark(rawtsvalue(o));  /* strings are `values', so are never weak */
    return 0;
  }
  return iswhite(gcvalue(o)) ||
    (ttisuserdata(o) && (!iskey && isfinalized(uvalue(o))));
}


/*
** clear collected entries from weaktables
*/
static void cleartable (GCObject *l) {
  while (l) {
    Table *h = gco2h(l);
    int i = h->sizearray;
    lua_assert(testbit(h->marked, VALUEWEAKBIT) ||
               testbit(h->marked, KEYWEAKBIT));
    if (testbit(h->marked, VALUEWEAKBIT)) {
      while (i--) {
        TValue *o = &h->array[i];
        if (iscleared(o, 0))  /* value was collected? */
          setnilvalue(o);  /* remove value */
      }
    }
    i = sizenode(h);
    while (i--) {
      Node *n = gnode(h, i);
      if (!ttisnil(gval(n)) &&  /* non-empty entry? */
          (iscleared(key2tval(n), 1) || iscleared(gval(n), 0))) {
        setnilvalue(gval(n));  /* remove value ... */
        removeentry(n);  /* remove entry from table */
      }
    }
    l = h->gclist;
  }
}


static void freeobj (lua_State *L, GCObject *o) {
  switch (o->gch.tt) {
    case LUA_TPROTO: luaF_freeproto(L, gco2p(o)); break;
    case LUA_TFUNCTION: luaF_freeclosure(L, gco2cl(o)); break;
    case LUA_TUPVAL: luaF_freeupval(L, gco2uv(o)); break;
    case LUA_TTABLE: luaH_free(L, gco2h(o)); break;
    case LUA_TTHREAD: {
      lua_assert(gco2th(o) != L && gco2th(o) != G(L)->mainthread);
      luaE_freethread(L, gco2th(o));
      break;
    }
    case LUA_TSTRING: {
//      G(L)->strt.nuse--;
      luaM_freemem(L, o, sizestring(gco2ts(o)));
      break;
    }
    case LUA_TUSERDATA: {
      luaM_freemem(L, o, sizeudata(gco2u(o)));
      break;
    }
    default: lua_assert(0);
  }
}



#define sweepwholelist(L,p)	sweeplist(L,p,MAX_LUMEM)


static GCObject **sweeplist (lua_State *L, GCObject **p, lu_mem count) {
  GCObject *curr;
  global_State *g = G(L);
  int deadmask = otherwhite(g);
  while ((curr = *p) != NULL && count-- > 0) {
    if (curr->gch.tt == LUA_TTHREAD)  /* sweep open upvalues of each thread */
      sweepwholelist(L, &gco2th(curr)->openupval);
    if ((curr->gch.marked ^ WHITEBITS) & deadmask) {  /* not dead? */
      lua_assert(!isdead(g, curr) || testbit(curr->gch.marked, FIXEDBIT));
      makewhite(g, curr);  /* make it white (for next cycle) */
      p = &curr->gch.next;
    }
    else {  /* must erase `curr' */
      lua_assert(isdead(g, curr) || deadmask == bitmask(SFIXEDBIT));
      *p = curr->gch.next;
      if (curr == g->rootgc)  /* is the first element of the list? */
        g->rootgc = curr->gch.next;  /* adjust first */
      freeobj(L, curr);
    }
  }
  return p;
}


static void checkSizes (lua_State *L) {
#if 0
  global_State *g = G(L);
  /* check size of string hash */
  if (g->strt.nuse < cast(uint32_t, g->strt.size/4) &&
      g->strt.size > MINSTRTABSIZE*2)
    luaS_resize(L, g->strt.size/2);  /* table is too big */
#endif
}


static void GCTM (lua_State *L) {
  global_State *g = G(L);
  GCObject *o = g->tmudata->gch.next;  /* get first element */
  Udata *udata = rawgco2u(o);
  const TValue *tm;
  /* remove udata from `tmudata' */
  if (o == g->tmudata)  /* last element? */
    g->tmudata = NULL;
  else
    g->tmudata->gch.next = udata->uv.next;
  udata->uv.next = g->mainthread->next;  /* return it to `root' list */
  g->mainthread->next = o;
  makewhite(g, o);
  tm = fasttm(L, udata->uv.metatable, TM_GC);
  if (tm != NULL) {
    lu_byte oldah = L->allowhook;
    lu_mem oldt = g->GCthreshold;
    L->allowhook = 0;  /* stop debug hooks during GC tag method */
    g->GCthreshold = 2*g->totalbytes;  /* avoid GC steps */
    setobj2s(L, L->top, tm);
    setuvalue(L, L->top+1, udata);
    L->top += 2;
    luaD_call(L, L->top - 2, 0);
    L->allowhook = oldah;  /* restore hooks */
    g->GCthreshold = oldt;  /* restore threshold */
  }
}


/*
** Call all GC tag methods
*/
void luaC_callGCTM (lua_State *L) {
  while (G(L)->tmudata)
    GCTM(L);
}


void luaC_freeall (lua_State *L) {
  global_State *g = G(L);
  int i;
  g->currentwhite = WHITEBITS | bitmask(SFIXEDBIT);  /* mask to collect all elements */
  sweepwholelist(L, &g->rootgc);
#if 0
  for (i = 0; i < g->strt.size; i++)  /* free all string lists */
    sweepwholelist(L, &g->strt.hash[i]);
#endif
}


static void markmt (global_State *g) {
  int i;
  for (i=0; i<NUM_TAGS; i++)
    if (g->mt[i]) markobject(g, g->mt[i]);
}


/* mark root set */
static void markroot (lua_State *L) {
  global_State *g = G(L);
  g->gray = NULL;
  g->grayagain = NULL;
  g->weak = NULL;
  markobject(g, g->mainthread);
  /* make global table be traversed before main stack */
  markvalue(g, gt(g->mainthread));
  markvalue(g, registry(L));
  markmt(g);
  g->gcstate = GCSpropagate;
}


static void remarkupvals (global_State *g) {
  UpVal *uv;
  for (uv = g->uvhead.u.l.next; uv != &g->uvhead; uv = uv->u.l.next) {
    lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
    if (isgray(obj2gco(uv)))
      markvalue(g, uv->v);
  }
}


static void atomic (lua_State *L) {
  global_State *g = G(L);
  size_t udsize;  /* total size of userdata to be finalized */
  /* remark occasional upvalues of (maybe) dead threads */
  remarkupvals(g);
  /* traverse objects cautch by write barrier and by 'remarkupvals' */
  propagateall(g);
  /* remark weak tables */
  g->gray = g->weak;
  g->weak = NULL;
  lua_assert(!iswhite(obj2gco(g->mainthread)));
  markobject(g, L);  /* mark running thread */
  markmt(g);  /* mark basic metatables (again) */
  propagateall(g);
  /* remark gray again */
  g->gray = g->grayagain;
  g->grayagain = NULL;
  propagateall(g);
  udsize = luaC_separateudata(L, 0);  /* separate userdata to be finalized */
  marktmu(g);  /* mark `preserved' userdata */
  udsize += propagateall(g);  /* remark, to propagate `preserveness' */
  cleartable(g->weak);  /* remove collected objects from weak tables */
  /* flip current white */
  g->currentwhite = cast_byte(otherwhite(g));
  g->sweepstrgc = 0;
  g->sweepgc = &g->rootgc;
  g->gcstate = GCSsweepstring;
  g->estimate = g->totalbytes - udsize;  /* first estimate */
}


static l_mem singlestep (lua_State *L) {
  global_State *g = G(L);
  /*lua_checkmemory(L);*/
  switch (g->gcstate) {
    case GCSpause: {
      markroot(L);  /* start a new collection */
      return 0;
    }
    case GCSpropagate: {
      if (g->gray)
        return propagatemark(g);
      else {  /* no more `gray' objects */
        atomic(L);  /* finish mark phase */
        return 0;
      }
    }
    case GCSsweepstring: {
      lu_mem old = g->totalbytes;
#if 0
      sweepwholelist(L, &g->strt.hash[g->sweepstrgc++]);
      if (g->sweepstrgc >= g->strt.size)  /* nothing more to sweep? */
#endif
        g->gcstate = GCSsweep;  /* end sweep-string phase */
      lua_assert(old >= g->totalbytes);
      g->estimate -= old - g->totalbytes;
      return GCSWEEPCOST;
    }
    case GCSsweep: {
      lu_mem old = g->totalbytes;
      g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);
      if (*g->sweepgc == NULL) {  /* nothing more to sweep? */
        checkSizes(L);
        g->gcstate = GCSfinalize;  /* end sweep phase */
      }
      lua_assert(old >= g->totalbytes);
      g->estimate -= old - g->totalbytes;
      return GCSWEEPMAX*GCSWEEPCOST;
    }
    case GCSfinalize: {
      if (g->tmudata) {
        GCTM(L);
        if (g->estimate > GCFINALIZECOST)
          g->estimate -= GCFINALIZECOST;
        return GCFINALIZECOST;
      }
      else {
        g->gcstate = GCSpause;  /* end collection */
        g->gcdept = 0;
        return 0;
      }
    }
    default: lua_assert(0); return 0;
  }
}


void luaC_step (lua_State *L) {
  global_State *g = G(L);
  l_mem lim = (GCSTEPSIZE/100) * g->gcstepmul;
  if (lim == 0)
    lim = (MAX_LUMEM-1)/2;  /* no limit */
  g->gcdept += g->totalbytes - g->GCthreshold;
  do {
    lim -= singlestep(L);
    if (g->gcstate == GCSpause)
      break;
  } while (lim > 0);
  if (g->gcstate != GCSpause) {
    if (g->gcdept < GCSTEPSIZE)
      g->GCthreshold = g->totalbytes + GCSTEPSIZE;  /* - lim/g->gcstepmul;*/
    else {
      g->gcdept -= GCSTEPSIZE;
      g->GCthreshold = g->totalbytes;
    }
  }
  else {
    lua_assert(g->totalbytes >= g->estimate);
    setthreshold(g);
  }
}


void luaC_fullgc (lua_State *L) {
  global_State *g = G(L);
  if (g->gcstate <= GCSpropagate) {
    /* reset sweep marks to sweep all elements (returning them to white) */
    g->sweepstrgc = 0;
    g->sweepgc = &g->rootgc;
    /* reset other collector lists */
    g->gray = NULL;
    g->grayagain = NULL;
    g->weak = NULL;
    g->gcstate = GCSsweepstring;
  }
  lua_assert(g->gcstate != GCSpause && g->gcstate != GCSpropagate);
  /* finish any pending sweep phase */
  while (g->gcstate != GCSfinalize) {
    lua_assert(g->gcstate == GCSsweepstring || g->gcstate == GCSsweep);
    singlestep(L);
  }
  markroot(L);
  while (g->gcstate != GCSpause) {
    singlestep(L);
  }
  setthreshold(g);
}


void luaC_barrierf (lua_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
  lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
  lua_assert(ttype(&o->gch) != LUA_TTABLE);
  /* must keep invariant? */
  if (g->gcstate == GCSpropagate)
    reallymarkobject(g, v);  /* restore invariant */
  else  /* don't mind */
    makewhite(g, o);  /* mark as white just to avoid other barriers */
}


void luaC_barrierback (lua_State *L, Table *t) {
  global_State *g = G(L);
  GCObject *o = obj2gco(t);
  lua_assert(isblack(o) && !isdead(g, o));
  lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
  black2gray(o);  /* make table gray (again) */
  t->gclist = g->grayagain;
  g->grayagain = o;
}


void luaC_link (lua_State *L, GCObject *o, lu_byte tt) {
  global_State *g = G(L);
  o->gch.next = g->rootgc;
  g->rootgc = o;
  o->gch.marked = luaC_white(g);
  o->gch.tt = tt;
}


void luaC_linkupval (lua_State *L, UpVal *uv) {
  global_State *g = G(L);
  GCObject *o = obj2gco(uv);
  o->gch.next = g->rootgc;  /* link upvalue into `rootgc' list */
  g->rootgc = o;
  if (isgray(o)) { 
    if (g->gcstate == GCSpropagate) {
      gray2black(o);  /* closed upvalues need barrier */
      luaC_barrier(L, uv, uv->v);
    }
    else {  /* sweep phase: sweep it (turning it into white) */
      makewhite(g, o);
      lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
    }
  }
}

static inline void o_push(global_State *g, struct gc_stack *stk, GCObject *o)
{
  if (stk->items + 1 >= stk->alloc) {
    unsigned int nsize = stk->alloc ? stk->alloc * 2 : 1024;
    GCObject *p;

    while (nsize < stk->items + 1) {
      nsize *= 2;
    }

    stk->obj = g->alloc(g->allocdata, stk->obj,
      stk->alloc * sizeof(GCObject*),
      nsize * sizeof(GCObject*));
    stk->alloc = nsize;
  }

  stk->obj[stk->items++] = o;
}

static void o_free(global_State *g, struct gc_stack *stk)
{
  g->alloc(g->allocdata, stk->obj, stk->alloc * sizeof(GCObject*), 0);
  memset(stk, 0, sizeof(*stk));
}

static inline void o_clear(struct gc_stack *stk)
{
  stk->items = 0;
}

static unsigned int log_or_count(global_State *g, 
  GCObject *o, struct gc_log_buffer *buf)
{
  unsigned int room = 0;

  switch (o->gch.tt) {
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
            buf->obj[buf->items++] = (GCObject*)ud->uv.metatable;
          }
          room++;
        }
        if (ud->uv.env) {
          if (buf) {
            buf->obj[buf->items++] = (GCObject*)ud->uv.env;
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
            buf->obj[buf->items++] = (GCObject*)h->metatable;
          }
          room++;
        }
        mode = gfasttm(g, h->metatable, TM_MODE);
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
            buf->obj[buf->items++] = (GCObject*)cl->l.p;
          }
          room++;
          for (i = 0; i < cl->l.nupvalues; i++) {
            if (buf) {
              buf->obj[buf->items++] = (GCObject*)cl->l.upvals[i];
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
            buf->obj[buf->items++] = (GCObject*)f->source;
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
              buf->obj[buf->items++] = (GCObject*)f->upvalues[i];
            }
            room++;
          }
        }
        for (i = 0; i < f->sizep; i++) {
          if (f->p[i]) {
            if (buf) {
              buf->obj[buf->items++] = (GCObject*)f->p[i];
            }
            room++;
          }
        }
        for (i = 0; i < f->sizelocvars; i++) {
          if (f->locvars[i].varname) {
            if (buf) {
              buf->obj[buf->items++] = (GCObject*)f->locvars[i].varname;
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

static void log_object(global_State *g, thr_State *pt, GCObject *o)
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
    buf = g->alloc(g->allocdata, NULL, 0,
      sizeof(*buf) + (size - 1) * sizeof(GCObject*));
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
  buf->obj[buf->items++] = (GCObject*)(((intptr_t)o) | 0x1);

  /* after all this effort, it may be the case that another thread has
   * already logged this object; make a final check before we commit.
   * FIXME: could CAS this, but may not be needed, assuming that the 
   * modifying thread should already hold a write lock */
  if (o->gch.logptr == NULL) {
    o->gch.logptr = &buf->obj[bi];
  } else {
    /* roll back the log */
    buf->items = bi;
  }
}

void luaC_writebarrier(global_State *g, GCObject *object,
  GCObject **lvalue, GCObject *rvalue)
{
  thr_State *pt = getpt(g);

  pthread_mutex_lock(&pt->handshake);
  if (pt->trace && object->gch.marked != pt->alloc_color &&
      object->gch.logptr == NULL) {
    log_object(g, pt, object);
  }
  *lvalue = rvalue;
  if (pt->snoop && rvalue != NULL) {
    o_push(g, &pt->snoop_buf, rvalue);
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

static void mark_object(global_State *g, GCObject *o)
{
  if (o->gch.marked == g->white) {
    if (o->gch.logptr) {
      /* use the logged snapshot */
      GCObject **log = o->gch.logptr;

      while (1) {
        GCObject *v = *log;
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


      switch (o->gch.tt) {
        case LUA_TSTRING:
          /* has no contents */
          break;

        case LUA_TUSERDATA:
          {
            Udata *ud = rawgco2u(o);
            if (ud->uv.metatable) {
              o_push(g, &g->mark_set, (GCObject*)ud->uv.metatable);
            }
            if (ud->uv.env) {
              o_push(g, &g->mark_set, (GCObject*)ud->uv.env);
            }
            break;
          }
        case LUA_TUPVAL:
          {
            UpVal *uv = gco2uv(o);
            mark_value(g, uv->v);
            if (uv->v == &uv->u.value) {
              /* FIXME: open upvalues are never black? */
            }
            break;
          }
        case LUA_TTABLE:
        {
          Table *h = gco2h(o);
          int i, weakkey = 0, weakvalue = 0;
          const TValue *mode;

          luaH_rdlock(g, h);
          if (h->metatable) {
            o_push(g, &g->mark_set, (GCObject*)h->metatable);
          }
          mode = gfasttm(g, h->metatable, TM_MODE);
          if (mode && ttisstring(mode)) {
            weakkey = (strchr(svalue(mode), 'k') != NULL);
            weakvalue = (strchr(svalue(mode), 'v') != NULL);
            if (weakkey || weakvalue) {
              o_push(g, &g->weak_set, (GCObject*)h);
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

            if (!ttisnil(gval(n))) {
              lua_assert(!ttisnil(gkey(n)));
              /* strings are never weak */
              if (!weakkey || ttisstring(gkey(n))) {
                mark_value(g, key2tval(n));
              }
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
          int i;

          if (cl->c.isC) {
            for (i = 0; i < cl->c.nupvalues; i++) {
              mark_value(g, &cl->c.upvalue[i]);
            }
          } else {
            lua_assert(cl->l.nupvalues == cl->l.p->nups);
            o_push(g, &g->mark_set, (GCObject*)cl->l.p);
            for (i = 0; i < cl->l.nupvalues; i++) {
              o_push(g, &g->mark_set, (GCObject*)cl->l.upvals[i]);
            }
          }
          break;
        }
        case LUA_TTHREAD:
        {
          lua_State *th = gco2th(o);
          StkId sk;

          /* FIXME: lock */
          mark_value(g, gt(th));
          /* the corresponding global state */
          lua_assert(g == th->l_G);
          o_push(g, &g->mark_set, (GCObject*)th->l_G);
          for (sk = th->stack; sk < th->top; sk++) {
            mark_value(g, sk);
          }
          break;
        }
        case LUA_TPROTO:
        {
          Proto *f = gco2p(o);
          int i;

          if (f->source) {
            o_push(g, &g->mark_set, (GCObject*)f->source);
          }
          for (i = 0; i < f->sizek; i++) {
            mark_value(g, &f->k[i]);
          }
          for (i = 0; i < f->sizeupvalues; i++) {
            if (f->upvalues[i]) {
              o_push(g, &g->mark_set, (GCObject*)f->upvalues[i]);
            }
          }
          for (i = 0; i < f->sizep; i++) {
            if (f->p[i]) {
              o_push(g, &g->mark_set, (GCObject*)f->p[i]);
            }
          }
          for (i = 0; i < f->sizelocvars; i++) {
            if (f->locvars[i].varname) {
              o_push(g, &g->mark_set, (GCObject*)f->locvars[i].varname);
            }
          }
          break;
        }
        default:
          abort();
      }
    }
    o->gch.marked = g->black;
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
  if (g->white == gcvalue(o)->gch.marked) {
    return 1;
  }
  /* NOTE: we differ from stock lua in that we don't preserve a weak
   * finalized udata key. */
  return 0;
}

static void finalize(global_State *g)
{
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
  GCObject *o, *prior;
  int i;

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

  /* Stage 4: gather roots from threads */
  o_clear(&g->root_set);
  for (pt = g->all_threads; pt; pt = pt->next) {
    pthread_mutex_lock(&pt->handshake);
    /* update color for the thread */
    pt->alloc_color = g->black;
    pt->snoop = 0;

    /* take its object list */
    if (pt->olist_tail) {
      pt->olist_tail->gch.next = g->the_heap;
      g->the_heap = pt->olist;
      pt->olist = NULL;
      pt->olist_tail = NULL;
    }
    pthread_mutex_unlock(&pt->handshake);

    /* its string table is also considered a root */
    pthread_mutex_lock(&pt->strt.lock);
    for (i = 0; i < pt->strt.size; i++) {
      struct stringtable_node *n = pt->strt.hash[i];
      while (n) {
        o_push(g, &g->root_set, (GCObject*)n->str);
        n = n->next;
      }
    }
    pthread_mutex_unlock(&pt->strt.lock);
  }

  /* Identify roots in the heap; they have a non-zero ref count */
  for (o = g->the_heap; o; o = o->gch.next) {
    heap_size++;
    if (o->gch.ref > 0) {
      o_push(g, &g->root_set, o);
    }
  }

  /* Stage 5: pull snoop buffers.  Since all threads now have snooping turned
   * off, we are the only thread that will be looking at them; no need to
   * lock the handshake lock */
  for (pt = g->all_threads; pt; pt = pt->next) {
    for (i = 0; i < pt->snoop_buf.items; i++) {
      o_push(g, &g->root_set, pt->snoop_buf.obj[i]);
    }
    o_clear(&pt->snoop_buf);
  }

  /* Stage 6: mark objects black; seed the marking stack with the set
   * of root objects */
  o_clear(&g->mark_set);
  for (i = 0; i < g->root_set.items; i++) {
    o_push(g, &g->mark_set, g->root_set.obj[i]);
  }
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
  prior = NULL;
  o = g->the_heap;
  while (o != NULL) {
    if (o->gch.marked == g->white) {
      swept++;
      if (prior) {
        prior->gch.next = o->gch.next;
      } else {
        g->the_heap = o->gch.next;
      }
      o->gch.next = g->to_finalize;
      g->to_finalize = o;
      if (prior) {
        o = prior->gch.next;
      } else {
        o = g->the_heap;
      }
    } else {
      prior = o;
      o = o->gch.next;
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
        o = (GCObject*)(((intptr_t)buf->obj[i]) & ~0x1);
        if (o) {
          o->gch.logptr = NULL;
        }
      }

      if (spare == NULL && buf->alloc == 128) {
        spare = buf;
        spare->items = 0;
      } else {
        g->alloc(g->allocdata, buf,
          sizeof(*buf) + (buf->alloc - 1) * sizeof(GCObject*), 0);
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
      o_push(g, &g->mark_set, (GCObject*)h->metatable);
    }
    mode = gfasttm(g, h->metatable, TM_MODE);
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

  /* Stage 12: for threads that have exited, they will have set the vacant
   * flag to indicate that we get to collect their per-thread state */
  pt = g->all_threads;
  while (pt) {
    thr_State *p;

    if (pt->vacant) {
      pt = pt->next;
      continue;
    }
    p = pt->next;

    pthread_mutex_destroy(&pt->handshake);
    luaZ_freebuffer(NULL, &pt->buff);
    luaM_freearray(NULL, pt->strt.hash, pt->strt.size, TString *);
    if (pt->log_buf) {
      g->alloc(g->allocdata, pt->log_buf,
          sizeof(*pt->log_buf) + (pt->log_buf->alloc - 1)
          * sizeof(GCObject*), 0);
    }
    o_free(g, &pt->snoop_buf);
    /* take its object list */
    if (pt->olist_tail) {
      pt->olist_tail->gch.next = g->the_heap;
      g->the_heap = pt->olist;
      pt->olist = NULL;
      pt->olist_tail = NULL;
    }
    free(pt);

    /* move to next */
    pt = p;
  }

  /* collection cycle complete */
  pthread_mutex_unlock(&g->collector_lock);

  /* return 0 to indicate that collect_all does not need to loop;
   * it means that there are roots */
  return g->root_set.items ? 0 : heap_size - swept;
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

/** The collector thread collects every second or when woken up
 * to collect */
static void *collector(void *ptr)
{
  global_State *g = ptr;

  while (!g->exiting) {
    struct timespec deadline;
    int ret;

    memset(&deadline, 0, sizeof(deadline));
    deadline.tv_sec = time(NULL) + 1;
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

  /* let the collector free this up */
  pt->vacant = 1;
}

thr_State *luaC_init_pt(global_State *g)
{
  thr_State *pt;

  pt = calloc(1, sizeof(*pt));
  pt->g = g;

  pthread_mutex_init(&pt->handshake, NULL);

  pthread_setspecific(g->tls_key, pt);
  luaZ_initbuffer(NULL, &pt->buff);

  pthread_mutex_init(&pt->strt.lock, NULL);
  pt->strt.hash = luaM_newvector(NULL, MINSTRTABSIZE, struct stringtable_node*);
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

  g = alloc(ud, NULL, 0, sizeof(*g));
  if (!g) {
    return NULL;
  }
  memset(g, 0, sizeof(*g));
  g->tt = LUA_TGLOBAL;
 
  pthread_key_create(&g->tls_key, tls_dtor);
  pthread_cond_init(&g->gc_cond, NULL);
  pthread_mutex_init(&g->collector_lock, NULL);
  g->black = 0;
  g->white = 1;
  g->alloc = alloc;
  g->allocdata = ud;

  pthread_create(&g->collector_thread, NULL, collector, g);

  return g;
}

void *luaC_newobj(lua_State *L, lu_byte tt)
{
  GCObject *o;
  switch (tt) {
    case LUA_TGLOBAL:
      abort();
#define NEWIMPL(a, b) \
    case a: \
      o = luaM_malloc(L, sizeof(b)); \
      memset(o, 0, sizeof(b)); \
      o->gch.tt = a; \
      return o
    NEWIMPL(LUA_TUPVAL, UpVal);
    NEWIMPL(LUA_TPROTO, Proto);
    NEWIMPL(LUA_TTABLE, Table);
    NEWIMPL(LUA_TTHREAD, lua_State);
    default:
      printf("unhandled tt=%d\n", tt);
      luaD_throw(L, LUA_ERRMEM);
      return NULL;
  }
}

void *luaC_newobjv(lua_State *L, lu_byte tt, size_t size)
{
  GCObject *o;
  switch (tt) {
#undef NEWIMPL
#define NEWIMPL(a, b) \
    case a: \
      o = luaM_malloc(L, size); \
      memset(o, 0, size); \
      o->gch.tt = a; \
      return o
    NEWIMPL(LUA_TFUNCTION, Closure);
    default:
      printf("unhandled tt=%d\n", tt);
      luaD_throw(L, LUA_ERRMEM);
      return NULL;
  }
}


/* vim:ts=2:sw=2:et:
 */
