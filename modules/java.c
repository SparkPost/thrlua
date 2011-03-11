/*
 * Copyright (c) 2010 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 */

#include "thrlua.h"
#include <jni.h>

/* userdata that points to a jclass */
#define LJB_CLASS "javabridge:jclass"
#define LJB_OBJECT "javabridge:jobject"
#define LJB_METHOD "javabridge:jmethod"
#define LJB_METHODS "javabridge:jmethod:match"

struct jni_methodmatch {
  jobject obj;
  char name[1];
};

typedef jint (*jni_create_vm_func)(JavaVM **pvm, void **penv, void *args);

static JavaVM *jvm = NULL;
static pthread_key_t jthrkey;
/* for interpreting exceptions */
static jclass clz_Throwable = NULL;
/* for converting to string */
static jclass clz_String = NULL;
static jmethodID mid_Object_toString;
static jclass clz_Object = NULL;
static jclass clz_Double = NULL;
static jclass clz_Modifier = NULL;
static jclass clz_Number = NULL;
static jclass clz_Method = NULL;
static jclass clz_Class = NULL;
static jclass clz_Boolean = NULL;
static jclass clz_Byte = NULL;
static jclass clz_Short = NULL;
static jclass clz_Int = NULL;
static jclass clz_Long = NULL;
static jclass clz_Float = NULL;
static jclass clz_Char = NULL;
/* for binding methods */
static jmethodID mid_Class_getMethods = NULL;
static jmethodID mid_Class_getMethod = NULL;
static jmethodID mid_Class_getName = NULL;
static jmethodID mid_Class_getDeclaredMethods = NULL;
static jmethodID mid_Class_isPrimitive = NULL;
static jmethodID mid_Method_getName = NULL;
static jmethodID mid_Method_getParameterTypes = NULL;
static jmethodID mid_Method_getReturnType = NULL;
static jmethodID mid_Method_getDeclaringClass = NULL;
static jmethodID mid_Method_getModifiers = NULL;
static jmethodID mid_Method_invoke = NULL;
static jmethodID mid_Double_ctor = NULL;
static jmethodID mid_Modifier_isStatic = NULL;
static jmethodID mid_Boolean_booleanValue = NULL;
static jmethodID mid_Byte_byteValue = NULL;
static jmethodID mid_Short_shortValue = NULL;
static jmethodID mid_Int_intValue = NULL;
static jmethodID mid_Long_longValue = NULL;
static jmethodID mid_Float_floatValue = NULL;
static jmethodID mid_Double_doubleValue = NULL;
static jmethodID mid_Char_charValue = NULL;

static void detach_jvm(void *ptr)
{
  if (jvm) {
    (*jvm)->DetachCurrentThread(jvm);
  }
}

static JNIEnv *get_jni(lua_State *L)
{
  JNIEnv *e;

  if (!jvm) {
    if (L) luaL_error(L, "JVM is not running");
    return NULL;
  }
  e = pthread_getspecific(jthrkey);
  if (!e) {
    int res;
    
    res = (*jvm)->GetEnv(jvm, (void**)&e, JNI_VERSION_1_4);
    if (res < 0) {
      res = (*jvm)->AttachCurrentThreadAsDaemon(jvm, (void**)&e, NULL);
    }
    if (e == NULL && L) {
      luaL_error(L, "could not attach thread to JVM: %d", res);
    }
    pthread_setspecific(jthrkey, e);
  }
  if (e == NULL && L) {
    luaL_error(L, "thread is not attached to JVM");
  }
  return e;
}

/* collect a generic java object */
static int gc_jni(lua_State *L)
{
  jobject *o = lua_touserdata(L, 1);
  JNIEnv *e = get_jni(L);
  (*e)->DeleteGlobalRef(e, *o);
  return 0;
}



