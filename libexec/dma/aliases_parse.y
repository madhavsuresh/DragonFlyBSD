%{
/* $DragonFly: src/libexec/dma/aliases_parse.y,v 1.1 2008/02/02 18:20:51 matthias Exp $ */

#include <err.h>
#include <string.h>
#include "dma.h"

extern int yylineno;
void yyerror(const char *);
int yywrap(void);

void
yyerror(const char *msg)
{
	warnx("aliases line %d: %s", yylineno, msg);
}

int
yywrap(void)
{
	return (1);
}

%}

%union {
	char *ident;
	struct stritem *strit;
	struct alias *alias;
}

%token <ident> T_IDENT
%token T_ERROR
%token T_EOF 0

%type <strit> dests
%type <alias> alias aliases

%%

start	: aliases T_EOF
		{
			LIST_FIRST(&aliases) = $1;
		}

aliases	: /* EMPTY */
		{
			$$ = NULL;
		}
	| alias aliases
		{
			if ($2 != NULL && $1 != NULL)
				LIST_INSERT_AFTER($2, $1, next);
			else if ($2 == NULL)
				$2 = $1;
			$$ = $2;
		}
       	;

alias	: T_IDENT ':' dests '\n'
		{
			struct alias *al;

			if ($1 == NULL)
				YYABORT;
			al = calloc(1, sizeof(*al));
			if (al == NULL)
				YYABORT;
			al->alias = $1;
			SLIST_FIRST(&al->dests) = $3;
			$$ = al;
		}
	| error '\n'
		{
			yyerrok;
			$$ = NULL;
		}
     	;

dests	: T_IDENT
		{
			struct stritem *it;

			if ($1 == NULL)
				YYABORT;
			it = calloc(1, sizeof(*it));
			if (it == NULL)
				YYABORT;
			it->str = $1;
			$$ = it;
		}
	| T_IDENT ',' dests
		{
			struct stritem *it;

			if ($1 == NULL)
				YYABORT;
			it = calloc(1, sizeof(*it));
			if (it == NULL)
				YYABORT;
			it->str = $1;
			SLIST_NEXT(it, next) = $3;
			$$ = it;
		}
	;

%%
