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
#define LJB_METHODID "javabridge:jmethodID"
#define LJB_METHOD "javabridge:jmethod"

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
/* for binding methods */
static jmethodID mid_Class_getMethods = NULL;
static jmethodID mid_Class_isPrimitive = NULL;
static jmethodID mid_Method_getName = NULL;
static jmethodID mid_Method_getParameterTypes = NULL;
static jmethodID mid_Method_getReturnType = NULL;
static jmethodID mid_Method_getDeclaringClass = NULL;
static jmethodID mid_Method_invoke = NULL;
static jmethodID mid_Double_ctor = NULL;

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




static void throw_jni(lua_State *L, JNIEnv *e)
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
      luaL_error(L, "An unspecified JVM error occurred");
    }

    str = (*e)->GetStringUTFChars(e, msg, NULL);
    if (str == NULL) {
      throw_jni(L, e);
      luaL_error(L, "An unspecified JVM error occurred");
    }
    lua_pushstring(L, str);
    (*e)->ReleaseStringUTFChars(e, msg, str);
    lua_error(L);
  }
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

  if (mid_Class_getMethods == NULL) {
    mid_Class_getMethods = (*e)->GetMethodID(e, clazz,
        "getMethods", "()[Ljava/lang/reflect/Method;");
    if (!mid_Class_getMethods) {
      throw_jni(L, e);
    }
  }
  if (mid_Class_isPrimitive == NULL) {
    mid_Class_isPrimitive = (*e)->GetMethodID(e, clazz,
        "isPrimitive", "()Z");
    if (!mid_Class_isPrimitive) {
      throw_jni(L, e);
    }
  }

  methods = (*e)->CallObjectMethod(e, clazz, mid_Class_getMethods);
  if (methods == NULL) {
    throw_jni(L, e);
  }
  nmethods = (*e)->GetArrayLength(e, methods);
  for (i = 0; i < nmethods; i++) {
    jobject method = (*e)->GetObjectArrayElement(e, methods, i);
    jstring jname;
    const char *name;

    if (method == NULL) {
      throw_jni(L, e);
    }
    if (mid_Method_getName == NULL) {
      jclass mclass = (*e)->GetObjectClass(e, method);
      mid_Method_getName = (*e)->GetMethodID(e, mclass,
          "getName", "()Ljava/lang/String;");

      mid_Method_invoke = (*e)->GetMethodID(e, method, "invoke",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");

      (*e)->DeleteLocalRef(e, mclass);
      if (!mid_Method_getName) {
        throw_jni(L, e);
      }
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
    luaL_error(L, "multiple methods with the name %s were found", fname);
  } else {
    lua_pushnil(L);
    return 1;
  }

//  mthclazz = (*e)->GetObjectClass(e, gdm);

  return 0;
}

static int jb_call_methodid(lua_State *L)
{
  jmethodID m = *(jmethodID*)lua_touserdata(L, 1);
  JNIEnv *e = get_jni(L);
  int nargs = lua_gettop(L) - 2;
  jobject cobj;
  jobject ret;

  luaL_checktype(L, 2, LUA_TUSERDATA);
  cobj = *(jobject*)lua_touserdata(L, 2);

printf("call invoked; there are %d args (method=%p obj=%p)\n", nargs, m, cobj);

  ret = (*e)->CallObjectMethod(e, cobj, m);

  if (ret == NULL) {
    throw_jni(L, e);
    lua_pushnil(L);
  } else {
    jobject *robj;

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

static int jb_call_methodobj(lua_State *L)
{
  jobject method = *(jobject*)lua_touserdata(L, 1);
  JNIEnv *e = get_jni(L);
  int nargs = lua_gettop(L) - 2;
  int argoffset = 3;
  jobject cobj;
  jobject ret;
  jobject ptypes;
  int expectedargs, i;
  jmethodID mid;
  jobject rettype;

  if (!lua_isnil(L, 2)) {
    luaL_checktype(L, 2, LUA_TUSERDATA);
    cobj = *(jobject*)lua_touserdata(L, 2);
  } else {
    cobj = NULL;
    nargs++;
    argoffset--;
  }

  if (mid_Method_getParameterTypes == NULL) {
    jclass mclass = (*e)->GetObjectClass(e, method);

    mid_Method_getParameterTypes = (*e)->GetMethodID(e, mclass,
        "getParameterTypes", "()[Ljava/lang/Class;");
    if (!mid_Method_getParameterTypes) {
      throw_jni(L, e);
    }

    mid_Method_getReturnType = (*e)->GetMethodID(e, mclass,
        "getReturnType", "()Ljava/lang/Class;");
    if (!mid_Method_getReturnType) {
      throw_jni(L, e);
    }
    
    mid_Method_getDeclaringClass = (*e)->GetMethodID(e, mclass,
        "getDeclaringClass", "()Ljava/lang/Class;");
    if (!mid_Method_getDeclaringClass) {
      throw_jni(L, e);
    }
    (*e)->DeleteLocalRef(e, mclass);
  }

  ptypes = (*e)->CallObjectMethod(e, method, mid_Method_getParameterTypes);
  if (!ptypes) {
    throw_jni(L, e);
  }
  expectedargs = (*e)->GetArrayLength(e, ptypes);
#if 0
  if (nargs != expectedargs) {
    const char *name;
    jstring jname;

    (*e)->DeleteLocalRef(e, ptypes);
    jname = (*e)->CallObjectMethod(e, method, mid_Method_getName);
    if (!jname) {
      throw_jni(L, e);
    }
    name = (*e)->GetStringUTFChars(e, jname, NULL);
    if (!name) {
      throw_jni(L, e);
    }
    lua_pushfstring(L, "%s: %d args expected, but passed %d",
      name, expectedargs, nargs);
    (*e)->ReleaseStringUTFChars(e, jname, name);
    (*e)->DeleteLocalRef(e, jname);
    lua_error(L);
    return 0;
  }
#endif

  rettype = (*e)->CallObjectMethod(e, method, mid_Method_getReturnType);
  if (!rettype) {
    throw_jni(L, e);
  }

printf("call invoked; there are %d args; expected %d (method=%p obj=%p)\n",
nargs, expectedargs, method, cobj);

  if (nargs) {
    /* if we have args to pass in, then we restort to using Method.invoke,
     * as it will follow the java rules for method binding and dispatch */
    jobjectArray args = NULL;
    jobject nil = (*e)->NewGlobalRef(e, NULL);
    if (nil == NULL) {
      throw_jni(L, e);
    }

    if (cobj == NULL) {
      nargs--;
      argoffset++;
    }

    args = (*e)->NewObjectArray(e, nargs, clz_Object, nil);
    if (args == NULL) {
      throw_jni(L, e);
    }

    for (i = 0; i < nargs; i++) {
      int tt;
      jobject val;

      tt = lua_type(L, argoffset + i);
      printf("param %d is %s\n", i, lua_typename(L, tt));
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
#if 0
      jobject pval;
      jboolean prim;

      pval = (*e)->GetObjectArrayElement(e, ptypes, i);
      if (pval == NULL) {
        throw_jni(L, e);
      }
      prim = (*e)->CallBooleanMethod(e, pval, mid_Class_isPrimitive);
      throw_jni(L, e);
      if (prim == JNI_TRUE) {
        printf("param %d is a primitive\n", i);
      } else {
        printf("param %d is NOT prim\n", i);
      }
#endif
    }
    if (cobj == NULL) {
      cobj = (*e)->NewGlobalRef(e, NULL);
      if (cobj == NULL) {
        throw_jni(L, e);
        printf("making a NULL reference, but got %p\n", cobj);
      }
    }
    printf("calling invoke(%p, %d args)\n", cobj, nargs);
    ret = (*e)->CallObjectMethod(e, method, mid_Method_invoke, cobj, args);
  } else {
    mid = (*e)->FromReflectedMethod(e, method);
    if (mid == NULL) {
      throw_jni(L, e);
    }
    fflush(stdout);

    if (cobj) {
      ret = (*e)->CallObjectMethod(e, cobj, mid);
    } else {
      jobject clazz = (*e)->CallObjectMethod(e, method,
          mid_Method_getDeclaringClass);
      if (clazz == NULL) {
        throw_jni(L, e);
      }
      ret = (*e)->CallStaticObjectMethod(e, clazz, mid);
      if (ret == NULL) {
        throw_jni(L, e);
      }
      (*e)->DeleteLocalRef(e, clazz);
    }
  }

  if (ret == NULL) {
    throw_jni(L, e);
    lua_pushnil(L);
  } else {
    jobject *robj;

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

static int jb_find_class(lua_State *L)
{
  const char *name = luaL_checklstring(L, 1, NULL);
  JNIEnv *e = get_jni(L);
  jclass clazz;
  jclass *cp;

  clazz = (*e)->FindClass(e, name);

  if (clazz == NULL) {
    throw_jni(L, e);
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
  jclass *A = luaL_checkudata(L, 1, LJB_CLASS);
  jclass *B = luaL_checkudata(L, 2, LJB_CLASS);
  JNIEnv *e = get_jni(L);
  jboolean b;

  b = (*e)->IsAssignableFrom(e, *A, *B);

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

static const luaL_reg methodid_funcs[] = {
  { "__call", jb_call_methodid },
  { NULL, NULL }
};

static const luaL_reg methodobj_funcs[] = {
  { "__index", jb_index },
  { "__tostring", jb_tostring },
  { "__gc", gc_jni },
  { "__call", jb_call_methodobj },
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

    clz_Throwable = (*jenv)->FindClass(jenv, "java/lang/Throwable");
    if (!clz_Throwable) {
      luaL_error(L, "Unable to locate class java/lang/Throwable");
    }
    clz_Throwable = (*jenv)->NewGlobalRef(jenv, clz_Throwable);
    clz_Object = (*jenv)->FindClass(jenv, "java/lang/Object");
    clz_Object = (*jenv)->NewGlobalRef(jenv, clz_Object);
    clz_Double = (*jenv)->FindClass(jenv, "java/lang/Double");
    clz_Double = (*jenv)->NewGlobalRef(jenv, clz_Double);
    mid_Double_ctor = (*jenv)->GetMethodID(jenv, clz_Double,
      "<init>", "(D)V");
    if (!mid_Double_ctor) {
      throw_jni(L, jenv);
    }

    mid_Object_toString = (*jenv)->GetMethodID(jenv, clz_Object,
      "toString", "()Ljava/lang/String;");
    if (!mid_Object_toString) {
      luaL_error(L, "Unable to locate java/lang/Object::toString");
    }

    clz_String = (*jenv)->FindClass(jenv, "java/lang/String");
    if (!clz_String) {
      luaL_error(L, "Unable to locate class java/lang/String");
    }
    clz_String = (*jenv)->NewGlobalRef(jenv, clz_String);

    lua_pushboolean(L, 1);
    return 1;
  }
  luaL_error(L, "Java VM creation failed with return value %d", res);
  return 0;
}

static int jb_stop_vm(lua_State *L)
{
  if (jvm) {
    (*jvm)->DestroyJavaVM(jvm);
    jvm = NULL;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static const luaL_reg funcs[] = {
  { "startVM", jb_new_vm },
  { "stopVM", jb_stop_vm },
  { "findClass", jb_find_class },
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

  luaL_newmetatable(L, LJB_METHODID);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, methodid_funcs);

  luaL_newmetatable(L, LJB_METHOD);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, methodobj_funcs);

  luaL_register(L, "javabridge", funcs);
  lua_pushliteral(L, "DEFAULT_JVM");
  lua_pushliteral(L, DEFAULT_JVM);
  lua_settable(L, -3);
  return 1;
}

/* vim:ts=2:sw=2:et:
 */
