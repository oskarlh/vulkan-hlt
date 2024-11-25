#pragma once

#define MODIFICATIONS_STRING "Submit detailed bug reports to (" PROJECT_ISSUE_TRACKER ")\n"

#define VERSION_STRING "v" PROJECT_VERSION

#define PROJECT_NAME_AND_VERSION PROJECT_NAME " v" PROJECT_VERSION


#if !defined (SDHLCSG) && !defined (SDHLBSP) && !defined (SDHLVIS) && !defined (SDHLRAD) && !defined (SDRIPENT) //seedee
#error "You must define one of these in the settings of each project: SDHLCSG, SDHLBSP, SDHLVIS, SDHLRAD, SDRIPENT. The most likely cause is that you didn't use CMake correctly."
#endif
#if !defined (VERSION_LINUX) && !defined (VERSION_MACOS) && !defined (VERSION_OTHER)
#error "You must define one of these in the settings of each project: VERSION_LINUX, VERSION_MACOS, VERSION_OTHER. The most likely cause is that you didn't use CMake correctly."
#endif


//=====================================================================
// AJM: Different features of the tools can be undefined here
//      these are not officially beta tested, but seem to work okay

// ZHLT_* features are spread across more than one tool. Hence, changing
//      one of these settings probably means recompiling the whole set


// tool specific settings below only mean a recompile of the tool affected

	#ifdef SYSTEM_WIN32
#define HLCSG_GAMETEXTMESSAGE_UTF8 //--vluzacn
	#endif

//=====================================================================

#if _MSC_VER <1400
#define strcpy_s strcpy //--vluzacn
#endif

#if 0 //--vluzacn
// AJM: gnu compiler fix
#ifdef __GNUC__
#define _alloca __builtin_alloca
#define alloca __builtin_alloca
#endif
#endif

#include "win32fix.h"
#include "mathtypes.h"

#include <stdarg.h>
#include <stdint.h>

#ifdef SYSTEM_WIN32
#define SYSTEM_SLASH_CHAR  '\\'
#define SYSTEM_SLASH_STR   "\\"
#endif
#ifdef SYSTEM_POSIX
#define SYSTEM_SLASH_CHAR  '/'
#define SYSTEM_SLASH_STR   "/"
#endif

// the dec offsetof macro doesn't work very well...
#define myoffsetof(type,identifier) ((size_t)&((type*)0)->identifier)
#define sizeofElement(type,identifier) (sizeof((type*)0)->identifier)

#ifdef SYSTEM_POSIX
extern char*    strupr(char* string);
extern char*    strlwr(char* string);
#endif
extern const char* stristr(const char* const string, const char* const substring);
extern bool FORMAT_PRINTF(3,4) safe_snprintf(char* const dest, const size_t count, const char* const args, ...);
extern bool     safe_strncpy(char* const dest, const char* const src, const size_t count);
extern bool     safe_strncat(char* const dest, const char* const src, const size_t count);
extern bool     TerminatedString(const char* buffer, const int size);

extern char*    FlipSlashes(char* string);

extern double   I_FloatTime();

extern int      CheckParm(char* check);

extern void     DefaultExtension(char* path, const char* extension);
extern void     DefaultPath(char* path, char* basepath);
extern void     StripFilename(char* path);
extern void     StripExtension(char* path);

extern void     ExtractFile(const char* const path, char* dest);
extern void     ExtractFilePath(const char* const path, char* dest);
extern void     ExtractFileBase(const char* const path, char* dest);
extern void     ExtractFileExtension(const char* const path, char* dest);

extern std::int16_t    LittleShort(short l);
extern std::int32_t      LittleLong(int l);
extern float    LittleFloat(float l);
