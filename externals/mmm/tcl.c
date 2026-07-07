/*
** Copyright (c) 2011 D. Richard Hipp
** Copyright (c) 2011-2026 Joe Mistachkin
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to load the Tcl scripting language into a
** stubs-enabled application.
*/
#include <stdio.h>     /* NOTE: For va_list, etc. */
#include <stdlib.h>    /* NOTE: For calloc, free, etc. */
#include <string.h>    /* NOTE: For _strdup, strcat, strlen, etc. */
#include <sys/types.h> /* NOTE: For struct _stat, etc. */
#include <sys/stat.h>  /* NOTE: For _stat, etc. */
#include "tcl.h"       /* NOTE: For public Tcl API. */

#if TCL_MAJOR_VERSION<9 && !defined(Tcl_Size)
# define Tcl_Size int
#endif

/*
** This macro is used to verify that the header version of Tcl meets some
** minimum requirement.
*/
#define MINIMUM_TCL_VERSION(major, minor) \
  ((TCL_MAJOR_VERSION > (major)) || \
   ((TCL_MAJOR_VERSION == (major)) && (TCL_MINOR_VERSION >= (minor))))

/*
** File type macros.  Only the directory checking one is needed by this file.
*/
#ifndef S_ISDIR
#  define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif

/*
** This is the name of an environment variable that may refer to a Tcl library
** directory or file name.  If this environment variable is set [to anything],
** its value will be used when searching for a Tcl library to load.
*/
#ifndef TCL_PATH_ENV_VAR_NAME
#  define TCL_PATH_ENV_VAR_NAME  "MMM3_TCL_PATH"
#endif

/*
** Define the Tcl shared library name, some exported function names, and some
** cross-platform macros for use with the Tcl stubs mechanism, when enabled.
*/
#if defined(USE_TCL_STUBS)
#  if defined(_WIN32)
#    if !defined(WIN32_LEAN_AND_MEAN)
#      define WIN32_LEAN_AND_MEAN
#    endif
#    if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0502)
#      undef _WIN32_WINNT
#      define _WIN32_WINNT 0x0502 /* SetDllDirectory, Windows XP SP2 */
#    endif
#    include <windows.h>
#    ifndef TCL_DIRECTORY_SEP
#      define TCL_DIRECTORY_SEP '\\'
#    endif
#    ifndef TCL_LIBRARY_NAME
#      define TCL_LIBRARY_NAME "tcl91.dll\0"
#    endif
#    ifndef TCL_MINOR_OFFSET
#      define TCL_MINOR_OFFSET (4)
#    endif
#    ifndef dlopen
#      define dlopen(a,b) (void *)LoadLibrary((a))
#    endif
#    ifndef dlsym
#      define dlsym(a,b) GetProcAddress((HANDLE)(a),(b))
#    endif
#    ifndef dlclose
#      define dlclose(a) FreeLibrary((HANDLE)(a))
#    endif
#  else
#    include <dlfcn.h>
#    ifndef TCL_DIRECTORY_SEP
#      define TCL_DIRECTORY_SEP '/'
#    endif
#    if defined(__CYGWIN__) && (TCL_MAJOR_VERSION > 8)
#      ifndef TCL_LIBRARY_NAME
#        define TCL_LIBRARY_NAME "cygtcl9.1.dll\0"
#      endif
#      ifndef TCL_MINOR_OFFSET
#        define TCL_MINOR_OFFSET (8)
#      endif
#    elif defined(__APPLE__)
#      ifndef TCL_LIBRARY_NAME
#        define TCL_LIBRARY_NAME "libtcl9.1.dylib\0"
#      endif
#      ifndef TCL_MINOR_OFFSET
#        define TCL_MINOR_OFFSET (8)
#      endif
#    elif defined(__FreeBSD__)
#      ifndef TCL_LIBRARY_NAME
#        define TCL_LIBRARY_NAME "libtcl91.so\0"
#      endif
#      ifndef TCL_MINOR_OFFSET
#        define TCL_MINOR_OFFSET (7)
#      endif
#    else
#      ifndef TCL_LIBRARY_NAME
#        define TCL_LIBRARY_NAME "libtcl9.1.so\0"
#      endif
#      ifndef TCL_MINOR_OFFSET
#        define TCL_MINOR_OFFSET (8)
#      endif
#    endif /* defined(__CYGWIN__) */
#  endif /* defined(_WIN32) */
#  ifndef TCL_FINDEXECUTABLE_NAME
#    define TCL_FINDEXECUTABLE_NAME "_Tcl_FindExecutable\0"
#  endif
#  ifndef TCL_ZIPFSAPPHOOK_NAME
#    define TCL_ZIPFSAPPHOOK_NAME "_TclZipfs_AppHook\0"
#  endif
#  ifndef TCL_CREATEINTERP_NAME
#    define TCL_CREATEINTERP_NAME "_Tcl_CreateInterp\0"
#  endif
#  ifndef TCL_DELETEINTERP_NAME
#    define TCL_DELETEINTERP_NAME "_Tcl_DeleteInterp\0"
#  endif
#  ifndef TCL_FINALIZE_NAME
#    define TCL_FINALIZE_NAME "_Tcl_Finalize\0"
#  endif
#endif /* defined(USE_TCL_STUBS) */

