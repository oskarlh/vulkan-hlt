#include "cmdlib.h"
#include "messages.h"
#include "hlassert.h"
#include "log.h"
#include "filelib.h"

#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "scriplib.h"
#include "cli_option_defaults.h"

const char*           g_Program = "Uninitialized variable ::g_Program";
char            g_Mapname[_MAX_PATH] = "Uninitialized variable ::g_Mapname";
char            g_Wadpath[_MAX_PATH] = "Uninitialized variable ::g_Wadpath";

developer_level_t g_developer = cli_option_defaults::developer;
bool            g_verbose = cli_option_defaults::verbose;
bool            g_log = cli_option_defaults::log;

unsigned long   g_clientid = 0;
unsigned long   g_nextclientid = 0;

static FILE*    CompileLog = nullptr;
static bool     fatal = false;

bool twice = false;
bool useconsole = false;
FILE *conout = nullptr;

int				g_lang_count = 0;
const int		g_lang_max = 1024;
char*			g_lang[g_lang_max][2];

////////

void            ResetTmpFiles()
{
    if (g_log)
    {
        char            filename[_MAX_PATH];

        safe_snprintf(filename, _MAX_PATH, "%s.bsp", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.inc", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p0", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p1", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p2", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p3", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.prt", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.pts", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.lin", g_Mapname);
        std::filesystem::remove(filename);


        safe_snprintf(filename, _MAX_PATH, "%s.hsz", g_Mapname);
        std::filesystem::remove(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.pln", g_Mapname);
        std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b0", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b1", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b2", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b3", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.wa_", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.ext", g_Mapname);
		std::filesystem::remove(filename);
    }
}

void            ResetLog()
{
    if (g_log)
    {
        char            logfilename[_MAX_PATH];

        safe_snprintf(logfilename, _MAX_PATH, "%s.log", g_Mapname);
        std::filesystem::remove(logfilename);
    }
}

void            ResetErrorLog()
{
    if (g_log)
    {
        char            logfilename[_MAX_PATH];

        safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
        std::filesystem::remove(logfilename);
    }
}

void            CheckForErrorLog()
{
    if (g_log)
    {
        char            logfilename[_MAX_PATH];

        safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
        if (std::filesystem::exists(logfilename))
        {
            Log(">> There was a problem compiling the map.\n"
                ">> Check the file %s.log for the cause.\n",
                 g_Mapname);
            exit(1);
        }
    }
}

///////

void            LogError(const char* const message)
{
    if (g_log && CompileLog)
    {
        char            logfilename[_MAX_PATH];
        FILE*           ErrorLog = nullptr;

        safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
        ErrorLog = fopen(logfilename, "a");

        if (ErrorLog)
        {
            fprintf(ErrorLog, "%s: %s\n", g_Program, message);
            fflush(ErrorLog);
            fclose(ErrorLog);
            ErrorLog = nullptr;
        }
        else
        {
            fprintf(stderr, Localize ("ERROR: Could not open error logfile %s"), logfilename);
            fflush(stderr);
			if (twice)
			{
				fprintf (conout, Localize ("ERROR: Could not open error logfile %s"), logfilename);
				fflush (conout);
			}
        }
    }
}

void       OpenLog(const int clientid)
{
    if (g_log)
    {
        char            logfilename[_MAX_PATH];

        {
            safe_snprintf(logfilename, _MAX_PATH, "%s.log", g_Mapname);
        }
        CompileLog = fopen(logfilename, "a");

        if (!CompileLog)
        {
            fprintf(stderr, Localize ("ERROR: Could not open logfile %s"), logfilename);
            fflush(stderr);
			if (twice)
			{
				fprintf (conout, Localize ("ERROR: Could not open logfile %s"), logfilename);
				fflush (conout);
			}
        }
    }
}

void     CloseLog()
{
    if (g_log && CompileLog)
    {
        LogEnd();
        fflush(CompileLog);
        fclose(CompileLog);
        CompileLog = nullptr;
    }
}

//
//  Every function up to this point should check g_log, the functions below should not
//

#ifdef SYSTEM_WIN32
// AJM: fprintf/flush wasnt printing newline chars correctly (prefixed with \r) under win32
//      due to the fact that those streams are in byte mode, so this function prefixes 
//      all \n with \r automatically.
//      NOTE: system load may be more with this method, but there isnt that much logging going
//      on compared to the time taken to compile the map, so its negligable.
void            Safe_WriteLog(const char* const message)
{
    const char* c;
    
    if (!CompileLog)
        return;

    c = &message[0];

    while (1)
    {
        if (!*c)
            return; // end of string

        if (*c == '\n')
            fputc('\r', CompileLog);

        fputc(*c, CompileLog);

        c++;
    }
}
#endif