#define throw_jni(L, e) throw_jni_(L, e, __FILE__, __LINE__)
static void throw_jni_(lua_State *L, JNIEnv *e, const char *file, int line)
{
  jthrowable exc;

  exc = (*e)->ExceptionOccurred(e);

  if (exc) {
    jobject msg;
    const char *str;

    (*e)->ExceptionDescribe(e);
    (*e)->ExceptionClear(e);
    msg = (*e)->CallObjectMethod(e, exc, mid_Object_toString);
    if (msg == NULL) {
      throw_jni(L, e);
      luaL_error(L, "An unspecified JVM error occurred (%s:%d)", file, line);
    }

    str = (*e)->GetStringUTFChars(e, msg, NULL);
    if (str == NULL) {
      throw_jni(L, e);
      luaL_error(L, "An unspecified JVM error occurred (%s:%d)", file, line);
    }
    lua_pushfstring(L, "%s (%s:%d)", str, file, line);
    (*e)->ReleaseStringUTFChars(e, msg, str);
    lua_error(L);
  }
}

static jclass get_class(lua_State *L, JNIEnv *e, const char *name)
{
  jclass c = (*e)->FindClass(e, name);
  if (!c) {
    throw_jni(L, e);
  }
  c = (*e)->NewGlobalRef(e, c);
  if (!c) {
    throw_jni(L, e);
  }
  return c;
}

static jmethodID get_method(lua_State *L, JNIEnv *e, jclass clz,
  const char *name, const char *sig)
{
  jmethodID id = (*e)->GetMethodID(e, clz, name, sig);
  if (id == NULL) {
    throw_jni(L, e);
  }
  return id;
}

static jmethodID get_static_method(lua_State *L, JNIEnv *e, jclass clz,
  const char *name, const char *sig)
{
  jmethodID id = (*e)->GetStaticMethodID(e, clz, name, sig);
  if (id == NULL) {
    throw_jni(L, e);
  }
  return id;
}
static int jb_index(lua_State *L)
{
  jobject o = *(jobject*)lua_touserdata(L, 1);
  JNIEnv *e = get_jni(L);
  const char *fname;
  jfieldID fid;
  jclass clazz, mthclazz;
  jobject methods;
  jsize nmethods;
  jobject lastmethod = NULL;
  int matches = 0;
  int i;

  if (lua_isnumber(L, 2)) {
    jobject retval;

    /* array style access */
    i = luaL_checknumber(L, 2);

    retval = (*e)->GetObjectArrayElement(e, o, i);
    if (!retval) {
      throw_jni(L, e);
      lua_pushnil(L);
    } else {
      jobject *robj;

      retval = (*e)->NewGlobalRef(e, retval);
      if (retval == NULL) {
        throw_jni(L, e);
      }
      robj = (jobject*)lua_newuserdata(L, sizeof(*robj));
      *robj = retval;
      luaL_getmetatable(L, LJB_OBJECT);
      lua_setmetatable(L, -2);
    }
    return 1;
  }

  fname = luaL_checklstring(L, 2, NULL);
  clazz = (*e)->GetObjectClass(e, o);
  if (clazz == NULL) {
    throw_jni(L, e);
  }

  fid = (*e)->GetFieldID(e, clazz, fname, "Ljava/lang/Object");
  if (fid) {
    jobject r = (*e)->GetObjectField(e, o, fid);
    if (r == NULL) {
      throw_jni(L, e);
      lua_pushnil(L);
    } else {
      jobject *ro;
      r = (*e)->NewGlobalRef(e, r); 
      if (r == NULL) {
        throw_jni(L, e);
      }
      ro = (jobject*)lua_newuserdata(L, sizeof(*ro));
      *ro = r;
      luaL_getmetatable(L, LJB_OBJECT);
      lua_setmetatable(L, -2);
    }
    return 1;
  }
  (*e)->ExceptionClear(e);

  methods = (*e)->CallObjectMethod(e, clazz, mid_Class_getMethods);
  if (methods == NULL) {
    throw_jni(L, e);
  }
  nmethods = (*e)->GetArrayLength(e, methods);
//  printf("%d methods; looking for %s\n", nmethods, fname);

  for (i = 0; i < nmethods; i++) {
    jobject method = (*e)->GetObjectArrayElement(e, methods, i);
    jstring jname;
    const char *name;

    if (method == NULL) {
      throw_jni(L, e);
    }
    jname = (*e)->CallObjectMethod(e, method, mid_Method_getName);
    if (jname == NULL) {
      throw_jni(L, e);
    }

    name = (*e)->GetStringUTFChars(e, jname, NULL);
    if (name == NULL) {
      throw_jni(L, e);
    }
    if (!strcmp(name, fname)) {
      matches++;
//    printf("   %s (%d)\n", name, matches);
      lastmethod = method;
    } else {
      (*e)->DeleteLocalRef(e, method);
    }
    (*e)->ReleaseStringUTFChars(e, jname, name);
  }

  if (matches == 1) {
    jobject *mobj;

    lastmethod = (*e)->NewGlobalRef(e, lastmethod);
    if (lastmethod == NULL) {
      throw_jni(L, e);
    }
    mobj = lua_newuserdata(L, sizeof(*mobj));
    *mobj = lastmethod;
    luaL_getmetatable(L, LJB_METHOD);
    lua_setmetatable(L, -2);
    return 1;
  } else if (matches) {
    struct jni_methodmatch *mmatch;

    mmatch = lua_newuserdata(L, sizeof(*mmatch) + strlen(fname));
    strcpy(mmatch->name, fname);
    mmatch->obj = (*e)->NewGlobalRef(e, o);
    luaL_getmetatable(L, LJB_METHODS);
    lua_setmetatable(L, -2);
    return 1;
  } else {
    lua_pushnil(L);
    return 1;
  }

//  mthclazz = (*e)->GetObjectClass(e, gdm);

  return 0;
}