/*
** If this constant is defined to non-zero, the Win32 SetDllDirectory function
** will be used during the Tcl library loading process if the path environment
** variable for Tcl was set.
*/
#ifndef TCL_USE_SET_DLL_DIRECTORY
#  if defined(_WIN32) && defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0502)
#    define TCL_USE_SET_DLL_DIRECTORY (1)
#  else
#    define TCL_USE_SET_DLL_DIRECTORY (0)
#  endif
#endif /* TCL_USE_SET_DLL_DIRECTORY */

/*
** The function types for Tcl_FindExecutable and Tcl_CreateInterp are needed
** when the Tcl library is being loaded dynamically by a stubs-enabled
** application (i.e. the inverse of using a stubs-enabled package).  These are
** the only Tcl API functions that MUST be called prior to being able to call
** Tcl_InitStubs (i.e. because it requires a Tcl interpreter).  For complete
** cleanup if the Tcl stubs initialization fails somehow, the Tcl_DeleteInterp
** and Tcl_Finalize function types are also required.
*/
#if TCL_MAJOR_VERSION>=9
typedef const char *(tcl_FindExecutableProc) (const char *);
typedef const char *(tcl_ZipfsAppHookProc) (int *, char ***);
#else
typedef void (tcl_FindExecutableProc) (const char *);
#endif
typedef Tcl_Interp *(tcl_CreateInterpProc) (void);
typedef void (tcl_DeleteInterpProc) (Tcl_Interp *);
typedef void (tcl_FinalizeProc) (void);

/*
** Are we using our own private implementation of the Tcl stubs mechanism?  If
** this is enabled, it prevents the user from having to link against the Tcl
** stubs library for the target platform, which may not be readily available.
*/
#if defined(ENABLE_TCL_PRIVATE_STUBS)
/*
** HACK: Using some preprocessor magic and a private static variable, redirect
**       the Tcl API calls [found within this file] to the function pointers
**       that will be contained in our private Tcl stubs table.  This takes
**       advantage of the fact that the Tcl headers always define the Tcl API
**       functions in terms of the "tclStubsPtr" variable when the define
**       USE_TCL_STUBS is present during compilation.
*/
#define tclStubsPtr privateTclStubsPtr
static const TclStubs *tclStubsPtr = NULL;

/*
** Create a Tcl interpreter structure that mirrors just enough fields to get
** it up and running successfully with our private implementation of the Tcl
** stubs mechanism.
*/
struct PrivateTclInterp {
  char *result;
  Tcl_FreeProc *freeProc;
  int errorLine;
  const struct TclStubs *stubTable;
};

/*
** Fossil can now be compiled without linking to the actual Tcl stubs library.
** In that case, this function will be used to perform those steps that would
** normally be performed within the Tcl stubs library.
*/
static int initTclStubs(
  Tcl_Interp *tclInterp,
  char **pzErrMsg
){
  tclStubsPtr = ((struct PrivateTclInterp *)tclInterp)->stubTable;
  if( !tclStubsPtr || (tclStubsPtr->magic!=TCL_STUB_MAGIC) ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup(
          "could not initialize Tcl stubs: incompatible mechanism");
    }
    return TCL_ERROR;
  }
  /* NOTE: At this point, the Tcl API functions should be available. */
  if( Tcl_PkgRequireEx(tclInterp, "Tcl", "8.4", 0, (void *)&tclStubsPtr)==0 ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup(
          "could not initialize Tcl stubs: incompatible version");
    }
    return TCL_ERROR;
  }
  return TCL_OK;
}
#endif /* defined(ENABLE_TCL_PRIVATE_STUBS) */

