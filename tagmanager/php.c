/*
*   $Id: php.c 5856 2011-06-17 22:52:43Z colombanw $
*
*   Copyright (c) 2000, Jesus Castagnetto <jmcastagnetto@zkey.com>
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License.
*
*   This module contains functions for generating tags for the PHP web page
*   scripting language. Only recognizes functions and classes, not methods or
*   variables.
*
*   Parsing PHP defines by Pavel Hlousek <pavel.hlousek@seznam.cz>, Apr 2003.
*/

/*
*   INCLUDE FILES
*/


#include "general.h"  /* must always come first */

#include <string.h>
#include "main.h"
#include "parse.h"
#include "read.h"
#include "vstring.h"
#include "keyword.h"
#include "get.h"

/*
*   DATA DEFINITIONS
*/
typedef enum {
	K_CLASS, K_DEFINE, K_FUNCTION, K_VARIABLE
} phpKind;

static kindOption PhpKinds [] = {
	{ TRUE, 'c', "class",    "classes" },
	{ TRUE, 'd', "define",   "constant definitions" },
	{ TRUE, 'f', "function", "functions" },
	{ TRUE, 'v', "variable", "variables" }
};

/*
*   FUNCTION DEFINITIONS
*/

/* JavaScript patterns are duplicated in jscript.c */

/*
 * Cygwin doesn't support non-ASCII characters in character classes.
 * This isn't a good solution to the underlying problem, because we're still
 * making assumptions about the character encoding.
 * Really, these regular expressions need to concentrate on what marks the
 * end of an identifier, and we need something like iconv to take into
 * account the user's locale (or an override on the command-line.)
 */
/*
#ifdef __CYGWIN__
#define ALPHA "[:alpha:]"
#define ALNUM "[:alnum:]"
#else
#define ALPHA "A-Za-z\x7f-\xff"
#define ALNUM "0-9A-Za-z\x7f-\xff"
#endif
*/
/* "A-Za-z\x7f-\xff" fails on other locales than "C" and so skip it */
#define ALPHA "[:alpha:]"
#define ALNUM "[:alnum:]"

langType lang;

static void function_cb(const char *line, const regexMatch *matches, unsigned int count);
static void parsePhpClass(const char *line, const regexMatch *matches, unsigned int count);
static void skipToMatch (const char *const pair);

static void installPHPRegex (const langType language)
{
	lang = language;
	
	addTagRegex(language, "^[ \t]*((final|abstract)[ \t]+)*class[ \t]+([" ALPHA "_][" ALNUM "_]*)",
		"\\3", "c,class,classes", NULL);
	addTagRegex(language, "^[ \t]*interface[ \t]+([" ALPHA "_][" ALNUM "_]*)",
		"\\1", "i,interface,interfaces", NULL);
	addTagRegex(language, "^[ \t]*define[ \t]*\\([ \t]*['\"]?([" ALPHA "_][" ALNUM "_]*)",
		"\\1", "m,macro,macros", NULL);
	addTagRegex(language, "^[ \t]*const[ \t]*([" ALPHA "_][" ALNUM "_]*)[ \t]*[=;]",
		"\\1", "m,macro,macros", NULL);
	/* Note: Using [] to match words is wrong, but using () doesn't seem to match 'function' on its own */
	addCallbackRegex(language,
		"^[ \t]*[(public|protected|private|static|final)[ \t]*]*[ \t]*function[ \t]+&?[ \t]*([" ALPHA "_][" ALNUM "_]*)[[:space:]]*(\\(.*\\))",
		NULL, function_cb);
	addTagRegex(language, "^[ \t]*(\\$|::\\$|\\$this->)([" ALPHA "_][" ALNUM "_]*)[ \t]*=",
		"\\2", "v,variable,variables", NULL);
	addTagRegex(language, "^[ \t]*((var|public|protected|private|static)[ \t]+)+\\$([" ALPHA "_][" ALNUM "_]*)[ \t]*[=;]",
		"\\3", "v,variable,variables", NULL);

	/* My function regex class */
	/* addCallbackRegex(language, "^[ \t]*((final|abstract)[ \t]+)class[ \t]+([" ALPHA "_][" ALNUM "_]*)",*/
	addCallbackRegex(language, "[ \t]+class[ \t]+([" ALPHA "_][" ALNUM "_]*)",
		NULL, parsePhpClass);

	/* function regex is covered by PHP regex */
	addTagRegex (language, "(^|[ \t])([A-Za-z0-9_]+)[ \t]*[=:][ \t]*function[ \t]*\\(",
		"\\2", "j,jsfunction,javascript functions", NULL);
	addTagRegex (language, "(^|[ \t])([A-Za-z0-9_.]+)\\.([A-Za-z0-9_]+)[ \t]*=[ \t]*function[ \t]*\\(",
		"\\2.\\3", "j,jsfunction,javascript functions", NULL);
	addTagRegex (language, "(^|[ \t])([A-Za-z0-9_.]+)\\.([A-Za-z0-9_]+)[ \t]*=[ \t]*function[ \t]*\\(",
		"\\3", "j,jsfunction,javascript functions", NULL);
}

void printTagEntry(const tagEntryInfo *tag)
{
	fprintf(stderr, "Tag: %s (%s) [ impl: %s, scope: %s, type: %s\n", tag->name,
	tag->kindName, tag->extensionFields.implementation, tag->extensionFields.scope[1],
	tag->extensionFields.varType);
}

