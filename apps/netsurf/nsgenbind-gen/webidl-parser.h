/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_WEBIDL_WEBIDL_PARSER_H_INCLUDED
# define YY_WEBIDL_WEBIDL_PARSER_H_INCLUDED
/* Debug traces.  */
#ifndef WEBIDL_DEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define WEBIDL_DEBUG 1
#  else
#   define WEBIDL_DEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define WEBIDL_DEBUG 1
# endif /* ! defined YYDEBUG */
#endif  /* ! defined WEBIDL_DEBUG */
#if WEBIDL_DEBUG
extern int webidl_debug;
#endif

/* Token kinds.  */
#ifndef WEBIDL_TOKENTYPE
# define WEBIDL_TOKENTYPE
  enum webidl_tokentype
  {
    WEBIDL_EMPTY = -2,
    WEBIDL_EOF = 0,                /* "end of file"  */
    WEBIDL_error = 256,            /* error  */
    WEBIDL_UNDEF = 257,            /* "invalid token"  */
    TOK_ANY = 258,                 /* TOK_ANY  */
    TOK_ATTRIBUTE = 259,           /* TOK_ATTRIBUTE  */
    TOK_BOOLEAN = 260,             /* TOK_BOOLEAN  */
    TOK_BYTE = 261,                /* TOK_BYTE  */
    TOK_CALLBACK = 262,            /* TOK_CALLBACK  */
    TOK_CONST = 263,               /* TOK_CONST  */
    TOK_CONSTRUCTOR = 264,         /* TOK_CONSTRUCTOR  */
    TOK_CREATOR = 265,             /* TOK_CREATOR  */
    TOK_DATE = 266,                /* TOK_DATE  */
    TOK_DELETER = 267,             /* TOK_DELETER  */
    TOK_DICTIONARY = 268,          /* TOK_DICTIONARY  */
    TOK_DOUBLE = 269,              /* TOK_DOUBLE  */
    TOK_ELLIPSIS = 270,            /* TOK_ELLIPSIS  */
    TOK_ENUM = 271,                /* TOK_ENUM  */
    TOK_EOL = 272,                 /* TOK_EOL  */
    TOK_EXCEPTION = 273,           /* TOK_EXCEPTION  */
    TOK_FALSE = 274,               /* TOK_FALSE  */
    TOK_FLOAT = 275,               /* TOK_FLOAT  */
    TOK_GETRAISES = 276,           /* TOK_GETRAISES  */
    TOK_GETTER = 277,              /* TOK_GETTER  */
    TOK_IMPLEMENTS = 278,          /* TOK_IMPLEMENTS  */
    TOK_IN = 279,                  /* TOK_IN  */
    TOK_INFINITY = 280,            /* TOK_INFINITY  */
    TOK_INHERIT = 281,             /* TOK_INHERIT  */
    TOK_INTERFACE = 282,           /* TOK_INTERFACE  */
    TOK_ITERABLE = 283,            /* TOK_ITERABLE  */
    TOK_LEGACYCALLER = 284,        /* TOK_LEGACYCALLER  */
    TOK_LEGACYITERABLE = 285,      /* TOK_LEGACYITERABLE  */
    TOK_LONG = 286,                /* TOK_LONG  */
    TOK_MODULE = 287,              /* TOK_MODULE  */
    TOK_NAN = 288,                 /* TOK_NAN  */
    TOK_NATIVE = 289,              /* TOK_NATIVE  */
    TOK_NAMEDCONSTRUCTOR = 290,    /* TOK_NAMEDCONSTRUCTOR  */
    TOK_NULL_LITERAL = 291,        /* TOK_NULL_LITERAL  */
    TOK_OBJECT = 292,              /* TOK_OBJECT  */
    TOK_OCTET = 293,               /* TOK_OCTET  */
    TOK_OMITTABLE = 294,           /* TOK_OMITTABLE  */
    TOK_OPTIONAL = 295,            /* TOK_OPTIONAL  */
    TOK_OR = 296,                  /* TOK_OR  */
    TOK_PARTIAL = 297,             /* TOK_PARTIAL  */
    TOK_PROMISE = 298,             /* TOK_PROMISE  */
    TOK_RAISES = 299,              /* TOK_RAISES  */
    TOK_READONLY = 300,            /* TOK_READONLY  */
    TOK_REQUIRED = 301,            /* TOK_REQUIRED  */
    TOK_SETRAISES = 302,           /* TOK_SETRAISES  */
    TOK_SETTER = 303,              /* TOK_SETTER  */
    TOK_SEQUENCE = 304,            /* TOK_SEQUENCE  */
    TOK_SHORT = 305,               /* TOK_SHORT  */
    TOK_STATIC = 306,              /* TOK_STATIC  */
    TOK_STRING = 307,              /* TOK_STRING  */
    TOK_STRINGIFIER = 308,         /* TOK_STRINGIFIER  */
    TOK_TRUE = 309,                /* TOK_TRUE  */
    TOK_TYPEDEF = 310,             /* TOK_TYPEDEF  */
    TOK_UNRESTRICTED = 311,        /* TOK_UNRESTRICTED  */
    TOK_UNSIGNED = 312,            /* TOK_UNSIGNED  */
    TOK_VOID = 313,                /* TOK_VOID  */
    TOK_POUND_SIGN = 314,          /* TOK_POUND_SIGN  */
    TOK_IDENTIFIER = 315,          /* TOK_IDENTIFIER  */
    TOK_INT_LITERAL = 316,         /* TOK_INT_LITERAL  */
    TOK_FLOAT_LITERAL = 317,       /* TOK_FLOAT_LITERAL  */
    TOK_STRING_LITERAL = 318,      /* TOK_STRING_LITERAL  */
    TOK_OTHER_LITERAL = 319,       /* TOK_OTHER_LITERAL  */
    TOK_JAVADOC = 320              /* TOK_JAVADOC  */
  };
  typedef enum webidl_tokentype webidl_token_kind_t;
#endif

/* Value type.  */
#if ! defined WEBIDL_STYPE && ! defined WEBIDL_STYPE_IS_DECLARED
union WEBIDL_STYPE
{
#line 56 "webidl-parser.y"

    int attr;
    long value;
    bool isit;
    char* text;
    struct webidl_node *node;

#line 145 "webidl-parser.h"

};
typedef union WEBIDL_STYPE WEBIDL_STYPE;
# define WEBIDL_STYPE_IS_TRIVIAL 1
# define WEBIDL_STYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined WEBIDL_LTYPE && ! defined WEBIDL_LTYPE_IS_DECLARED
typedef struct WEBIDL_LTYPE WEBIDL_LTYPE;
struct WEBIDL_LTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define WEBIDL_LTYPE_IS_DECLARED 1
# define WEBIDL_LTYPE_IS_TRIVIAL 1
#endif




int webidl_parse (struct webidl_node **webidl_ast);


#endif /* !YY_WEBIDL_WEBIDL_PARSER_H_INCLUDED  */