/*
** Is the loaded version of Tcl one where querying and/or calling the objProc
** for a command does not work for some reason?  The following special cases
** are currently handled by this function:
**
** 1. All versions of Tcl 8.4 have a bug that causes a crash when calling into
**    the Tcl_GetCommandFromObj function via stubs (i.e. the stubs table entry
**    is NULL).
**
** 2. Various beta builds of Tcl 8.6, namely 1 and 2, have an NRE-specific bug
**    in Tcl_EvalObjCmd (SF bug #3399564) that cause a panic when calling into
**    the objProc directly.
**
** For both of the above cases, the Tcl_EvalObjv function must be used instead
** of the more direct route of querying and calling the objProc directly.
*/
static int canUseObjProc(void){
  int major = -1, minor = -1, patchLevel = -1, type = -1;

  Tcl_GetVersion(&major, &minor, &patchLevel, &type);
  if( major<0 || minor<0 || patchLevel<0 || type<0 ){
    return 0; /* NOTE: Invalid version info, assume bad. */
  }
  if( major==8 && minor==4 ){
    return 0; /* NOTE: Disabled on Tcl 8.4, missing public API. */
  }
  if( major==8 && minor==6 && type==TCL_BETA_RELEASE && patchLevel<3 ){
    return 0; /* NOTE: Disabled on Tcl 8.6b1/b2, SF bug #3399564. */
  }
  return 1;   /* NOTE: For all other cases, assume good. */
}

/*
** Is the loaded version of Tcl one where TIP #285 (asynchronous script
** cancellation) is available?  This should return non-zero only for Tcl
** 8.6 and higher.
*/
static int canUseTip285(void){
#if MINIMUM_TCL_VERSION(8, 6)
  int major = -1, minor = -1, patchLevel = -1, type = -1;

  Tcl_GetVersion(&major, &minor, &patchLevel, &type);
  if( major<0 || minor<0 || patchLevel<0 || type<0 ){
    return 0; /* NOTE: Invalid version info, assume bad. */
  }
  return (major>8 || (major==8 && minor>=6));
#else
  return 0;
#endif
}

/*
** Returns a name for a Tcl return code.
*/
const char *getTclReturnCodeName(
  int rc,
  int nullIfOk
){
  static char zRc[TCL_INTEGER_SPACE + 17]; /* "Tcl return code\0" */

  switch( rc ){
    case TCL_OK:       return nullIfOk ? 0 : "TCL_OK";
    case TCL_ERROR:    return "TCL_ERROR";
    case TCL_RETURN:   return "TCL_RETURN";
    case TCL_BREAK:    return "TCL_BREAK";
    case TCL_CONTINUE: return "TCL_CONTINUE";
    default: {
      _snprintf(zRc, sizeof(zRc), "Tcl return code %d\0", rc);
    }
  }
  return zRc;
}

/*
** Tcl context information used by this file.
*/
struct TclContext {
  int argc;           /* Number of original arguments. */
  char **argv;        /* Full copy of the original arguments. */
  void *hLibrary;     /* The Tcl library module handle. */
  tcl_FindExecutableProc *xFindExecutable; /* Tcl_FindExecutable() pointer. */
#if TCL_MAJOR_VERSION>=9
  tcl_ZipfsAppHookProc  *xZipfsAppHook;    /* TclZipfsAppHook() pointer. */
#endif
  tcl_CreateInterpProc *xCreateInterp;     /* Tcl_CreateInterp() pointer. */
  tcl_DeleteInterpProc *xDeleteInterp;     /* Tcl_DeleteInterp() pointer. */
  tcl_FinalizeProc *xFinalize;             /* Tcl_Finalize() pointer. */
  Tcl_Interp *interp; /* The on-demand created Tcl interpreter. */
  int useObjProc;     /* Non-zero if an objProc can be called directly. */
  int useTip285;      /* Non-zero if TIP #285 is available. */
  const char *setup;  /* The optional Tcl setup script. */
};

/*
** This value is used to indicate that the size of the specified object is
** unknown or cannot be determined.
*/
#ifndef SIZE_T_UNKNOWN
#  define SIZE_T_UNKNOWN                        ((size_t)-1)
#endif

/*
** This value represents an unknown Tcl error line number.
*/
#ifndef ERROR_LINE_UNKNOWN
#  define ERROR_LINE_UNKNOWN                    ((int)-1)
#endif

/*
** This function takes a pointer to a string and returns the length of the
** string -OR- SIZE_T_UNKNOWN if it cannot be determined.
*/
static size_t strplen(
  const char **pzString
){
  if( !pzString || !*pzString ) return SIZE_T_UNKNOWN;
  return strlen(*pzString);
}

/*
** This function takes a string of the specified size and allocates a copy
** of it.  The copy is returned and must later be freed via the free()
** function.
*/
static char *strndup2(
  const char *zString,
  size_t count,
  size_t extra
){
  char *zResult = calloc(count + extra + 1 /* NUL */, sizeof(char));
  if( zResult ){
    memcpy(zResult, zString, count);
  }
  return zResult;
}

