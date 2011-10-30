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

typedef enum{
	AM_NULL, AM_PUBLIC, AM_PRIVATE, AM_PROTECTD
} phpAccessMethod;

typedef enum{
	TM_NULL, TM_FINAL, TM_ABSTRACT, TM_STATIC
} phpTypeMethod;

typedef enum{
	SC_ROOT, SC_CLASS, SC_FUNCTION
} phpScope;

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

typedef struct sStatement
{
	char*	scopeName;
	int		nested;
	phpScope type;
	struct sStatement *parent;
} sStatement;

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

static void handlePhpLine(vString *line, sStatement **statement)
{
	unsigned char head[16];
	int i, s, ct = 0;
	int decl[3] = {6, 15, 12};
	sStatement *st = *statement;
	tagEntryInfo tag;
	
	strncpy(head, vStringValue(line), 15);
	for (i = 0; i < 15; i++)
	{
		head[i] = tolower(head[i]);
	}
	
	// Parse class
	if (strncmp(head, "class ", (size_t) decl[0]) == 0 && (ct = 1) ||
		strncmp(head, "abstract class ", (size_t) decl[1]) == 0 && (ct = 2) ||
		strncmp(head, "final class ", (size_t) decl[2]) == 0 && (ct = 3))
	{
		s = decl[ct - 1];
		
		// find class name
		for (i = s; i < vStringSize(line); i++)
		{
			char ch = vStringChar(line, i);

			if (isspace((int) ch) || !(isalnum((int) ch) || (int) ch == '_'))
			{
				break;
			}
		}
		
		// Store statement
		sStatement *parent = st;
		st = xMalloc(1, sStatement);
		if ( i - s == 0 )
		{
			return;
		}
		st->scopeName = xMalloc(i - s, char);
		strncpy(st->scopeName, vStringValue(line) + s, i - s);
		*(st->scopeName + i - s) = '\0';
		st->parent = parent;
		st->type = SC_CLASS;
		st->nested = 0;
		*statement = st;
		
		// Create tag
		initTagEntry(&tag, st->scopeName);
		tag.kind = 'c';
		tag.kindName = "class";
		makeTagEntry (&tag);
		
		// DEBUG
		
		// printf("class %s (parent:%x)\n", st->scopeName, st->parent);
		// printTagEntry(&tag);
		// printf("Line number: %d\n", tag.lineNumber);
		
		return;
	}	
	
	unsigned char* cp = vStringValue(line);
	while ( isspace( (int) *cp ) )
		cp++;
		
	// Parse methods
	phpAccessMethod am = AM_NULL;
	phpTypeMethod tm = TM_NULL;
	int isStatic = 0;
	
	
	char *keywords[] = {"public", "private", "protected", "abstact", "static", "final"};
	
	for (i = 0; i < 6; i++)
	{
		if (strncmp(cp, keywords[i], strlen(keywords[i])) == 0)
		{
			switch (i)
			{
				case 0: am = AM_PUBLIC; break;
				case 1: am = AM_PRIVATE; break;
				case 2: am = AM_PROTECTD; break;
				case 3: tm = TM_ABSTRACT; break;
				case 4: isStatic = 1; break;
				case 5: tm = TM_FINAL; break;
			}
			
			cp += strlen(keywords[i]);
			while ( isspace( (int) *cp ) )
				cp++;
			i = -1;
		}
	}
	
	char *name = 0;
	if ( strncmp( cp, "function ", 9 ) == 0 )
	{
		
		cp += 9;
		i = 0;
		
		while ( isspace( (int) *cp ) )
			cp++;
			
		while ( (int) *cp != '\0' )
		{
			if (isspace((int) *cp) || !(isalnum((int) *cp) || (int) *cp == '_'))
			{
				break;
			}
			cp++; i++;
		}
		
		
		if ( i != 0)
		{
			name = xMalloc(i+1, char);
			strncpy(name, cp - i, i);
			name[i] = '\0';
		}
	}
	
	if (name)
	{
		eFree(name);
	}
	
	if ( !st->scopeName )
	{
		return;
	}
}

static void findPHPTags (void)
{
    vString *line = vStringNew ();
	sStatement *st = xMalloc(1, sStatement);

	st->scopeName = '\0';
	st->nested = 0;
    
    int c = '\0';
    int ln = 0;

    while ((c = cppGetc ()) != EOF)
    {
		vStringPut(line, c);
		
		if (c == '{')
		{
			st->nested++;
		}
		else if (c == '}')
		{
			st->nested--;
			if (st->nested == 0)
			{
				sStatement *stp = st;
				
				if (st->scopeName)
				{
					eFree(st->scopeName);
				}
				
				if (st->type == SC_CLASS || st->type == SC_FUNCTION)
				{
					st = st->parent;
					eFree(stp);
				}
			}
		}
		else if (c == NEWLINE)
		{
			ln++;
			handlePhpLine(line, &st);
			vStringClear(line);
		}
    }
    
	eFree(st);
    vStringDelete (line);
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
