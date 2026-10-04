
N			[0-9]
L			[a-zA-Z_]
H			[a-fA-F0-9]
E			[Ee][+-]?{N}+
FS			(f|F)
%e 10000
%n 4000
%p 5000

%top {
	#include "linden_common.h"
}

%{
// Deal with the fact that lex/yacc generates unreachable code
#ifdef LL_WINDOWS
#pragma warning (disable : 4018) // warning C4018: signed/unsigned mismatch
#pragma warning (disable : 4702) // warning C4702: unreachable code
#endif	//	LL_WINDOWS
#include "llmath.h"
#include "lscript_tree.h"
#include "lscript_typecheck.h"
#include "lscript_resource.h"
#include "indra.y.hpp"
#include "lltimer.h"
#include "lluuid.h"

void count();
void line_comment();
void block_comment();
void parse_string();

#define YYLMAX 16384
#define YY_NEVER_INTERACTIVE 1 /* stops flex from calling isatty() */
#ifdef LL_WINDOWS
#define isatty(x) 0 /* hack for bug in cygwin flex 2.5.35 */
#endif

#ifdef ECHO
#undef ECHO
#endif

#define ECHO do { } while (0)

#define yyparse indra_parse
#define yyerror indra_error
#define yylval indra_lval
#define yy_create_buffer indra__create_buffer
#define yy_delete_buffer indra__delete_buffer
#define yy_flex_debug indra__flex_debug
#define yy_init_buffer indra__init_buffer
#define yy_flush_buffer indra__flush_buffer
#define yy_load_buffer_state indra__load_buffer_state
#define yy_switch_to_buffer indra__switch_to_buffer
#define yyin indra_in
#define yyleng indra_leng
#define yylex indra_lex
#define yylineno indra_lineno
#define yyout indra_out
#define yyrestart indra_restart
#define yytext indra_text
#define yywrap indra_wrap
#define yyalloc indra_alloc
#define yyrealloc indra_realloc
#define yyfree indra_free


int yyparse( void );
int yylex( void );
int yyerror(const char *fmt, ...);

%}

%%
"//"					{ gInternalLine++; gInternalColumn = 0; line_comment(); }
"/*"					{ block_comment(); }

"integer"			{ count(); return(INTEGER); }
"float"				{ count(); return(FLOAT_TYPE); }
"string"			{ count(); return(STRING); }
"key"				{ count(); return(LLKEY); }
"vector"			{ count(); return(VECTOR); }
"quaternion"		{ count(); return(QUATERNION); }
"rotation"			{ count(); return(QUATERNION); }
"list"				{ count(); return(LIST); }

"default"			{ count(); yylval.sval = new char[strlen(yytext) + 1]; strcpy(yylval.sval, yytext); return(STATE_DEFAULT); }
"state"				{ count(); return(STATE); }
"event"				{ count(); return(EVENT); }
"jump"				{ count(); return(JUMP); }
"return"			{ count(); return(RETURN); }
"if"				{ count(); return(IF); }
"else"				{ count(); return(ELSE); }
"for"				{ count(); return(FOR); }
"do"				{ count(); return(DO); }
"while"				{ count(); return(WHILE); }

/* GENERATED LEXER EVENTS */
"listen"				{ count(); return(CHAT); }
"print"					{ count(); return(PRINT); }
"changed"				{ count(); return(INVENTORY); }
"on_rez"				{ count(); return(REZ); }
"linkset_data"			{ count(); return(LINKSET_DATA); }
"."						{ count(); return(PERIOD); }


0[xX]{H}+			{ count(); yylval.ival = strtoul(yytext, NULL, 0);  return(INTEGER_CONSTANT); }
{N}+				{ count(); yylval.ival = strtoul(yytext, NULL, 10); return(INTEGER_CONSTANT); }
"TRUE"				{ count(); yylval.ival = 1; return(INTEGER_TRUE); }
"FALSE"				{ count(); yylval.ival = 0; return(INTEGER_FALSE); }
/* GENERATED LEXER CONSTANTS */

{L}({L}|{N})*		{ count(); yylval.sval = new char[strlen(yytext) + 1]; strcpy(yylval.sval, yytext); return(IDENTIFIER); }

{N}+{E}					{ count(); yylval.fval = (F32)atof(yytext); return(FP_CONSTANT); }
{N}*"."{N}+({E})?{FS}?	{ count(); yylval.fval = (F32)atof(yytext); return(FP_CONSTANT); }
{N}+"."{N}*({E})?{FS}?	{ count(); yylval.fval = (F32)atof(yytext); return(FP_CONSTANT); }