/*
** This function takes N+1 arguments, all of which must be strings and then
** allocates a single string to hold all their content.  The new string is
** returned and must be freed when it is no longer needed via the free()
** function.  If any of the strings are NULL, the result is undefined.
*/
static char *strdupall(
  const char *zFirst,
  ...
){
  size_t size = zFirst ? strlen(zFirst) : 0;
  va_list args;
  char *result;

  va_start(args, zFirst);
  while( 1 ){
    const char *arg = va_arg(args, const char *);
    if( !arg ) break;
    size += strlen(arg);
  }
  va_end(args);
  result = calloc(size + 1 /* NUL */, sizeof(char));
  if( result ){
    if( zFirst ) strcat(result, zFirst);
    va_start(args, zFirst);
    while( 1 ){
      const char *arg = va_arg(args, const char *);
      if( !arg ) break;
      strcat(result, arg);
    }
    va_end(args);
  }
  return result;
}

/*
** Return 1 if zFileName is a directory.  Return 0 if zFileName
** does not exist.  Return 2 if zFileName exists but is something
** other than a directory.
*/
static int isdir(
  const char *zFileName
){
  if( zFileName ){
    struct _stat buf;
    return _stat(zFileName, &buf) ? 0 : (S_ISDIR(buf.st_mode) ? 1 : 2);
  }
  return 0;
}

#if TCL_USE_SET_DLL_DIRECTORY
/*
** Returns non-zero if the specified character is a directory separator.
*/
#ifndef isdirsep
#  define isdirsep(a) (((a) == '/') || ((a) == '\\'))
#endif

/*
** Return the tail of a path.  The tail is the last component of the path.
** For example, the tail of "/a/b/c.d" is "c.d".
*/
static const char *tail(
  const char *zPath
){
  const char *zTail = zPath;
  if( !zTail ) return 0;
  while( zPath[0] ){
    if( isdirsep(zPath[0]) ) zTail = &zPath[1];
    zPath++;
  }
  return zTail;
}

/*
** Return the directory of a path name.  The directory is all path components
** except the last one.  For example, the directory of "/a/b/c.d" is "/a/b".
** If there is no directory, NULL is returned; otherwise, the returned memory
** should be freed via free().
*/
static char *dirname(
  const char *zPath
){
  const char *zTail = tail(zPath);
  if( zTail && zTail!=zPath ){
    size_t nDirName = zTail - zPath - 1;
    char *zDirName = calloc(nDirName + 1 /* NUL */, sizeof(char));
    if( zDirName ){
      _snprintf(zDirName, nDirName + 1, "%.*s%c", (int)nDirName, zPath, '\0');
      return zDirName;
    }
  }
  return 0;
}
#endif

/*
** When Tcl stubs support is enabled, attempts to dynamically load the Tcl
** shared library and fetch the function pointers necessary to create an
** interpreter and initialize the stubs mechanism; otherwise, simply setup
** the function pointers provided by the caller with the statically linked
** functions.
*/
static int loadTcl(
  void **phLibrary,
  tcl_FindExecutableProc **pxFindExecutable,
#if TCL_MAJOR_VERSION>=9
  tcl_ZipfsAppHookProc **pxZipfsAppHook,
#endif
  tcl_CreateInterpProc **pxCreateInterp,
  tcl_DeleteInterpProc **pxDeleteInterp,
  tcl_FinalizeProc **pxFinalize,
  char **pzErrMsg
){
#if defined(USE_TCL_STUBS)
  const char *zEnvPath = getenv(TCL_PATH_ENV_VAR_NAME);
  char aFileName[] = TCL_LIBRARY_NAME;
#endif /* defined(USE_TCL_STUBS) */

  if( !phLibrary || !pxFindExecutable || !pxCreateInterp ||
      !pxDeleteInterp || !pxFinalize ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup("invalid Tcl loader argument(s)");
    }
    return TCL_ERROR;
  }
#if defined(USE_TCL_STUBS)
#if TCL_MAJOR_VERSION<9
#if defined(_WIN32) || defined(__FreeBSD__)
  aFileName[TCL_MINOR_OFFSET-1] = '0' + TCL_MAJOR_VERSION;
#else
  aFileName[TCL_MINOR_OFFSET-2] = '0' + TCL_MAJOR_VERSION;
#endif
  aFileName[TCL_MINOR_OFFSET] = '0' + TCL_MINOR_VERSION;