static int gc_methodmatch(lua_State *L)
{
  struct jni_methodmatch *m = luaL_checkudata(L, 1, LJB_METHODS);
  JNIEnv *e = get_jni(L);
  
  (*e)->DeleteGlobalRef(e, m->obj);
  return 0;
}

static int do_call_method(lua_State *L, JNIEnv *e,
  jobject method, jobject selfptr, int nargs)
{
  jobject ret;
  int i;
  jobjectArray args = NULL;
  jobject cobj;
  int argoffset = lua_gettop(L) - nargs + 1;
 
//  printf("argoffset=%d nargs=%d\n", argoffset, nargs);

  args = (*e)->NewObjectArray(e, nargs, clz_Object, NULL);
//  printf("Allocating array of %d args\n", nargs);
  if (args == NULL) {
    throw_jni(L, e);
  }

  for (i = 0; i < nargs; i++) {
    int tt;
    jobject val;

    tt = lua_type(L, argoffset + i);
//    printf("param %d at pos %d is %s\n", i, argoffset + i, lua_typename(L, tt));
    switch (tt) {
      case LUA_TNIL:
        /* already initialized to nil */
        break;
      case LUA_TNUMBER:
        val = (*e)->NewObject(e, clz_Double, mid_Double_ctor,
            (double)lua_tonumber(L, argoffset + i));
        (*e)->SetObjectArrayElement(e, args, i, val);
        break;

      case LUA_TUSERDATA:
        /* FIXME: assert that it is a java object */
        val = *(jobject*)lua_touserdata(L, argoffset + i);
        (*e)->SetObjectArrayElement(e, args, i, val);
        break;

      default:
        luaL_error(L, "no handler for %s\n", lua_typename(L, tt));
    }
  }
 if (0) {
  jstring jname;
  const char *name;

  jname = (*e)->CallObjectMethod(e, method, mid_Object_toString);
  if (!jname) {
    throw_jni(L, e);
  }
  name = (*e)->GetStringUTFChars(e, jname, NULL);
  if (!name) {
    throw_jni(L, e);
  }
  printf("invoking method: %s\n", name);

    (*e)->ReleaseStringUTFChars(e, jname, name);
    (*e)->DeleteLocalRef(e, jname);
 }
//  printf("calling invoke(%p, %d args)\n", cobj, nargs);
  ret = (*e)->CallObjectMethod(e, method, mid_Method_invoke, selfptr, args);

  if (ret == NULL) {
    throw_jni(L, e);
    lua_pushnil(L);
  } else {
    jobject *robj;

    if ((*e)->IsInstanceOf(e, ret, clz_Boolean)) {
      jboolean b = (*e)->CallBooleanMethod(e, ret,
        mid_Boolean_booleanValue);
      throw_jni(L, e);

      lua_pushboolean(L, b == JNI_TRUE);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }
    if ((*e)->IsInstanceOf(e, ret, clz_Byte)) {
      jbyte b = (*e)->CallByteMethod(e, ret,
        mid_Byte_byteValue);
      throw_jni(L, e);

      lua_pushinteger(L, b);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }
    if ((*e)->IsInstanceOf(e, ret, clz_Short)) {
      jshort b = (*e)->CallByteMethod(e, ret,
        mid_Short_shortValue);
      throw_jni(L, e);

      lua_pushinteger(L, b);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }
    if ((*e)->IsInstanceOf(e, ret, clz_Int)) {
      jint b = (*e)->CallByteMethod(e, ret,
        mid_Int_intValue);
      throw_jni(L, e);

      lua_pushinteger(L, b);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }
    if ((*e)->IsInstanceOf(e, ret, clz_Long)) {
      jlong b = (*e)->CallByteMethod(e, ret,
        mid_Long_longValue);
      throw_jni(L, e);

      lua_pushinteger(L, b);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }
    if ((*e)->IsInstanceOf(e, ret, clz_Float)) {
      jfloat b = (*e)->CallByteMethod(e, ret,
        mid_Float_floatValue);
      throw_jni(L, e);

      lua_pushnumber(L, b);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }
    if ((*e)->IsInstanceOf(e, ret, clz_Double)) {
      jdouble b = (*e)->CallByteMethod(e, ret,
        mid_Double_doubleValue);
      throw_jni(L, e);

      lua_pushnumber(L, b);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }
    if ((*e)->IsInstanceOf(e, ret, clz_Char)) {
      jchar b = (*e)->CallByteMethod(e, ret,
        mid_Char_charValue);
      throw_jni(L, e);

      lua_pushinteger(L, b);
      (*e)->DeleteLocalRef(e, ret);
      return 1;
    }

    ret = (*e)->NewGlobalRef(e, ret);
    if (ret == NULL) {
      throw_jni(L, e);
    }
    robj = (jobject*)lua_newuserdata(L, sizeof(*robj));
    *robj = ret;
    luaL_getmetatable(L, LJB_OBJECT);
    lua_setmetatable(L, -2);
    return 1;
  }

  return 0;

}