L?\"(\\.|[^\\"])*\"	{ parse_string(); count(); return(STRING_CONSTANT); }

"++"				{ count(); return(INC_OP); }
"--"				{ count(); return(DEC_OP); }
"+="				{ count(); return(ADD_ASSIGN); }
"-="				{ count(); return(SUB_ASSIGN); }
"*="				{ count(); return(MUL_ASSIGN); }
"/="				{ count(); return(DIV_ASSIGN); }
"%="				{ count(); return(MOD_ASSIGN); }
";"					{ count(); return(';'); }
"{"					{ count(); return('{'); }
"}"					{ count(); return('}'); }
","					{ count(); return(','); }
"="					{ count(); return('='); }
"("					{ count(); return('('); }
")"					{ count(); return(')'); }
"-"					{ count(); return('-'); }
"+"					{ count(); return('+'); }
"*"					{ count(); return('*'); }
"/"					{ count(); return('/'); }
"%"					{ count(); return('%'); }
"@"					{ count(); return('@'); }
":"					{ count(); return(':'); }
">"					{ count(); return('>'); }
"<"					{ count(); return('<'); }
"]"					{ count(); return(']'); }
"["					{ count(); return('['); }
"=="				{ count(); return(EQ);  }
"!="				{ count(); return(NEQ);  }
">="				{ count(); return(GEQ);  }
"<="				{ count(); return(LEQ);  }
"&"					{ count(); return('&');  }
"|"					{ count(); return('|');  }
"^"					{ count(); return('^');  }
"~"					{ count(); return('~');  }
"!"					{ count(); return('!');  }
"&&"				{ count(); return(BOOLEAN_AND);	}
"||"				{ count(); return(BOOLEAN_OR);	}
"<<"				{ count(); return(SHIFT_LEFT);  }
">>"				{ count(); return(SHIFT_RIGHT); }

[ \t\v\n\f]			{ count(); }
.					{ /* ignore bad characters */ }

%%

LLScriptAllocationManager	*gAllocationManager;
LLScriptScript				*gScriptp;

// Prototype for the yacc parser entry point
int yyparse(void);

int yyerror(const char *fmt, ...)
{
	gErrorToText.writeError(yyout, gLine, gColumn, LSERROR_SYNTAX_ERROR);
	return 0;
}

//#define EMERGENCY_DEBUG_PRINTOUTS
//#define EMIT_CIL_ASSEMBLER

