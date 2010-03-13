/*
** $Id: lobject.h,v 2.20.1.2 2008/08/06 13:29:48 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h

#if (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__)
typedef uint32_t scpt_atomic_t;

static inline scpt_atomic_t scpt_atomic_inc(volatile scpt_atomic_t *mem)
{
  scpt_atomic_t prev;
  __asm__ volatile ("lock; xaddl %0, %1"
    : "=r" (prev)
    : "m" (*(mem)),
    "0" (1));
  return prev + 1;
}

static inline scpt_atomic_t scpt_atomic_dec(volatile scpt_atomic_t *mem)
{
  scpt_atomic_t prev;
  __asm__ volatile ("lock; xaddl %0, %1"
    : "=r" (prev)
    : "m" (*(mem)),
    "0" (-1));
  return prev - 1;
}

#else
# error no atomic inc/dec defined for this system
#endif

/* tags for values visible from Lua */
#define LAST_TAG	LUA_TTHREAD

#define NUM_TAGS	(LAST_TAG+1)


/*
** Extra tags for non-values
*/
#define LUA_TPROTO	(LAST_TAG+1)
#define LUA_TUPVAL	(LAST_TAG+2)
#define LUA_TDEADKEY	(LAST_TAG+3)
#define LUA_TGLOBAL (LAST_TAG+4)

/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
/*
** Common header in struct form
*/
typedef struct GCheader {
  struct GCheader *next, *prev;
  struct GCheader **logptr;
  scpt_atomic_t ref;
  lu_byte tt;
  lu_byte marked;
} GCheader;

/*
** Union of all Lua values
*/
typedef union {
  GCheader *gc;
  void *p;
  lua_Number n;
  int b;
} Value;


/*
** Tagged Values
*/

#define TValuefields	Value value; int tt

typedef struct lua_TValue {
  TValuefields;
} TValue;

typedef TValue *StkId;  /* index to stack elements */


/*
** String headers for string table
*/
typedef union TString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  struct {
    GCheader gch;
    /** is this a reserved word? */
    lu_byte reserved;
    unsigned int hash;
    size_t len;
  } tsv;
} TString;



/* Macros to test type */
#define ttisnil(o)	(ttype(o) == LUA_TNIL)
#define ttisnumber(o)	(ttype(o) == LUA_TNUMBER)
#define ttisstring(o)	(ttype(o) == LUA_TSTRING)
#define ttistable(o)	(ttype(o) == LUA_TTABLE)
#define ttisfunction(o)	(ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o)	(ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o)	(ttype(o) == LUA_TUSERDATA)
#define ttisthread(o)	(ttype(o) == LUA_TTHREAD)
#define ttislightuserdata(o)	(ttype(o) == LUA_TLIGHTUSERDATA)

/* Macros to access values */
#define ttype(o)	((o)->tt)
#define gcvalue(o)	check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o)	check_exp(ttisnumber(o), (o)->value.n)
#define rawtsvalue(o)	check_exp(ttisstring(o), (TString*)(o)->value.gc)
#define tsvalue(o)	(&rawtsvalue(o)->tsv)
#define rawuvalue(o)	check_exp(ttisuserdata(o), (Udata*)(o)->value.gc)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define clvalue(o)	check_exp(ttisfunction(o), (Closure*)(o)->value.gc)
#define hvalue(o)	check_exp(ttistable(o), (Table*)(o)->value.gc)
#define bvalue(o)	check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o)	check_exp(ttisthread(o), (lua_State*)(o)->value.gc)

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

/*
** for internal debug only
*/
#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->tt))

#define iscollectable(o)	(ttype(o) >= LUA_TSTRING)
#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  ((ttype(obj) == (obj)->value.gc->tt))) // && !isdead(g, (obj)->value.gc)))




typedef union Udata {
  L_Umaxalign dummy;  /* ensures maximum alignment for `local' udata */
  struct {
    GCheader gch;
    GCheader /*struct Table*/ *metatable;
    GCheader /*struct Table*/ *env;
    size_t len;
    lu_byte finalized;
  } uv;
} Udata;




/*
** Function Prototypes
*/
typedef struct Proto {
  GCheader gch;
  TValue *k;  /* constants used by the function */
  Instruction *code;
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines */
  struct LocVar *locvars;  /* information about local variables */
  GCheader **upvalues;  /* upvalue names */
  TString  *source;
  int sizeupvalues;
  int sizek;  /* size of `k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of `p' */
  int sizelocvars;
  int linedefined;
  int lastlinedefined;
  lu_byte nups;  /* number of upvalues */
  lu_byte numparams;
  lu_byte is_vararg;
  lu_byte maxstacksize;
} Proto;


/* masks for new-style vararg */
#define VARARG_HASARG		1
#define VARARG_ISVARARG		2
#define VARARG_NEEDSARG		4


typedef struct LocVar {
  GCheader /*TString*/ *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;



/*
** Upvalues
*/

typedef struct UpVal {
  GCheader gch;
  TValue *v;  /* points to stack or to its own value */
  union {
    TValue value;  /* the value (when closed) */
    struct {  /* double linked list (when open) */
      struct UpVal *prev;
      struct UpVal *next;
    } l;
  } u;
} UpVal;


/*
** Closures
*/

#define ClosureHeader \
	GCheader gch; lu_byte isC; lu_byte nupvalues; \
	GCheader *env

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];
} CClosure;


typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];
} LClosure;


typedef union Closure {
  GCheader gch;
  CClosure c;
  LClosure l;
} Closure;


#define iscfunction(o)	(ttype(o) == LUA_TFUNCTION && clvalue(o)->c.isC)
#define isLfunction(o)	(ttype(o) == LUA_TFUNCTION && !clvalue(o)->c.isC)


/*
** Tables
*/

typedef union TKey {
  struct {
    TValuefields;
    struct Node *next;  /* for chaining */
  } nk;
  TValue tvk;
} TKey;


typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


typedef struct Table {
  GCheader gch;
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */ 
  lu_byte lsizenode;  /* log2 of size of `node' array */
  pthread_rwlock_t lock;
  GCheader /*struct Table*/ *metatable;
  TValue *array;  /* array part */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  int sizearray;  /* size of `array' array */
} Table;



/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


#define luaO_nilobject		(&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x)	(luaO_log2((x)-1) + 1)

LUAI_FUNC int luaO_log2 (unsigned int x);
LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_rawequalObj (const TValue *t1, const TValue *t2);
LUAI_FUNC int luaO_str2d (const char *s, lua_Number *result);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

/* vim:ts=2:sw=2:et:
 */