static void parsePhpClass(const char *line, const regexMatch *matches, unsigned int count)
{
	char * ln, *className;
	
	className = xMalloc(matches[count - 1].length + 1, char);
	strncpy(className, line + matches[count - 1].start, matches[count - 1].length);
	*(className+matches[count - 1].length) = '\x0';
	printf("Name: %s\n", className);
	
	
	skipToMatch("{}");
	
	
	eFree(className);
}

static void function_cb(const char *line, const regexMatch *matches, unsigned int count)
{
	char *name, *arglist;
	char kind = 'f';
	static const char *kindName = "function";
	tagEntryInfo e;
	const regexMatch *match_funcname = NULL;
	const regexMatch *match_arglist = NULL;

	if (count > 2)
	{
		match_funcname = &matches[count - 2];
		match_arglist = &matches[count - 1];
	}

	if (match_funcname != NULL)
	{
		name = xMalloc(match_funcname->length + 1, char);
		strncpy(name, line + match_funcname->start, match_funcname->length);
		*(name+match_funcname->length) = '\x0';
		arglist = xMalloc(match_arglist->length + 1, char);
		strncpy(arglist, line + match_arglist->start, match_arglist->length);
		*(arglist+match_arglist->length) = '\x0';

		initTagEntry (&e, name);
		e.kind = kind;
		e.kindName = kindName;
		e.extensionFields.arglist = arglist;
		makeTagEntry (&e);
		// printTagEntry(&e);

		eFree(name);
		eFree(arglist);
	}
}

static void handlePhpLine(vString *line)
{
	char *class, head[16];
	int i, ct = 0;
	int decl[3] = {6, 15, 12};
	
	strncpy(head, vStringValue(line), 15);
	for (i = 0; i < 15; i++)
	{
		head[i] = tolower(head[i]);
	}
	
	if (strncmp(head, "class ", (size_t) decl[0]) == 0 && (ct = 1) ||
		strncmp(head, "abstract class ", (size_t) decl[1]) == 0 && (ct = 2) ||
		strncmp(head, "final class ", (size_t) decl[2]) == 0 && (ct = 3))
	{
		for (i = decl[ct - 1]; i < vStringSize(line); i++)
		{
			if (isspace(vStringChar(line, i)))
			{
				break;
			}
			fputc(vStringChar(line, i), stdout); 
		}
		fputc('\n', stdout);
	}	
	else
	{
		//fputs( "NONE CLASS: ", stdout);
	}
}

static void findPHPTags (void)
{
    vString *name = vStringNew ();
    vString *line = vStringNew ();
    
    int c = '\0';

    while ((c = cppGetc ()) != EOF)
    {
		vStringPut(line, c);
		
		if (c == NEWLINE)
		{
			handlePhpLine(line);
			vStringClear(line);
		}

		/*
        if (strncmp ((const char*) line, "class ", (size_t) 6) == 0  &&
            isspace ((int) line [6]))
        {
            const unsigned char *cp = line + 6;
            while (isspace ((int) *cp))
                ++cp;
            while (isalnum ((int) *cp)  ||  *cp == '_')
            {
                vStringPut (name, (int) *cp);
                ++cp;
            }
            printf("NAME: %s\n", name);
            /*
            vStringTerminate (name);
            makeSimpleTag (name, SwineKinds, K_DEFINE);
            vStringClear (name);
        }
		*/
    }
    vStringDelete (name);
}

/* Create parser definition structure */
extern parserDefinition* PhpParser (void)
{
	static const char *const extensions [] = { "php", "php3", "phtml", NULL };
	parserDefinition* def = parserNew ("PHP");
	
    def->kinds      = PhpKinds;
    def->kindCount  = KIND_COUNT (PhpKinds);
    def->extensions = extensions;
    def->parser     = findPHPTags;
    
    return def;
}

/*  Skips to the next brace in column 1. This is intended for cases where
 *  preprocessor constructs result in unbalanced braces.
 */
static void skipToFormattedBraceMatch (void)
{
	int c, next;

	c = cppGetc ();
	next = cppGetc ();
	while (c != EOF  &&  (c != '\n'  ||  next != '}'))
	{
		c = next;
		next = cppGetc ();
	}
}

/*  Skip to the matching character indicated by the pair string. If skipping
 *  to a matching brace and any brace is found within a different level of a
 *  #if conditional statement while brace formatting is in effect, we skip to
 *  the brace matched by its formatting. It is assumed that we have already
 *  read the character which starts the group (i.e. the first character of
 *  "pair").
 */
static void skipToMatch (const char *const pair)
{
	const boolean braceMatching = (boolean) (strcmp ("{}", pair) == 0);
	const boolean braceFormatting = (boolean) (isBraceFormat () && braceMatching);
	const unsigned int initialLevel = getDirectiveNestLevel ();
	const int begin = pair [0], end = pair [1];
	const unsigned long inputLineNumber = getInputLineNumber ();
	int matchLevel = 1;
	int c = '\0';
	while (matchLevel > 0  &&  (c = cppGetc ()) != EOF)
	{
		fputc(c, stdout);
		if (c == begin)
		{
			++matchLevel;
			if (braceFormatting  &&  getDirectiveNestLevel () != initialLevel)
			{
				skipToFormattedBraceMatch ();
				break;
			}
		}
		else if (c == end)
		{
			--matchLevel;
			if (braceFormatting  &&  getDirectiveNestLevel () != initialLevel)
			{
				skipToFormattedBraceMatch ();
				break;
			}
		}
	}
	fputc('\n', stdout);
	
	printf("---- Line number: %d\n", inputLineNumber);
	if (c == EOF)
	{
		printf("\test\n");
	}
}

/* vi:set tabstop=4 shiftwidth=4: */