#endif
  do {
    char *zFileName;
    void *hLibrary;
    if( !zEnvPath ){
      zFileName = aFileName; /* NOTE: Assume present in PATH. */
    }else if( isdir(zEnvPath)==1 ){
      size_t nEnvPath = strlen(zEnvPath);
      size_t nFileName = nEnvPath + strlen(aFileName) + 2 /* SEP, NUL */;
#if TCL_USE_SET_DLL_DIRECTORY
      SetDllDirectoryA(zEnvPath); /* NOTE: Maybe needed for "zlib1.dll". */
#endif /* TCL_USE_SET_DLL_DIRECTORY */
      /* NOTE: The environment variable contains a directory name. */
      zFileName = calloc(nFileName, sizeof(char));
      if( zFileName ){
        _snprintf(zFileName, nFileName, "%s%c%s%c", zEnvPath,
                  TCL_DIRECTORY_SEP, aFileName, '\0');
      }
    }else{
      size_t nEnvPath = strlen(zEnvPath);
      size_t nFileName = nEnvPath + 1 /* NUL */;
#if TCL_USE_SET_DLL_DIRECTORY
      char *zDirName = dirname(zEnvPath);
      if( zDirName ){
        SetDllDirectoryA(zDirName); /* NOTE: Maybe needed for "zlib1.dll". */
      }
#endif /* TCL_USE_SET_DLL_DIRECTORY */
      /* NOTE: The environment variable might contain a file name. */
      zFileName = calloc(nFileName, sizeof(char));
      if( zFileName ){
        _snprintf(zFileName, nFileName, "%s%c", zEnvPath, '\0');
      }
#if TCL_USE_SET_DLL_DIRECTORY
      if( zDirName ){
        free(zDirName); zDirName = 0;
      }
#endif /* TCL_USE_SET_DLL_DIRECTORY */
    }
    if( !zFileName ) break;
    hLibrary = dlopen(zFileName, RTLD_NOW | RTLD_GLOBAL);
    /* NOTE: If the file name was allocated, free it now. */
    if( zFileName!=aFileName ){
      free(zFileName); zFileName = 0;
    }
    if ( hLibrary ){
      tcl_FindExecutableProc *xFindExecutable;
#if TCL_MAJOR_VERSION>=9
      tcl_ZipfsAppHookProc *xZipfsAppHook;
#endif
      tcl_CreateInterpProc *xCreateInterp;
      tcl_DeleteInterpProc *xDeleteInterp;
      tcl_FinalizeProc *xFinalize;
      const char *procName = TCL_FINDEXECUTABLE_NAME;
      xFindExecutable = (tcl_FindExecutableProc *)dlsym(hLibrary, procName + 1);
      if( !xFindExecutable ){
        xFindExecutable = (tcl_FindExecutableProc *)dlsym(hLibrary, procName);
      }
      if( !xFindExecutable ){
        if( pzErrMsg ){
          *pzErrMsg = _strdup("could not locate Tcl_FindExecutable");
        }
        dlclose(hLibrary); hLibrary = 0;
        return TCL_ERROR;
      }
#if TCL_MAJOR_VERSION>=9
      procName = TCL_ZIPFSAPPHOOK_NAME;
      xZipfsAppHook = (tcl_ZipfsAppHookProc *)dlsym(hLibrary, procName+1);
      if( !xZipfsAppHook ){
        xZipfsAppHook = (tcl_ZipfsAppHookProc *)dlsym(hLibrary, procName);
      }
#endif
      procName = TCL_CREATEINTERP_NAME;
      xCreateInterp = (tcl_CreateInterpProc *)dlsym(hLibrary, procName + 1);
      if( !xCreateInterp ){
        xCreateInterp = (tcl_CreateInterpProc *)dlsym(hLibrary, procName);
      }
      if( !xCreateInterp ){
        if( pzErrMsg ){
          *pzErrMsg = _strdup("could not locate Tcl_CreateInterp");
        }
        dlclose(hLibrary); hLibrary = 0;
        return TCL_ERROR;
      }
      procName = TCL_DELETEINTERP_NAME;
      xDeleteInterp = (tcl_DeleteInterpProc *)dlsym(hLibrary, procName + 1);
      if( !xDeleteInterp ){
        xDeleteInterp = (tcl_DeleteInterpProc *)dlsym(hLibrary, procName);
      }
      if( !xDeleteInterp ){
        if( pzErrMsg ){
          *pzErrMsg = _strdup("could not locate Tcl_DeleteInterp");
        }
        dlclose(hLibrary); hLibrary = 0;
        return TCL_ERROR;
      }
      procName = TCL_FINALIZE_NAME;
      xFinalize = (tcl_FinalizeProc *)dlsym(hLibrary, procName + 1);
      if( !xFinalize ){
        xFinalize = (tcl_FinalizeProc *)dlsym(hLibrary, procName);
      }
      if( !xFinalize ){
        if( pzErrMsg ){
            *pzErrMsg = _strdup("could not locate Tcl_Finalize");
        }
        dlclose(hLibrary); hLibrary = 0;
        return TCL_ERROR;
      }
      *phLibrary = hLibrary;
      *pxFindExecutable = xFindExecutable;
#if TCL_MAJOR_VERSION>=9
      *pxZipfsAppHook = xZipfsAppHook;
#endif
      *pxCreateInterp = xCreateInterp;
      *pxDeleteInterp = xDeleteInterp;
      *pxFinalize = xFinalize;
      return TCL_OK;
    }
  } while( --aFileName[TCL_MINOR_OFFSET]>'3' ); /* Tcl 8.4+ */
  aFileName[TCL_MINOR_OFFSET] = 'x';
  if( pzErrMsg ){
    *pzErrMsg = strdupall(
        "could not load any supported Tcl shared library \"",
        aFileName, "\"", 0);
  }
  return TCL_ERROR;