void            WriteLog(const char* const message)
{

#ifndef SYSTEM_WIN32
    if (CompileLog)
    {
        fprintf(CompileLog, "%s", message); //fprintf(CompileLog, message); //--vluzacn
        fflush(CompileLog);
    }
#else
    Safe_WriteLog(message);
#endif

    fprintf(stdout, "%s", message); //fprintf(stdout, message); //--vluzacn
    fflush(stdout);
	if (twice)
	{
		fprintf (conout, "%s", message);
		fflush (conout);
	}
}

// =====================================================================================
//  CheckFatal 
// =====================================================================================
void            CheckFatal()
{
    if (fatal)
    {
        hlassert(false);
        exit(1);
    }
}

#define MAX_ERROR   2048
#define MAX_WARNING 2048
#define MAX_MESSAGE 2048

// =====================================================================================
//  Error
//      for formatted error messages, fatals out
// =====================================================================================
void FORMAT_PRINTF(1,2)      Error(const char* const error, ...)
{
    char            message[MAX_ERROR];
    char            message2[MAX_ERROR];
    va_list         argptr;
    
 /*#if defined( SYSTEM_WIN32 ) && !defined( __MINGW32__ ) && !defined( __BORLANDC__ )
    {
        char* wantint3 = getenv("WANTINT3");
		if (wantint3)
		{
			if (atoi(wantint3))
			{
				__asm
				{
					int 3;
				}
			}
		}
    }
#endif*/

    va_start(argptr, error);
    vsnprintf(message, MAX_ERROR, Localize (error), argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", Localize ("Error: "), message);
    WriteLog(message2);
    LogError(message2);

    fatal = 1;
    CheckFatal();
}

// =====================================================================================
//  Fatal
//      For formatted 'fatal' warning messages
//      automatically appends an extra newline to the message
//      This function sets a flag that the compile should abort before completing
// =====================================================================================
void FORMAT_PRINTF(2,3)      Fatal(assume_msgs msgid, const char* const warning, ...)
{
    char            message[MAX_WARNING];
    char            message2[MAX_WARNING];

    va_list         argptr;

    va_start(argptr, warning);
    vsnprintf(message, MAX_WARNING, Localize (warning), argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", Localize ("Error: "), message);
    WriteLog(message2);
    LogError(message2);

    {
        char            message[MAX_MESSAGE];
        const MessageTable_t* msg = GetAssume(msgid);

        safe_snprintf(message, MAX_MESSAGE, "%s\n%s%s\n%s%s\n", Localize (msg->title), Localize ("Description: "), Localize (msg->text), Localize ("Howto Fix: "), Localize (msg->howto));
        PrintOnce("%s", message);
    }

    fatal = 1;
}

// =====================================================================================
//  PrintOnce
//      This function is only callable one time. Further calls will be ignored
// =====================================================================================
void FORMAT_PRINTF(1,2)      PrintOnce(const char* const warning, ...)
{
    char            message[MAX_WARNING];
    char            message2[MAX_WARNING];
    va_list         argptr;
    static int      count = 0;

    if (count > 0) // make sure it only gets called once
    {
        return;
    }
    count++;

    va_start(argptr, warning);
    vsnprintf(message, MAX_WARNING, Localize (warning), argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", Localize ("Error: "), message);
    WriteLog(message2);
    LogError(message2);
}

// =====================================================================================
//  Warning
//      For formatted warning messages
//      automatically appends an extra newline to the message
// =====================================================================================
void FORMAT_PRINTF(1,2)      Warning(const char* const warning, ...)
{
    char            message[MAX_WARNING];
    char            message2[MAX_WARNING];

    va_list         argptr;

    va_start(argptr, warning);
    vsnprintf(message, MAX_WARNING, Localize (warning), argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", Localize ("Warning: "), message);
    WriteLog(message2);
}

// =====================================================================================
//  Verbose
//      Same as log but only prints when in verbose mode
// =====================================================================================
void FORMAT_PRINTF(1,2)      Verbose(const char* const warning, ...)
{
    if (g_verbose)
    {
        char            message[MAX_MESSAGE];

        va_list         argptr;

        va_start(argptr, warning);
        vsnprintf(message, MAX_MESSAGE, Localize (warning), argptr);
        va_end(argptr);

        WriteLog(message);
    }
}

// =====================================================================================
//  Developer
//      Same as log but only prints when in developer mode
// =====================================================================================
void FORMAT_PRINTF(2,3)      Developer(developer_level_t level, const char* const warning, ...)
{
    if (level <= g_developer)
    {
        char            message[MAX_MESSAGE];

        va_list         argptr;

        va_start(argptr, warning);
        vsnprintf(message, MAX_MESSAGE, Localize (warning), argptr);
        va_end(argptr);

        WriteLog(message);
    }
}

// =====================================================================================
//  DisplayDeveloperLevel
// =====================================================================================
static void     DisplayDeveloperLevel()
{
    char            message[MAX_MESSAGE];

    safe_strncpy(message, "Developer messages enabled : [", MAX_MESSAGE);
    if (g_developer >= DEVELOPER_LEVEL_MEGASPAM)
    {
        safe_strncat(message, "MegaSpam ", MAX_MESSAGE);
    }
    if (g_developer >= DEVELOPER_LEVEL_SPAM)
    {
        safe_strncat(message, "Spam ", MAX_MESSAGE);
    }
    if (g_developer >= DEVELOPER_LEVEL_FLUFF)
    {
        safe_strncat(message, "Fluff ", MAX_MESSAGE);
    }
    if (g_developer >= DEVELOPER_LEVEL_MESSAGE)
    {
        safe_strncat(message, "Message ", MAX_MESSAGE);
    }
    if (g_developer >= DEVELOPER_LEVEL_WARNING)
    {
        safe_strncat(message, "Warning ", MAX_MESSAGE);
    }
    if (g_developer >= DEVELOPER_LEVEL_ERROR)
    {
        safe_strncat(message, "Error", MAX_MESSAGE);
    }
    if (g_developer)
    {
        safe_strncat(message, "]\n", MAX_MESSAGE);
        Log("%s", message);
    }
}

// =====================================================================================
//  Log
//      For formatted log output messages
// =====================================================================================
void FORMAT_PRINTF(1,2)      Log(const char* const warning, ...)
{
    char            message[MAX_MESSAGE];

    va_list         argptr;

    va_start(argptr, warning);
    vsnprintf(message, MAX_MESSAGE, Localize (warning), argptr);
    va_end(argptr);

    WriteLog(message);
}

// =====================================================================================
//  LogArgs
// =====================================================================================
static void     LogArgs(int argc, char** argv)
{
    int             i;

    Log("Command line: ");
    for (i = 0; i < argc; i++)
    {
        if (strchr(argv[i], ' '))
        {
            Log("\"%s\" ", argv[i]); //Log("\"%s\"", argv[i]); //--vluzacn
        }
        else
        {
            Log("%s ", argv[i]);
        }
    }
    Log("\n");
}

// =====================================================================================
//  Banner
// =====================================================================================
void            Banner()
{
    Log("%s " VERSION_STRING
		" " PLATFORM_VERSION
		" (%s)\n", g_Program, __DATE__);
    //Log("BUGGY %s (built: %s)\nUse at own risk.\n", g_Program, __DATE__);

    Log(PROJECT_DESCRIPTION "\n"
        "Based on code modifications by Sean 'Zoner' Cavanaugh, Vluzacn, and seedee.\n"
        "Based on Valve's version, modified with permission.\n"
        MODIFICATIONS_STRING);

}

// =====================================================================================
//  LogStart
// =====================================================================================
void            LogStart(int argc, char** argv)
{
    Banner();
    Log("-----  BEGIN  %s -----\n", g_Program);
    LogArgs(argc, argv);
    DisplayDeveloperLevel();
}

// =====================================================================================
//  LogEnd
// =====================================================================================
void            LogEnd()
{
    Log("\n-----   END   %s -----\n\n\n\n", g_Program);
}

// =====================================================================================
//  hlassume
//      my assume
// =====================================================================================
void            hlassume(bool exp, assume_msgs msgid)
{
    if (!exp)
    {
        char            message[MAX_MESSAGE];
        const MessageTable_t* msg = GetAssume(msgid);

        safe_snprintf(message, MAX_MESSAGE, "%s\n%s%s\n%s%s\n", Localize (msg->title), Localize ("Description: "), Localize (msg->text), Localize ("Howto Fix: "), Localize (msg->howto));
        Error("%s", message);
    }
}

// =====================================================================================
//  seconds_to_hhmm
// =====================================================================================
static void seconds_to_hhmm(unsigned int elapsed_time, unsigned& days, unsigned& hours, unsigned& minutes, unsigned& seconds)
{
    seconds = elapsed_time % 60;
    elapsed_time /= 60;

    minutes = elapsed_time % 60;
    elapsed_time /= 60;

    hours = elapsed_time % 24;
    elapsed_time /= 24;

    days = elapsed_time;
}

// =====================================================================================
//  LogTimeElapsed
// =====================================================================================
void LogTimeElapsed(float elapsed_time)
{
    unsigned days = 0;
    unsigned hours = 0;
    unsigned minutes = 0;
    unsigned seconds = 0;

    seconds_to_hhmm(elapsed_time, days, hours, minutes, seconds);

    if (days)
    {
        Log("%.2f seconds elapsed [%ud %uh %um %us]\n", elapsed_time, days, hours, minutes, seconds);
    }
    else if (hours)
    {
        Log("%.2f seconds elapsed [%uh %um %us]\n", elapsed_time, hours, minutes, seconds);
    }
    else if (minutes)
    {
        Log("%.2f seconds elapsed [%um %us]\n", elapsed_time, minutes, seconds);
    }
    else
    {
        Log("%.2f seconds elapsed\n", elapsed_time);
    }
}

int InitConsole (int argc, char **argv)
{
	twice = false;
	useconsole = false;
	return 0;
}

void FORMAT_PRINTF(1,2) PrintConsole(const char* const warning, ...)
{
    char            message[MAX_MESSAGE];

    va_list         argptr;

    va_start(argptr, warning);
//ZHLT_LANGFILE: don't call function Localize here because of performance issue
    vsnprintf(message, MAX_MESSAGE, warning, argptr);
    va_end(argptr);

    if (useconsole)
	{
		fprintf (conout, "%s", message);
		fflush (conout);
	}
	else
	{
		fprintf (stdout, "%s", message);
	}
}

int loadlangfileline (char *line, int n, FILE *f)
{
	int i = 0, c = 0;
	bool special = false;
	while (1)
	{
		c = fgetc (f);
		if (c == '\r')
			continue;
		if (c == '\n' || c == EOF)
			break;
		if (c == '\\' && !special)
		{
			special = true;
		}
		else
		{
			if (special)
			{
				switch (c)
				{
				case 'n': c = '\n'; break;
				case 't': c = '\t'; break;
				case 'v': c = '\v'; break;
				case 'b': c = '\b'; break;
				case 'r': c = '\r'; break;
				case 'f': c = '\f'; break;
				case 'a': c = '\a'; break;
				case '\\': c = '\\'; break;
				case '?': c = '\?'; break;
				case '\'': c = '\''; break;
				case '"': c = '\"'; break;
				default: break;
				}
			}
			if (i < n - 1)
				line[i++] = c;
			else
			{
				Warning ("line too long in localization file");
				break;
			}
			special = false;
		}
	}
	line[i] = '\0';
	if (c == EOF)
		return 1;
	return 0;
}
const char * Localize (const char *s)
{
	int i;
	for (i=0; i<g_lang_count; i++)
	{
		if (!strcmp (g_lang[i][0], s))
		{
			return g_lang[i][1];
		}
	}
	return s;
}
void LoadLangFile (const char *name, std::filesystem::path programDirectoryPath)
{
	std::filesystem::path filePath;
	char line1[MAXTOKEN];
	char line2[MAXTOKEN];
	FILE *f = nullptr;
	if (!f)
	{
        filePath = name;
		f = fopen (filePath.c_str(), "r");
	}
	if (!f)
	{
		filePath = programDirectoryPath / name;
		f = fopen (filePath.c_str(), "r");
	}
	if (!f)
	{
		Warning ("can not open file: '%s'", name);
		return;
	}
	while (1)
	{
		if (loadlangfileline (line1, MAXTOKEN, f) == 1)
			break;
		loadlangfileline (line2, MAXTOKEN, f);
		if (g_lang_count < g_lang_max)
		{
			g_lang[g_lang_count][0] = strdup (line1);
			g_lang[g_lang_count][1] = strdup (line2);
			g_lang_count++;
		}
		else
		{
			Warning ("too many lines in localization file");
			break;
		}
	}
	fclose (f);
	Log ("Localization file: '%s'\n", filePath.c_str());
}