static int jb_call_methodmatch(lua_State *L)
{
  struct jni_methodmatch *m = luaL_checkudata(L, 1, LJB_METHODS);
  int nargs = lua_gettop(L) - 1;
  int i;
  JNIEnv *e = get_jni(L);
  int argoffset = 2;
  jobject method = NULL;
  jclass clz;
  jobjectArray args;
  jstring jstr;

  clz = (*e)->GetObjectClass(e, m->obj);

  args = (*e)->NewObjectArray(e, nargs, clz_Object, NULL);
//  printf("Allocating array of %d args\n", nargs);
  if (args == NULL) {
    throw_jni(L, e);
  }

  jstr = (*e)->NewStringUTF(e, m->name);
  if (!jstr) {
    throw_jni(L, e);
  }

  for (i = 0; i < nargs; i++) {
    int tt;
    jobject val;

    tt = lua_type(L, argoffset + i);
//    printf("param %d at pos %d is %s\n", i, argoffset + i, lua_typename(L, tt));
    switch (tt) {
      case LUA_TNIL:
        (*e)->SetObjectArrayElement(e, args, i, clz_Object);
        break;
      case LUA_TNUMBER:
        (*e)->SetObjectArrayElement(e, args, i, clz_Number);
        break;

      case LUA_TUSERDATA:
      {
        jclass c;

        /* FIXME: assert that it is a java object */
        val = *(jobject*)lua_touserdata(L, argoffset + i);
        c = (*e)->GetObjectClass(e, val);
        (*e)->SetObjectArrayElement(e, args, i, c);
        break;
      }

      default:
        luaL_error(L, "no handler for %s\n", lua_typename(L, tt));
    }
  }
  method = (*e)->CallObjectMethod(e, clz, mid_Class_getMethod, jstr, args);
  if (method == NULL) {
    throw_jni(L, e);
  }
  return do_call_method(L, e, method, m->obj, nargs);
}