#else
  *phLibrary = 0;
  *pxFindExecutable = Tcl_FindExecutable;
#if TCL_MAJOR_VERSION>=9
  *pxZipfsAppHook = (tcl_ZipfsAppHookProc *)(void *)TclZipfs_AppHook;
#endif
  *pxCreateInterp = Tcl_CreateInterp;
  *pxDeleteInterp = Tcl_DeleteInterp;
  *pxFinalize = Tcl_Finalize;
  return TCL_OK;
#endif /* defined(USE_TCL_STUBS) */
}

/*
** Sets the "argv0", "argc", and "argv" script variables in the Tcl interpreter
** based on the supplied command line arguments.
*/
static int setTclArguments(
  Tcl_Interp *pInterp,
  int argc,
  char **argv
){
  Tcl_Obj *objPtr;
  Tcl_Obj *resultObjPtr;
  Tcl_Obj *listPtr;
  int rc = TCL_OK;

  if( argc<=0 || !argv ){
    return TCL_OK;
  }
  objPtr = Tcl_NewStringObj(argv[0], -1);
  Tcl_IncrRefCount(objPtr);
  resultObjPtr = Tcl_SetVar2Ex(pInterp, "argv0", NULL, objPtr,
      TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
  Tcl_DecrRefCount(objPtr);
  if( !resultObjPtr ){
    return TCL_ERROR;
  }
  objPtr = Tcl_NewWideIntObj(argc - 1);
  Tcl_IncrRefCount(objPtr);
  resultObjPtr = Tcl_SetVar2Ex(pInterp, "argc", NULL, objPtr,
      TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
  Tcl_DecrRefCount(objPtr);
  if( !resultObjPtr ){
    return TCL_ERROR;
  }
  listPtr = Tcl_NewListObj(0, NULL);
  Tcl_IncrRefCount(listPtr);
  if( argc>1 ){
    while( --argc ){
      objPtr = Tcl_NewStringObj(*++argv, -1);
      Tcl_IncrRefCount(objPtr);
      rc = Tcl_ListObjAppendElement(pInterp, listPtr, objPtr);
      Tcl_DecrRefCount(objPtr);
      if( rc!=TCL_OK ){
        break;
      }
    }
  }
  if( rc==TCL_OK ){
    resultObjPtr = Tcl_SetVar2Ex(pInterp, "argv", NULL, listPtr,
        TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
    if( !resultObjPtr ){
      rc = TCL_ERROR;
    }
  }
  Tcl_DecrRefCount(listPtr);
  return rc;
}

/*
** Creates and initializes a Tcl interpreter.  The created Tcl interpreter is
** stored in the newly created Tcl context.  The unloadTcl() function must be
** called to properly cleanup the actions performed by this function.
*/
int createTclInterp(
  int argc,
  char **argv,
  const char *setup,
  void **pContext,
  char **pzErrMsg
){
  struct TclContext *tclContext;
  char *argv0 = 0;
  Tcl_Interp *tclInterp;

  tclContext = calloc(1, sizeof(struct TclContext));
  if( !tclContext ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup("could not allocate Tcl context");
    }
    return TCL_ERROR;
  }
  tclContext->argc = argc;
  tclContext->argv = argv;
  tclContext->setup = setup;
  /* Attempt to load the Tcl library now, if necessary. */
  if( loadTcl(&tclContext->hLibrary, &tclContext->xFindExecutable,
  #if TCL_MAJOR_VERSION >= 9
              &tclContext->xZipfsAppHook,
  #endif
              &tclContext->xCreateInterp, &tclContext->xDeleteInterp,
              &tclContext->xFinalize, pzErrMsg)!=TCL_OK ){
    free(tclContext);
    return TCL_ERROR;
  }
  if( tclContext->argc>0 && tclContext->argv ){
    argv0 = tclContext->argv[0];
  }
#if TCL_MAJOR_VERSION>=9
  if (tclContext->xZipfsAppHook) {
    tclContext->xZipfsAppHook(&tclContext->argc, &tclContext->argv);
  }
#endif
  tclContext->xFindExecutable(argv0);
  tclInterp = tclContext->xCreateInterp();
  if( !tclInterp ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup("could not create Tcl interpreter");
    }
    free(tclContext);
    return TCL_ERROR;
  }
#if defined(USE_TCL_STUBS)
#if defined(ENABLE_TCL_PRIVATE_STUBS)
  if( initTclStubs(tclInterp, pzErrMsg)!=TCL_OK ){
    tclContext->xDeleteInterp(tclInterp);
    tclInterp = 0;
    free(tclContext);
    return TCL_ERROR;
  }
#else
  if( !Tcl_InitStubs(tclInterp, "8.4", 0) ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup("could not initialize Tcl stubs");
    }
    tclContext->xDeleteInterp(tclInterp);
    tclInterp = 0;
    free(tclContext);
    return TCL_ERROR;
  }
#endif /* defined(ENABLE_TCL_PRIVATE_STUBS) */
#endif /* defined(USE_TCL_STUBS) */
  if( Tcl_InterpDeleted(tclInterp) ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup("Tcl interpreter appears to be deleted");
    }
    Tcl_DeleteInterp(tclInterp); /* TODO: Redundant? */
    tclInterp = 0;
    free(tclContext);
    return TCL_ERROR;
  }
  tclContext->interp = tclInterp;
  if( Tcl_Init(tclInterp)!=TCL_OK ){
    if( pzErrMsg ){
      *pzErrMsg = strdupall("Tcl initialization error:",
          Tcl_GetString(Tcl_GetObjResult(tclInterp)), 0);
    }
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0; /* NOTE: Redundant. */
    free(tclContext);
    return TCL_ERROR;
  }
  if( setTclArguments(tclInterp, tclContext->argc, tclContext->argv)!=TCL_OK ){
    if( pzErrMsg ){
      *pzErrMsg = strdupall("Tcl error setting arguments:",
          Tcl_GetString(Tcl_GetObjResult(tclInterp)), 0);
    }
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0; /* NOTE: Redundant. */
    free(tclContext);
    return TCL_ERROR;
  }
  /*
  ** Determine (and cache) if an objProc can be called directly for a Tcl
  ** command invoked via the tclInvoke TH1 command.
  */
  tclContext->useObjProc = canUseObjProc();
  /*
  ** Determine (and cache) whether or not we can use TIP #285 (asynchronous
  ** script cancellation).
  */
  tclContext->useTip285 = canUseTip285();
  /* If necessary, evaluate the custom Tcl setup script. */
  if( tclContext->setup &&
      Tcl_EvalEx(tclInterp, tclContext->setup, -1, 0)!=TCL_OK ){
    if( pzErrMsg ){
      *pzErrMsg = strdupall("Tcl setup script error:",
          Tcl_GetString(Tcl_GetObjResult(tclInterp)), 0);
    }
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0; /* NOTE: Redundant. */
    free(tclContext);
    return TCL_ERROR;
  }
  *pContext = tclContext;
  return TCL_OK;
}

