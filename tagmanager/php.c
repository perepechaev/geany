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
	K_CLASS, K_METHOD, K_DEFINE, K_FUNCTION, K_VARIABLE
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
	{ TRUE, 'm', "method",   "methods" },
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

void printTagEntry(const tagEntryInfo *tag)
{
	fprintf(stderr, "Tag: %s (%s) [ impl: %s, scope: %s, type: %s\n", tag->name,
	tag->kindName, tag->extensionFields.implementation, tag->extensionFields.scope[1],
	tag->extensionFields.varType);
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
		tag.type = tm_tag_name_type("class"); 
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
	char *access[] = {"public", "private", "protected"};
	
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
			
			initTagEntry(&tag, st->scopeName);
			tag.kind = 'm';
			tag.kindName = "method";
			tag.name = name;
			tag.extensionFields.access = access[am];
			tag.extensionFields.scope [0] = "class";
			tag.extensionFields.scope [1] = st->scopeName;
			tag.type = tm_tag_name_type("method"); 
			makeTagEntry (&tag);
			
			
			//printTagEntry(&tag);
			//printf("%d %s::%s()\n", tag.type, st->scopeName, name);
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
		
		// BUG: class { - nested will wrong
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

/* vi:set tabstop=4 shiftwidth=4: */