static int jb_call_methodobj(lua_State *L)
{
  jobject method = *(jobject*)lua_touserdata(L, 1);
  JNIEnv *e = get_jni(L);
  /* number of args (not including "self"; the method object) */
  int nargs = lua_gettop(L) - 1;
  /* index of first argument to the method */
  int argoffset = 2;
  jobject ret;
  int i;
  int isstatic;
  jint mod;
  jobjectArray args = NULL;
  jobject cobj;

 
  mod = (*e)->CallIntMethod(e, method, mid_Method_getModifiers);
  throw_jni(L, e);

  isstatic = (*e)->CallStaticBooleanMethod(e, clz_Modifier,
              mid_Modifier_isStatic, mod) == JNI_TRUE;
  throw_jni(L, e);

  if (!isstatic) {
//    printf("NOT STATIC\n");
    nargs--;
    argoffset++;
  } else {
//    printf("STATIC\n");
  }

  if (isstatic) {
    cobj = NULL;
  } else if (lua_isnil(L, 2)) {
    cobj = NULL;
  } else {
    luaL_checktype(L, 2, LUA_TUSERDATA); /* FIXME: assert java object */
    cobj = *(jobject*)lua_touserdata(L, 2);
  }
  return do_call_method(L, e, method, cobj, nargs);
}

static int jb_find_class(lua_State *L)
{
  const char *name = luaL_checklstring(L, 1, NULL);
  JNIEnv *e = get_jni(L);
  jclass clazz;
  jclass *cp;

  clazz = get_class(L, e, name);

  cp = lua_newuserdata(L, sizeof(*cp));
  *cp = clazz;
  luaL_getmetatable(L, LJB_CLASS);
  lua_setmetatable(L, -2);
  return 1;
}

static int jb_clazz_super(lua_State *L)
{
  jclass *cp = luaL_checkudata(L, 1, LJB_CLASS);
  JNIEnv *e = get_jni(L);
  jclass clazz;

  clazz = (*e)->GetSuperclass(e, *cp);

  if (clazz == NULL) {
    lua_pushnil(L);
    return 1;
  }
  clazz = (*e)->NewGlobalRef(e, clazz);
  throw_jni(L, e);

  cp = lua_newuserdata(L, sizeof(*cp));
  *cp = clazz;
  luaL_getmetatable(L, LJB_CLASS);
  lua_setmetatable(L, -2);
  return 1;
}

static int jb_clazz_assignable(lua_State *L)
{
//  jclass *A = luaL_checkudata(L, 1, LJB_CLASS);
//  jclass *B = luaL_checkudata(L, 2, LJB_CLASS);
  jclass *A = lua_touserdata(L, 1);
  jclass *B = lua_touserdata(L, 2);
  JNIEnv *e = get_jni(L);
  jboolean b;

  b = (*e)->IsAssignableFrom(e, *A, *B);
  throw_jni(L, e);

  lua_pushboolean(L, b == JNI_TRUE);
  return 1;
}

static int jb_len(lua_State *L)
{
  jobject o = *(jobject*)lua_touserdata(L, 1);
  JNIEnv *e = get_jni(L);
  const char *str;
  jsize l;

  if ((*e)->IsInstanceOf(e, o, clz_String) == JNI_FALSE) {
    /* it it's not a string, return the array length */
    l = (*e)->GetArrayLength(e, o);
    throw_jni(L, e);
    lua_pushnumber(L, l);
    return 1;
  }
  /* return the length in bytes */
  l = (*e)->GetStringUTFLength(e, o);
  throw_jni(L, e);
  lua_pushnumber(L, l);
  return 1;
}

static int jb_tostring(lua_State *L)
{
  jobject o = *(jobject*)lua_touserdata(L, 1);
  JNIEnv *e = get_jni(L);
  const char *str;

  if ((*e)->IsInstanceOf(e, o, clz_String) == JNI_FALSE) {
    o = (*e)->CallObjectMethod(e, o, mid_Object_toString);
    if (o == NULL) {
      throw_jni(L, e);
    }
  }
  str = (*e)->GetStringUTFChars(e, o, NULL);
  if (o == NULL) {
    throw_jni(L, e);
    luaL_error(L, "An unspecified JVM error occurred");
  }
  lua_pushstring(L, str);
  (*e)->ReleaseStringUTFChars(e, o, str);
  return 1;
}