/*
** This function sets the specified Tcl variable.
*/
int setTclVariable2(
  void *pContext,
  const char *zPart1,
  size_t nPart1,
  const char *zPart2,
  size_t nPart2,
  const char *zNewValue,
  size_t nNewValue,
  int flags,
  char **pzErrMsg
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;
  Tcl_Obj *part1Ptr, *part2Ptr, *newValuePtr;
  int rc = TCL_OK;

  if( !tclContext ){
    if( pzErrMsg ) *pzErrMsg = _strdup("invalid Tcl context");
    return TCL_ERROR;
  }
  tclInterp = tclContext->interp;
  if( !tclInterp ){
    if( pzErrMsg ) *pzErrMsg = _strdup("invalid Tcl interpreter");
    return TCL_ERROR;
  }
  Tcl_Preserve((ClientData)tclInterp);
  part1Ptr = Tcl_NewStringObj(zPart1, (Tcl_Size)nPart1);
  Tcl_IncrRefCount(part1Ptr);
  part2Ptr = Tcl_NewStringObj(zPart2, (Tcl_Size)nPart2);
  Tcl_IncrRefCount(part2Ptr);
  newValuePtr = Tcl_NewStringObj(zNewValue, (Tcl_Size)nNewValue);
  Tcl_IncrRefCount(newValuePtr);
  if( !Tcl_ObjSetVar2(tclInterp, part1Ptr, part2Ptr, newValuePtr, flags) ){
    rc = TCL_ERROR;
  }
  Tcl_DecrRefCount(newValuePtr);
  Tcl_DecrRefCount(part2Ptr);
  Tcl_DecrRefCount(part1Ptr);
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** This function unsets the specified Tcl variable.
*/
int unsetTclVariable2(
  void *pContext,
  const char *zPart1,
  const char *zPart2,
  int flags,
  char **pzErrMsg
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;

  if( !tclContext ){
    if( pzErrMsg ) *pzErrMsg = _strdup("invalid Tcl context");
    return TCL_ERROR;
  }
  tclInterp = tclContext->interp;
  if( !tclInterp ){
    if( pzErrMsg ) *pzErrMsg = _strdup("invalid Tcl interpreter");
    return TCL_ERROR;
  }
  Tcl_Preserve((ClientData)tclInterp);
  Tcl_UnsetVar2(tclInterp, zPart1, zPart2, flags);
  Tcl_Release((ClientData)tclInterp);
  return TCL_OK;
}

/*
** Evaluates the specified Tcl script.  The result is stored into pzResult,
** with the underlying memory obtained from malloc().  The length of the new
** string is stored into pnResult.
*/
int evaluateTcl(
  void *pContext,
  const char *zScript,
  size_t nScript,
  char **pzResult,
  size_t *pnResult
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;
  Tcl_Obj *objPtr;
  int rc;

  if( !tclContext ){
    if( pzResult ) *pzResult = _strdup("invalid Tcl context");
    if( pnResult ) *pnResult = strplen((const char **)pzResult);
    return TCL_ERROR;
  }
  tclInterp = tclContext->interp;
  if( !tclInterp ){
    if( pzResult ) *pzResult = _strdup("invalid Tcl interpreter");
    if( pnResult ) *pnResult = strplen((const char **)pzResult);
    return TCL_ERROR;
  }
  Tcl_Preserve((ClientData)tclInterp);
  objPtr = Tcl_NewStringObj(zScript, (Tcl_Size)nScript);
  Tcl_IncrRefCount(objPtr);
  rc = Tcl_EvalObjEx(tclInterp, objPtr, 0);
  Tcl_DecrRefCount(objPtr);
  objPtr = Tcl_GetObjResult(tclInterp);
  if( objPtr ){
    Tcl_Size nResult;
    char *zResult = Tcl_GetStringFromObj(objPtr, &nResult);
    if( pzResult ) *pzResult = strndup2(zResult, (size_t)nResult, 0);
    if( pnResult ) *pnResult = (size_t)nResult;
  }else{
    /* No Tcl result?  This should not be able to happen. */
    if( pzResult ) *pzResult = 0;
    if( pnResult ) *pnResult = SIZE_T_UNKNOWN;
  }
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** This function returns the Tcl error line number (i.e. from the previous
** script evaluation) -OR- ERROR_LINE_UNKNOWN if it cannot be determined.
*/
int getTclErrorLine(
  void *pContext,
  char **pzErrMsg
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;

  if( !tclContext ){
    if( pzErrMsg ) *pzErrMsg = _strdup("invalid Tcl context");
    return ERROR_LINE_UNKNOWN;
  }
  tclInterp = tclContext->interp;
  if( !tclInterp ){
    if( pzErrMsg ) *pzErrMsg = _strdup("invalid Tcl interpreter");
    return ERROR_LINE_UNKNOWN;
  }
  return Tcl_GetErrorLine(tclInterp);
}

/*
** Finalizes and unloads the previously loaded Tcl library, if applicable.
*/
int unloadTcl(
  void *pContext,
  char **pzErrMsg
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;
  tcl_FinalizeProc *xFinalize;
#if defined(USE_TCL_STUBS)
  void *hLibrary;
#endif /* defined(USE_TCL_STUBS) */

  if( !tclContext ){
    if( pzErrMsg ){
      *pzErrMsg = _strdup("invalid Tcl context");
    }
    return TCL_ERROR;
  }
  /*
  ** Grab the Tcl_Finalize function pointer prior to deleting the Tcl
  ** interpreter because the memory backing the Tcl stubs table will
  ** be going away.
  */
  xFinalize = tclContext->xFinalize;
  /*
  ** If the Tcl interpreter has been created, formally delete it now.
  */
  tclInterp = tclContext->interp;
  if( tclInterp ){
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0;
  }
  /*
  ** If the Tcl library is not finalized prior to unloading it, a deadlock
  ** can occur in some circumstances (i.e. the [clock] thread is running).
  */
  if( xFinalize ) xFinalize();
#if defined(USE_TCL_STUBS)
  /*
  ** If Tcl is compiled on Windows using the latest MinGW, Fossil can crash
  ** when exiting while a stubs-enabled Tcl is still loaded.  This is due to
  ** a bug in MinGW, see:
  **
  **     http://comments.gmane.org/gmane.comp.gnu.mingw.user/41724
  **
  ** The workaround is to manually unload the loaded Tcl library prior to
  ** exiting the process.
  */
  hLibrary = tclContext->hLibrary;
  if( hLibrary ){
    dlclose(hLibrary);
    tclContext->hLibrary = hLibrary = 0;
  }
#endif /* defined(USE_TCL_STUBS) */
  free(tclContext);
  return TCL_OK;
}