BOOL lscript_compile(const char* src_filename, const char* dst_filename,
					 const char* err_filename, BOOL compile_to_mono, const char* class_name, BOOL is_god_like)
{
	BOOL			b_parse_ok = FALSE;
	BOOL			b_dummy = FALSE;
	U64				b_dummy_count = FALSE;
	LSCRIPTType		type = LST_NULL;

	gInternalColumn = 0;
	gInternalLine = 0;
	gScriptp = NULL;

	gErrorToText.init();
	init_supported_expressions();
	init_temp_jumps();
	gAllocationManager = new LLScriptAllocationManager();

	yyin = LLFile::fopen(std::string(src_filename), "r");
	if (yyin)
	{
		yyout = LLFile::fopen(std::string(err_filename), "w");

		// Reset the lexer's internal buffering.

	    yyrestart(yyin);

		b_parse_ok = !yyparse();

		if (b_parse_ok)
		{
#ifdef EMERGENCY_DEBUG_PRINTOUTS
			char compiled[256];
			sprintf(compiled, "%s.o", src_filename);
			LLFILE* compfile;
			compfile = LLFile::fopen(compiled, "w");
#endif

			if(dst_filename)
			{
				gScriptp->setBytecodeDest(dst_filename);
			}

			gScriptp->mGodLike = is_god_like;
			
			gScriptp->setClassName(class_name);

			gScopeStringTable = new LLStringTable(16384);
#ifdef EMERGENCY_DEBUG_PRINTOUTS
			gScriptp->recurse(compfile, 0, 4, LSCP_PRETTY_PRINT, LSPRUNE_INVALID, b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
#endif
			gScriptp->recurse(yyout, 0, 0, LSCP_PRUNE,		 LSPRUNE_INVALID, b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
			gScriptp->recurse(yyout, 0, 0, LSCP_SCOPE_PASS1, LSPRUNE_INVALID, b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
			gScriptp->recurse(yyout, 0, 0, LSCP_SCOPE_PASS2, LSPRUNE_INVALID, b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
			gScriptp->recurse(yyout, 0, 0, LSCP_TYPE,		 LSPRUNE_INVALID, b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
			if (!gErrorToText.getErrors())
			{
				gScriptp->recurse(yyout, 0, 0, LSCP_RESOURCE, LSPRUNE_INVALID,		 b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
#ifdef EMERGENCY_DEBUG_PRINTOUTS
				gScriptp->recurse(yyout, 0, 0, LSCP_EMIT_ASSEMBLY, LSPRUNE_INVALID,  b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
#endif
				if(TRUE == compile_to_mono)
				{
					gScriptp->recurse(yyout, 0, 0, LSCP_EMIT_CIL_ASSEMBLY, LSPRUNE_INVALID,  b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
				}
				else
				{
					gScriptp->recurse(yyout, 0, 0, LSCP_EMIT_BYTE_CODE, LSPRUNE_INVALID, b_dummy, NULL, type, type, b_dummy_count, NULL, NULL, 0, NULL, 0, NULL);
				}
			}
			delete gScopeStringTable;
			gScopeStringTable = NULL;
#ifdef EMERGENCY_DEBUG_PRINTOUTS
			fclose(compfile);
#endif
		}
		fclose(yyout);
		fclose(yyin);
	}

	delete gAllocationManager;
	delete gScopeStringTable;
	
	return b_parse_ok && !gErrorToText.getErrors();
}


BOOL lscript_compile(char *filename, BOOL compile_to_mono, BOOL is_god_like = FALSE)
{
	char src_filename[MAX_STRING];
	sprintf(src_filename, "%s.lsl", filename);
	char err_filename[MAX_STRING];
	sprintf(err_filename, "%s.out", filename);
	char class_name[MAX_STRING];
	sprintf(class_name, "%s", filename);
	return lscript_compile(src_filename, NULL, err_filename, compile_to_mono, class_name, is_god_like);
}


S32 yywrap()
{
#if defined(FLEX_SCANNER) && !defined(LL_WINDOWS)
	// get gcc to stop complaining about lack of use of yyunput
	(void) yyunput;
#endif
	return(1);
}

void line_comment()
{
	char c;

	while ((c = yyinput()) != '\n' && c != 0 && c != EOF)
		;
}

void block_comment()
{
	char c1 = 0;
	char c2 = yyinput();
	while (c2 != 0 && c2 != EOF && !(c1 == '*' && c2 == '/')) {
		if (c2 == '\n')
		{
			gInternalLine++;
			gInternalColumn = 0;
		}
		else if (c2 == '\t')
			gInternalColumn += 4 - (gInternalColumn % 8);
		else
			gInternalColumn++;
		c1 = c2;
		c2 = yyinput();
	}
}

void count()
{
	S32 i;

	gColumn = gInternalColumn;
	gLine = gInternalLine;

	for (i = 0; yytext[i] != '\0'; i++)
		if (yytext[i] == '\n')
		{
			gInternalLine++;
			gInternalColumn = 0;
		}
		else if (yytext[i] == '\t')
			gInternalColumn += 4 - (gInternalColumn % 8);
		else
			gInternalColumn++;
}

void parse_string()
{
	S32 length = (S32)strlen(yytext);
	length = length - 2; 
	char *temp = yytext + 1;

	S32 i;
	S32 escapes = 0;
	S32 tabs = 0;
	for (i = 0; i < length; i++)
	{
		if (temp[i] == '\\')
		{
			escapes++;
			i++;
			if (temp[i] == 't')
				tabs++;
		}
	}

	S32 newlength = length - escapes + tabs*3;
	yylval.sval = new char[newlength + 1];

	char *dest = yylval.sval;

	for (i = 0; i < length; i++)
	{
		if (temp[i] == '\\')
		{
			i++;
			// linefeed
			if (temp[i] == 'n')
			{
				*dest++ = 10;
			}
			else if (temp[i] == 't')
			{
				*dest++ = ' ';
				*dest++ = ' ';
				*dest++ = ' ';
				*dest++ = ' ';
			}
			else
			{
				*dest++ = temp[i];
			}
		}
		else
		{
			*dest++ = temp[i];
		}
	}
	yylval.sval[newlength] = 0;
}