static const luaL_reg clazz_funcs[] = {
  { "getSuperClass", jb_clazz_super },
  { "isAssignableFrom", jb_clazz_assignable },
  { "__index", jb_index },
  { "__gc", gc_jni },
  { "__tostring", jb_tostring },
  { NULL, NULL }
};

static const luaL_reg obj_funcs[] = {
  { "__index", jb_index },
  { "__tostring", jb_tostring },
  { "__gc", gc_jni },
  { "__len", jb_len },
  { NULL, NULL }
};

static const luaL_reg methodobj_funcs[] = {
  { "__index", jb_index },
  { "__tostring", jb_tostring },
  { "__gc", gc_jni },
  { "__call", jb_call_methodobj },
  { NULL, NULL }
};

static const luaL_reg methodmatch_funcs[] = {
  { "__gc", gc_methodmatch },
  { "__call", jb_call_methodmatch },
  { NULL, NULL }
};


static int jb_new_vm(lua_State *L)
{
  JavaVMInitArgs args;
  int nargs = lua_gettop(L);
  int i;
  jint res;
  JNIEnv *jenv = NULL;
  void *lib;
  const char *jvmname;
  jni_create_vm_func dl_jni_create_vm = NULL;

  if (jvm) {
    luaL_error(L, "Java VM already started");
  }

  /* locate the jvm */
  if (lua_isstring(L, 1)) {
    jvmname = lua_tostring(L, 1);
  } else {
    jvmname = DEFAULT_JVM;

    /* if unspecified, allow for LD_PRELOAD to have gotten us a jvm */
    lib = dlopen(NULL, RTLD_LAZY);
    dl_jni_create_vm = (jni_create_vm_func)dlsym(lib, "JNI_CreateJavaVM");
  }
  if (dl_jni_create_vm == NULL) {
    lib = dlopen(jvmname, RTLD_LAZY);
    if (!lib) {
      luaL_error(L, "unable to resolve %s: %s\n", jvmname, dlerror());
    }
    dl_jni_create_vm = (jni_create_vm_func)dlsym(lib, "JNI_CreateJavaVM");
    if (dl_jni_create_vm == NULL) {
      luaL_error(L, "unable to resolve JNI_CreateJavaVM in %s: %s\n",
        jvmname, dlerror());
    }
  }

  pthread_key_create(&jthrkey, detach_jvm);

  memset(&args, 0, sizeof(args));
  args.version = JNI_VERSION_1_4;
  args.ignoreUnrecognized = JNI_TRUE;
  /* parameters are passed as option strings to the JVM */
  args.nOptions = nargs - 1;
  args.options = calloc(args.nOptions, sizeof(JavaVMOption));
  LUAI_TRY_BLOCK(L) {
    for (i = 2; i <= nargs; i++) {
      args.options[i-2].optionString = (char*)luaL_checklstring(L, i, NULL);
    }
    res = dl_jni_create_vm(&jvm, (void**)&jenv, &args);
  } LUAI_TRY_FINALLY(L) {
    free(args.options);
  } LUAI_TRY_END(L);

  if (res == 0) {
    jclass objcls;

    pthread_setspecific(jthrkey, jenv);

    /* get these resolved first, as they are used in exception processing */
    clz_Object = get_class(L, jenv, "java/lang/Object");
    mid_Object_toString = get_method(L, jenv, clz_Object,
      "toString", "()Ljava/lang/String;");

    clz_Throwable = get_class(L, jenv, "java/lang/Throwable");

    clz_Double = get_class(L, jenv, "java/lang/Double");
    clz_String = get_class(L, jenv, "java/lang/String");
    clz_Method = get_class(L, jenv, "java/lang/reflect/Method");
    clz_Modifier = get_class(L, jenv, "java/lang/reflect/Modifier");
    clz_Class = get_class(L, jenv, "java/lang/Class");

    clz_Boolean = get_class(L, jenv, "java/lang/Boolean");
    mid_Boolean_booleanValue = get_method(L, jenv, clz_Boolean,
      "booleanValue", "()Z");

    clz_Byte = get_class(L, jenv, "java/lang/Byte");
    mid_Byte_byteValue = get_method(L, jenv, clz_Byte,
      "byteValue", "()B");

    clz_Short = get_class(L, jenv, "java/lang/Short");
    mid_Short_shortValue = get_method(L, jenv, clz_Short,
      "shortValue", "()S");

    clz_Int = get_class(L, jenv, "java/lang/Integer");
    mid_Int_intValue = get_method(L, jenv, clz_Int,
      "intValue", "()I");

    clz_Long = get_class(L, jenv, "java/lang/Long");
    mid_Long_longValue = get_method(L, jenv, clz_Long,
      "longValue", "()J");

    clz_Float = get_class(L, jenv, "java/lang/Float");
    mid_Float_floatValue = get_method(L, jenv, clz_Float,
      "floatValue", "()F");

    clz_Double = get_class(L, jenv, "java/lang/Double");
    mid_Double_doubleValue = get_method(L, jenv, clz_Double,
      "doubleValue", "()D");

    clz_Char = get_class(L, jenv, "java/lang/Character");
    mid_Char_charValue = get_method(L, jenv, clz_Char,
      "charValue", "()C");
    
    clz_Number = get_class(L, jenv, "java/lang/Number");

    mid_Class_getMethods = get_method(L, jenv, clz_Class,
        "getMethods", "()[Ljava/lang/reflect/Method;");
    mid_Class_getMethod = get_method(L, jenv, clz_Class,
        "getMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    mid_Class_getName = get_method(L, jenv, clz_Class,
        "getName",
        "()Ljava/lang/String;");
    mid_Class_getDeclaredMethods = get_method(L, jenv, clz_Class,
        "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    mid_Class_isPrimitive = get_method(L, jenv, clz_Class,
        "isPrimitive", "()Z");

    mid_Modifier_isStatic = get_static_method(L, jenv, clz_Modifier,
        "isStatic", "(I)Z");

    mid_Method_getName = get_method(L, jenv, clz_Method,
        "getName", "()Ljava/lang/String;");
    mid_Method_invoke = get_method(L, jenv, clz_Method,
        "invoke",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    mid_Method_getParameterTypes = get_method(L, jenv, clz_Method,
        "getParameterTypes", "()[Ljava/lang/Class;");
    mid_Method_getReturnType = get_method(L, jenv, clz_Method,
        "getReturnType", "()Ljava/lang/Class;");
    mid_Method_getDeclaringClass = get_method(L, jenv, clz_Method,
        "getDeclaringClass", "()Ljava/lang/Class;");
    mid_Method_getModifiers = get_method(L, jenv, clz_Method,
        "getModifiers", "()I");

    mid_Double_ctor = get_method(L, jenv, clz_Double,
      "<init>", "(D)V");

    lua_pushboolean(L, 1);
    return 1;
  }
  luaL_error(L, "Java VM creation failed with return value %d", res);
  return 0;
}

static int jb_stop_vm(lua_State *L)
{
#if 0 /* seems unsafe to do this, so don't do it */
  if (jvm) {
    (*jvm)->DestroyJavaVM(jvm);
    jvm = NULL;
  }
#endif
  lua_pushboolean(L, 1);
  return 1;
}

static const luaL_reg funcs[] = {
  { "startVM", jb_new_vm },
  { "stopVM", jb_stop_vm },
  { "findClass", jb_find_class },
  { "isAssignableFrom", jb_clazz_assignable },
  { NULL, NULL }
};

int luaopen_javabridge(lua_State *L)
{
  luaL_newmetatable(L, LJB_CLASS);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, clazz_funcs);

  luaL_newmetatable(L, LJB_OBJECT);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, obj_funcs);

  luaL_newmetatable(L, LJB_METHOD);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, methodobj_funcs);

  luaL_newmetatable(L, LJB_METHODS);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, methodmatch_funcs);

  luaL_register(L, "javabridge", funcs);
  lua_pushliteral(L, "DEFAULT_JVM");
  lua_pushliteral(L, DEFAULT_JVM);
  lua_settable(L, -3);
  return 1;
}

/* vim:ts=2:sw=2:et:
 */
