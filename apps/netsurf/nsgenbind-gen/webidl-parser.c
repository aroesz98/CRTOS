/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Substitute the type names.  */
#define YYSTYPE         WEBIDL_STYPE
#define YYLTYPE         WEBIDL_LTYPE
/* Substitute the variable and function names.  */
#define yyparse         webidl_parse
#define yylex           webidl_lex
#define yyerror         webidl_error
#define yydebug         webidl_debug
#define yynerrs         webidl_nerrs

/* First part of user prologue.  */
#line 1 "webidl-parser.y"


/* This is a bison parser for Web IDL
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 *
 * Derived from the the grammar in apendix A of W3C WEB IDL
 *   http://www.w3.org/TR/WebIDL/
 *
 * WebIDL now has a second edition draft (mid 2015) that the dom and
 *   html specs are using. https://heycam.github.io/webidl
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define YYFPRINTF webidl_fprintf
#define YY_LOCATION_PRINT(File, Loc)                            \
  webidl_fprintf(File, "%d.%d-%d.%d",                           \
                 (Loc).first_line, (Loc).first_column,          \
                 (Loc).last_line,  (Loc).last_column)

#include "utils.h"
#include "webidl-parser.h"
#include "webidl-lexer.h"

#include "webidl-ast.h"

static char *errtxt;

static void
webidl_error(YYLTYPE *locp, struct webidl_node **winbind_ast, const char *str)
{
    UNUSED(locp);
    UNUSED(winbind_ast);
    errtxt = strdup(str);
}


#line 124 "webidl-parser.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "webidl-parser.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_TOK_ANY = 3,                    /* TOK_ANY  */
  YYSYMBOL_TOK_ATTRIBUTE = 4,              /* TOK_ATTRIBUTE  */
  YYSYMBOL_TOK_BOOLEAN = 5,                /* TOK_BOOLEAN  */
  YYSYMBOL_TOK_BYTE = 6,                   /* TOK_BYTE  */
  YYSYMBOL_TOK_CALLBACK = 7,               /* TOK_CALLBACK  */
  YYSYMBOL_TOK_CONST = 8,                  /* TOK_CONST  */
  YYSYMBOL_TOK_CONSTRUCTOR = 9,            /* TOK_CONSTRUCTOR  */
  YYSYMBOL_TOK_CREATOR = 10,               /* TOK_CREATOR  */
  YYSYMBOL_TOK_DATE = 11,                  /* TOK_DATE  */
  YYSYMBOL_TOK_DELETER = 12,               /* TOK_DELETER  */
  YYSYMBOL_TOK_DICTIONARY = 13,            /* TOK_DICTIONARY  */
  YYSYMBOL_TOK_DOUBLE = 14,                /* TOK_DOUBLE  */
  YYSYMBOL_TOK_ELLIPSIS = 15,              /* TOK_ELLIPSIS  */
  YYSYMBOL_TOK_ENUM = 16,                  /* TOK_ENUM  */
  YYSYMBOL_TOK_EOL = 17,                   /* TOK_EOL  */
  YYSYMBOL_TOK_EXCEPTION = 18,             /* TOK_EXCEPTION  */
  YYSYMBOL_TOK_FALSE = 19,                 /* TOK_FALSE  */
  YYSYMBOL_TOK_FLOAT = 20,                 /* TOK_FLOAT  */
  YYSYMBOL_TOK_GETRAISES = 21,             /* TOK_GETRAISES  */
  YYSYMBOL_TOK_GETTER = 22,                /* TOK_GETTER  */
  YYSYMBOL_TOK_IMPLEMENTS = 23,            /* TOK_IMPLEMENTS  */
  YYSYMBOL_TOK_IN = 24,                    /* TOK_IN  */
  YYSYMBOL_TOK_INFINITY = 25,              /* TOK_INFINITY  */
  YYSYMBOL_TOK_INHERIT = 26,               /* TOK_INHERIT  */
  YYSYMBOL_TOK_INTERFACE = 27,             /* TOK_INTERFACE  */
  YYSYMBOL_TOK_ITERABLE = 28,              /* TOK_ITERABLE  */
  YYSYMBOL_TOK_LEGACYCALLER = 29,          /* TOK_LEGACYCALLER  */
  YYSYMBOL_TOK_LEGACYITERABLE = 30,        /* TOK_LEGACYITERABLE  */
  YYSYMBOL_TOK_LONG = 31,                  /* TOK_LONG  */
  YYSYMBOL_TOK_MODULE = 32,                /* TOK_MODULE  */
  YYSYMBOL_TOK_NAN = 33,                   /* TOK_NAN  */
  YYSYMBOL_TOK_NATIVE = 34,                /* TOK_NATIVE  */
  YYSYMBOL_TOK_NAMEDCONSTRUCTOR = 35,      /* TOK_NAMEDCONSTRUCTOR  */
  YYSYMBOL_TOK_NULL_LITERAL = 36,          /* TOK_NULL_LITERAL  */
  YYSYMBOL_TOK_OBJECT = 37,                /* TOK_OBJECT  */
  YYSYMBOL_TOK_OCTET = 38,                 /* TOK_OCTET  */
  YYSYMBOL_TOK_OMITTABLE = 39,             /* TOK_OMITTABLE  */
  YYSYMBOL_TOK_OPTIONAL = 40,              /* TOK_OPTIONAL  */
  YYSYMBOL_TOK_OR = 41,                    /* TOK_OR  */
  YYSYMBOL_TOK_PARTIAL = 42,               /* TOK_PARTIAL  */
  YYSYMBOL_TOK_PROMISE = 43,               /* TOK_PROMISE  */
  YYSYMBOL_TOK_RAISES = 44,                /* TOK_RAISES  */
  YYSYMBOL_TOK_READONLY = 45,              /* TOK_READONLY  */
  YYSYMBOL_TOK_REQUIRED = 46,              /* TOK_REQUIRED  */
  YYSYMBOL_TOK_SETRAISES = 47,             /* TOK_SETRAISES  */
  YYSYMBOL_TOK_SETTER = 48,                /* TOK_SETTER  */
  YYSYMBOL_TOK_SEQUENCE = 49,              /* TOK_SEQUENCE  */
  YYSYMBOL_TOK_SHORT = 50,                 /* TOK_SHORT  */
  YYSYMBOL_TOK_STATIC = 51,                /* TOK_STATIC  */
  YYSYMBOL_TOK_STRING = 52,                /* TOK_STRING  */
  YYSYMBOL_TOK_STRINGIFIER = 53,           /* TOK_STRINGIFIER  */
  YYSYMBOL_TOK_TRUE = 54,                  /* TOK_TRUE  */
  YYSYMBOL_TOK_TYPEDEF = 55,               /* TOK_TYPEDEF  */
  YYSYMBOL_TOK_UNRESTRICTED = 56,          /* TOK_UNRESTRICTED  */
  YYSYMBOL_TOK_UNSIGNED = 57,              /* TOK_UNSIGNED  */
  YYSYMBOL_TOK_VOID = 58,                  /* TOK_VOID  */
  YYSYMBOL_TOK_POUND_SIGN = 59,            /* TOK_POUND_SIGN  */
  YYSYMBOL_TOK_IDENTIFIER = 60,            /* TOK_IDENTIFIER  */
  YYSYMBOL_TOK_INT_LITERAL = 61,           /* TOK_INT_LITERAL  */
  YYSYMBOL_TOK_FLOAT_LITERAL = 62,         /* TOK_FLOAT_LITERAL  */
  YYSYMBOL_TOK_STRING_LITERAL = 63,        /* TOK_STRING_LITERAL  */
  YYSYMBOL_TOK_OTHER_LITERAL = 64,         /* TOK_OTHER_LITERAL  */
  YYSYMBOL_TOK_JAVADOC = 65,               /* TOK_JAVADOC  */
  YYSYMBOL_66_ = 66,                       /* '{'  */
  YYSYMBOL_67_ = 67,                       /* '}'  */
  YYSYMBOL_68_ = 68,                       /* ';'  */
  YYSYMBOL_69_ = 69,                       /* '='  */
  YYSYMBOL_70_ = 70,                       /* ':'  */
  YYSYMBOL_71_ = 71,                       /* ','  */
  YYSYMBOL_72_ = 72,                       /* '('  */
  YYSYMBOL_73_ = 73,                       /* ')'  */
  YYSYMBOL_74_ = 74,                       /* '-'  */
  YYSYMBOL_75_ = 75,                       /* '<'  */
  YYSYMBOL_76_ = 76,                       /* '>'  */
  YYSYMBOL_77_ = 77,                       /* '['  */
  YYSYMBOL_78_ = 78,                       /* ']'  */
  YYSYMBOL_79_ = 79,                       /* '.'  */
  YYSYMBOL_80_ = 80,                       /* '?'  */
  YYSYMBOL_YYACCEPT = 81,                  /* $accept  */
  YYSYMBOL_Definitions = 82,               /* Definitions  */
  YYSYMBOL_Definition = 83,                /* Definition  */
  YYSYMBOL_CallbackOrInterface = 84,       /* CallbackOrInterface  */
  YYSYMBOL_CallbackRestOrInterface = 85,   /* CallbackRestOrInterface  */
  YYSYMBOL_Interface = 86,                 /* Interface  */
  YYSYMBOL_Partial = 87,                   /* Partial  */
  YYSYMBOL_PartialDefinition = 88,         /* PartialDefinition  */
  YYSYMBOL_PartialInterface = 89,          /* PartialInterface  */
  YYSYMBOL_InterfaceMembers = 90,          /* InterfaceMembers  */
  YYSYMBOL_InterfaceMember = 91,           /* InterfaceMember  */
  YYSYMBOL_Dictionary = 92,                /* Dictionary  */
  YYSYMBOL_DictionaryMembers = 93,         /* DictionaryMembers  */
  YYSYMBOL_DictionaryMember = 94,          /* DictionaryMember  */
  YYSYMBOL_PartialDictionary = 95,         /* PartialDictionary  */
  YYSYMBOL_Default = 96,                   /* Default  */
  YYSYMBOL_DefaultValue = 97,              /* DefaultValue  */
  YYSYMBOL_Exception = 98,                 /* Exception  */
  YYSYMBOL_ExceptionMembers = 99,          /* ExceptionMembers  */
  YYSYMBOL_Inheritance = 100,              /* Inheritance  */
  YYSYMBOL_Enum = 101,                     /* Enum  */
  YYSYMBOL_EnumValueList = 102,            /* EnumValueList  */
  YYSYMBOL_EnumValueListComma = 103,       /* EnumValueListComma  */
  YYSYMBOL_EnumValueListString = 104,      /* EnumValueListString  */
  YYSYMBOL_CallbackRest = 105,             /* CallbackRest  */
  YYSYMBOL_Typedef = 106,                  /* Typedef  */
  YYSYMBOL_ImplementsStatement = 107,      /* ImplementsStatement  */
  YYSYMBOL_Const = 108,                    /* Const  */
  YYSYMBOL_ConstValue = 109,               /* ConstValue  */
  YYSYMBOL_BooleanLiteral = 110,           /* BooleanLiteral  */
  YYSYMBOL_FloatLiteral = 111,             /* FloatLiteral  */
  YYSYMBOL_AttributeOrOperation = 112,     /* AttributeOrOperation  */
  YYSYMBOL_StringifierAttributeOrOperation = 113, /* StringifierAttributeOrOperation  */
  YYSYMBOL_Attribute = 114,                /* Attribute  */
  YYSYMBOL_StaticMember = 115,             /* StaticMember  */
  YYSYMBOL_StaticMemberRest = 116,         /* StaticMemberRest  */
  YYSYMBOL_AttributeRest = 117,            /* AttributeRest  */
  YYSYMBOL_AttributeName = 118,            /* AttributeName  */
  YYSYMBOL_AttributeNameKeyword = 119,     /* AttributeNameKeyword  */
  YYSYMBOL_Inherit = 120,                  /* Inherit  */
  YYSYMBOL_ReadOnly = 121,                 /* ReadOnly  */
  YYSYMBOL_Operation = 122,                /* Operation  */
  YYSYMBOL_SpecialOperation = 123,         /* SpecialOperation  */
  YYSYMBOL_Specials = 124,                 /* Specials  */
  YYSYMBOL_Special = 125,                  /* Special  */
  YYSYMBOL_OperationRest = 126,            /* OperationRest  */
  YYSYMBOL_OptionalIdentifier = 127,       /* OptionalIdentifier  */
  YYSYMBOL_ArgumentList = 128,             /* ArgumentList  */
  YYSYMBOL_Arguments = 129,                /* Arguments  */
  YYSYMBOL_Argument = 130,                 /* Argument  */
  YYSYMBOL_OptionalOrRequiredArgument = 131, /* OptionalOrRequiredArgument  */
  YYSYMBOL_ArgumentName = 132,             /* ArgumentName  */
  YYSYMBOL_Ellipsis = 133,                 /* Ellipsis  */
  YYSYMBOL_Iterable = 134,                 /* Iterable  */
  YYSYMBOL_OptionalType = 135,             /* OptionalType  */
  YYSYMBOL_ExceptionMember = 136,          /* ExceptionMember  */
  YYSYMBOL_ExceptionField = 137,           /* ExceptionField  */
  YYSYMBOL_ExtendedAttributeList = 138,    /* ExtendedAttributeList  */
  YYSYMBOL_ExtendedAttributes = 139,       /* ExtendedAttributes  */
  YYSYMBOL_ExtendedAttribute = 140,        /* ExtendedAttribute  */
  YYSYMBOL_ExtendedAttributeRest = 141,    /* ExtendedAttributeRest  */
  YYSYMBOL_ExtendedAttributeInner = 142,   /* ExtendedAttributeInner  */
  YYSYMBOL_Other = 143,                    /* Other  */
  YYSYMBOL_ArgumentNameKeyword = 144,      /* ArgumentNameKeyword  */
  YYSYMBOL_OtherOrComma = 145,             /* OtherOrComma  */
  YYSYMBOL_Type = 146,                     /* Type  */
  YYSYMBOL_SingleType = 147,               /* SingleType  */
  YYSYMBOL_UnionType = 148,                /* UnionType  */
  YYSYMBOL_UnionMemberType = 149,          /* UnionMemberType  */
  YYSYMBOL_UnionMemberTypes = 150,         /* UnionMemberTypes  */
  YYSYMBOL_NonAnyType = 151,               /* NonAnyType  */
  YYSYMBOL_ConstType = 152,                /* ConstType  */
  YYSYMBOL_PrimitiveType = 153,            /* PrimitiveType  */
  YYSYMBOL_UnrestrictedFloatType = 154,    /* UnrestrictedFloatType  */
  YYSYMBOL_FloatType = 155,                /* FloatType  */
  YYSYMBOL_UnsignedIntegerType = 156,      /* UnsignedIntegerType  */
  YYSYMBOL_IntegerType = 157,              /* IntegerType  */
  YYSYMBOL_OptionalLong = 158,             /* OptionalLong  */
  YYSYMBOL_PromiseType = 159,              /* PromiseType  */
  YYSYMBOL_TypeSuffix = 160,               /* TypeSuffix  */
  YYSYMBOL_TypeSuffixStartingWithArray = 161, /* TypeSuffixStartingWithArray  */
  YYSYMBOL_Null = 162,                     /* Null  */
  YYSYMBOL_ReturnType = 163                /* ReturnType  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined WEBIDL_LTYPE_IS_TRIVIAL && WEBIDL_LTYPE_IS_TRIVIAL \
             && defined WEBIDL_STYPE_IS_TRIVIAL && WEBIDL_STYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   819

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  81
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  83
/* YYNRULES -- Number of rules.  */
#define YYNRULES  232
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  387

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   320


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      72,    73,     2,     2,    71,    74,    79,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    70,    68,
      75,    69,    76,    80,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    77,     2,    78,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    66,     2,    67,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65
};

#if WEBIDL_DEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   230,   230,   234,   240,   250,   252,   254,   256,   258,
     260,   262,   268,   273,   278,   280,   285,   326,   334,   336,
     341,   372,   376,   428,   430,   432,   434,   439,   480,   484,
     497,   508,   536,   573,   577,   586,   588,   596,   603,   606,
     612,   616,   624,   634,   639,   640,   646,   647,   654,   662,
     670,   702,   720,   722,   724,   731,   739,   746,   756,   766,
     776,   786,   799,   804,   806,   811,   813,   819,   829,   860,
     869,   890,   911,   919,   921,   926,   936,   940,   952,   956,
     965,   973,   981,   997,  1001,  1009,  1016,  1023,  1033,  1040,
    1050,  1062,  1066,  1078,  1082,  1091,  1095,  1104,  1112,  1121,
    1133,  1135,  1141,  1145,  1153,  1158,  1167,  1171,  1179,  1181,
    1186,  1192,  1196,  1207,  1211,  1223,  1228,  1234,  1240,  1248,
    1260,  1274,  1286,  1290,  1299,  1303,  1312,  1322,  1332,  1343,
    1350,  1355,  1360,  1365,  1370,  1375,  1380,  1385,  1390,  1395,
    1400,  1405,  1410,  1415,  1420,  1425,  1430,  1435,  1440,  1445,
    1450,  1455,  1460,  1465,  1470,  1475,  1480,  1485,  1490,  1495,
    1500,  1505,  1510,  1515,  1520,  1530,  1535,  1540,  1545,  1550,
    1555,  1560,  1565,  1570,  1575,  1580,  1585,  1590,  1595,  1600,
    1605,  1610,  1615,  1620,  1625,  1630,  1635,  1643,  1648,  1656,
    1661,  1670,  1672,  1683,  1691,  1696,  1702,  1711,  1715,  1726,
    1731,  1737,  1744,  1753,  1760,  1767,  1777,  1782,  1796,  1798,
    1800,  1807,  1814,  1824,  1831,  1836,  1843,  1853,  1860,  1865,
    1872,  1889,  1893,  1901,  1910,  1914,  1919,  1928,  1932,  1939,
    1942,  1947,  1949
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "TOK_ANY",
  "TOK_ATTRIBUTE", "TOK_BOOLEAN", "TOK_BYTE", "TOK_CALLBACK", "TOK_CONST",
  "TOK_CONSTRUCTOR", "TOK_CREATOR", "TOK_DATE", "TOK_DELETER",
  "TOK_DICTIONARY", "TOK_DOUBLE", "TOK_ELLIPSIS", "TOK_ENUM", "TOK_EOL",
  "TOK_EXCEPTION", "TOK_FALSE", "TOK_FLOAT", "TOK_GETRAISES", "TOK_GETTER",
  "TOK_IMPLEMENTS", "TOK_IN", "TOK_INFINITY", "TOK_INHERIT",
  "TOK_INTERFACE", "TOK_ITERABLE", "TOK_LEGACYCALLER",
  "TOK_LEGACYITERABLE", "TOK_LONG", "TOK_MODULE", "TOK_NAN", "TOK_NATIVE",
  "TOK_NAMEDCONSTRUCTOR", "TOK_NULL_LITERAL", "TOK_OBJECT", "TOK_OCTET",
  "TOK_OMITTABLE", "TOK_OPTIONAL", "TOK_OR", "TOK_PARTIAL", "TOK_PROMISE",
  "TOK_RAISES", "TOK_READONLY", "TOK_REQUIRED", "TOK_SETRAISES",
  "TOK_SETTER", "TOK_SEQUENCE", "TOK_SHORT", "TOK_STATIC", "TOK_STRING",
  "TOK_STRINGIFIER", "TOK_TRUE", "TOK_TYPEDEF", "TOK_UNRESTRICTED",
  "TOK_UNSIGNED", "TOK_VOID", "TOK_POUND_SIGN", "TOK_IDENTIFIER",
  "TOK_INT_LITERAL", "TOK_FLOAT_LITERAL", "TOK_STRING_LITERAL",
  "TOK_OTHER_LITERAL", "TOK_JAVADOC", "'{'", "'}'", "';'", "'='", "':'",
  "','", "'('", "')'", "'-'", "'<'", "'>'", "'['", "']'", "'.'", "'?'",
  "$accept", "Definitions", "Definition", "CallbackOrInterface",
  "CallbackRestOrInterface", "Interface", "Partial", "PartialDefinition",
  "PartialInterface", "InterfaceMembers", "InterfaceMember", "Dictionary",
  "DictionaryMembers", "DictionaryMember", "PartialDictionary", "Default",
  "DefaultValue", "Exception", "ExceptionMembers", "Inheritance", "Enum",
  "EnumValueList", "EnumValueListComma", "EnumValueListString",
  "CallbackRest", "Typedef", "ImplementsStatement", "Const", "ConstValue",
  "BooleanLiteral", "FloatLiteral", "AttributeOrOperation",
  "StringifierAttributeOrOperation", "Attribute", "StaticMember",
  "StaticMemberRest", "AttributeRest", "AttributeName",
  "AttributeNameKeyword", "Inherit", "ReadOnly", "Operation",
  "SpecialOperation", "Specials", "Special", "OperationRest",
  "OptionalIdentifier", "ArgumentList", "Arguments", "Argument",
  "OptionalOrRequiredArgument", "ArgumentName", "Ellipsis", "Iterable",
  "OptionalType", "ExceptionMember", "ExceptionField",
  "ExtendedAttributeList", "ExtendedAttributes", "ExtendedAttribute",
  "ExtendedAttributeRest", "ExtendedAttributeInner", "Other",
  "ArgumentNameKeyword", "OtherOrComma", "Type", "SingleType", "UnionType",
  "UnionMemberType", "UnionMemberTypes", "NonAnyType", "ConstType",
  "PrimitiveType", "UnrestrictedFloatType", "FloatType",
  "UnsignedIntegerType", "IntegerType", "OptionalLong", "PromiseType",
  "TypeSuffix", "TypeSuffixStartingWithArray", "Null", "ReturnType", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-289)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-94)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      16,  -289,    11,  -289,   256,    32,  -289,  -289,  -289,  -289,
    -289,  -289,     0,  -289,  -289,  -289,  -289,  -289,  -289,  -289,
    -289,  -289,  -289,  -289,  -289,  -289,  -289,  -289,  -289,  -289,
    -289,  -289,  -289,    12,  -289,  -289,  -289,  -289,  -289,  -289,
    -289,  -289,  -289,  -289,  -289,  -289,  -289,  -289,  -289,  -289,
    -289,  -289,  -289,  -289,  -289,  -289,  -289,   334,  -289,  -289,
    -289,   334,  -289,  -289,  -289,   334,  -289,  -289,   -20,   256,
    -289,    -3,    18,    34,    39,    41,     9,    27,    83,  -289,
    -289,  -289,  -289,  -289,  -289,  -289,  -289,  -289,   -10,    47,
     334,  -289,   334,   334,    51,  -289,   334,    40,    36,   256,
      42,  -289,  -289,    50,  -289,  -289,  -289,    57,    55,    57,
      57,    69,    79,  -289,  -289,  -289,   660,    81,    70,    73,
     460,    76,    78,    77,    74,   256,  -289,   256,   256,   -20,
    -289,   508,    91,    87,    93,    88,    92,    94,    98,    80,
    -289,  -289,   -15,  -289,  -289,   128,   -15,  -289,    90,    99,
    -289,   -15,    46,    -1,   -15,   708,   109,  -289,   -15,  -289,
     -15,  -289,  -289,  -289,  -289,   -15,   108,  -289,    27,  -289,
     660,  -289,   165,   -10,   334,   334,   334,  -289,  -289,  -289,
    -289,  -289,  -289,   110,  -289,   -42,   113,   123,   -30,  -289,
     -42,  -289,   107,  -289,   116,    80,  -289,  -289,  -289,  -289,
     508,   660,  -289,  -289,  -289,  -289,   114,   -15,   158,  -289,
     132,  -289,  -289,  -289,  -289,    73,   759,  -289,   759,   129,
    -289,  -289,  -289,   -10,   134,   556,   140,  -289,   138,   141,
     612,   -21,   142,   -13,   -15,   -15,  -289,   131,   135,   136,
    -289,   708,  -289,  -289,  -289,   143,  -289,  -289,  -289,   153,
     145,   660,   -42,   168,   113,  -289,  -289,   162,   111,  -289,
     -30,  -289,   172,   166,   167,   169,   170,  -289,  -289,  -289,
     155,   -15,   192,   737,  -289,   173,  -289,   176,  -289,   143,
    -289,  -289,   155,   180,   155,  -289,   174,  -289,  -289,  -289,
    -289,  -289,   171,  -289,   177,  -289,   412,     1,  -289,  -289,
    -289,  -289,  -289,   198,  -289,  -289,   118,  -289,   184,  -289,
    -289,  -289,  -289,  -289,   708,   175,  -289,  -289,  -289,  -289,
    -289,  -289,  -289,  -289,   222,  -289,  -289,  -289,  -289,  -289,
     181,   183,  -289,   186,  -289,  -289,   660,   660,  -289,  -289,
     249,   184,  -289,  -289,  -289,  -289,  -289,   185,   249,   508,
     118,  -289,   192,  -289,  -289,  -289,  -289,    72,   202,   201,
     660,  -289,  -289,   -10,  -289,   184,  -289,  -289,   188,   660,
     204,   220,   -18,   217,  -289,  -289,  -289,   227,  -289,  -289,
    -289,   231,  -289,   232,  -289,  -289,  -289
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     4,   111,     1,     0,     0,   147,   165,   148,   149,
     166,   167,   118,   168,   143,   169,   170,   150,   136,   171,
     172,   151,   152,   173,   174,   145,   175,   176,   177,   178,
     179,   153,   146,     0,   154,   155,   156,   158,   157,   180,
     181,   182,   159,   160,   183,   144,   184,   161,   185,   186,
     162,   163,   131,   129,   130,   132,   133,   124,   138,   140,
     137,   124,   134,   139,   141,   124,   135,   142,   113,   122,
     164,     0,     0,     0,     0,     0,     0,   111,     0,     3,
       5,    13,     6,     7,     8,     9,    10,    11,   111,     0,
     124,   188,   124,   124,     0,   187,   124,     0,     0,     0,
       0,   123,   121,     0,    12,    15,    14,    40,     0,    40,
      40,     0,     0,    17,    18,    19,     0,     0,     0,    95,
       0,     0,     0,     0,     0,   122,   128,   122,   122,   113,
     112,     0,     0,     0,     0,     0,     0,     0,     0,   227,
     210,   211,   224,   216,   215,   221,   224,   212,     0,     0,
     219,   224,     0,     0,   224,     0,     0,   189,   224,   191,
     224,   209,   214,   208,   218,   224,     0,   119,   111,    94,
       0,    97,   102,   111,   124,   124,   124,   117,   115,   116,
     114,   232,   231,     0,    41,   111,    45,     0,   111,    21,
     111,    21,     0,   192,     0,   227,   205,   222,   220,   204,
       0,     0,   201,   213,   217,   202,     0,   224,     0,   194,
       0,   190,   199,   200,    50,    95,     0,   103,     0,     0,
     127,   125,   126,   111,     0,     0,    47,    43,     0,     0,
       0,   111,     0,   111,   224,   224,   226,     0,     0,     0,
     195,     0,    49,    96,   101,    33,   100,    99,   120,     0,
       0,     0,   111,     0,    45,    44,    42,     0,     0,   108,
     111,   109,     0,     0,    76,     0,     0,   228,   225,   223,
     229,   224,   197,     0,    98,     0,    27,     0,    29,    33,
      46,    37,   229,     0,   229,    39,     0,    16,    87,    88,
      85,    77,     0,    89,     0,    86,    78,    76,    22,    23,
      24,    63,    25,    78,    64,    81,    83,    26,    91,    32,
      20,   230,   203,   196,     0,     0,    57,    60,    61,    55,
      56,    54,    58,    36,     0,    34,    35,    52,    53,    48,
       0,     0,   207,     0,   206,   110,     0,     0,    79,    69,
       0,    91,    92,    67,    62,    65,    66,     0,     0,     0,
      83,    80,   197,   193,    59,    30,    31,     0,   106,     0,
       0,    70,    71,   111,    68,    91,    84,   198,     0,     0,
       0,     0,     0,     0,    82,    51,   107,     0,   105,    75,
      74,     0,    73,     0,   104,    72,    90
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -289,  -289,  -289,  -289,  -289,   230,  -289,  -289,  -289,   112,
    -289,  -289,  -172,  -289,  -289,    44,  -289,  -289,    61,     2,
    -289,  -289,    75,  -289,  -289,  -289,  -289,    63,   -23,  -289,
    -289,  -289,  -289,    54,  -289,  -289,   -33,  -289,  -289,  -289,
      52,  -289,  -289,    -7,   102,  -288,  -289,  -171,   154,   190,
    -289,   150,  -289,  -289,  -289,  -289,  -289,    -2,   244,     4,
     -43,   -52,    10,  -185,  -289,  -115,  -289,  -152,  -231,    25,
    -151,  -289,   120,  -289,   229,  -289,   226,  -289,  -289,  -139,
     206,  -184,  -194
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     2,    79,    80,   104,    81,    82,   113,   114,   231,
     298,    83,   224,   252,   115,   274,   325,    84,   229,   133,
      85,   187,   227,   255,   106,    86,    87,   259,   326,   327,
     328,   300,   344,   301,   302,   339,   361,   381,   382,   303,
     340,   304,   305,   349,   350,   346,   347,   118,   169,   119,
     171,   245,   218,   307,   370,   260,   261,   120,   100,   101,
     102,    94,    95,    70,    96,   182,   157,   158,   208,   315,
     159,   283,   160,   161,   162,   163,   164,   198,   165,   196,
     193,   312,   183
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
       5,   156,   219,   207,   209,   172,   237,   199,    68,    97,
     272,     3,   202,    98,    69,   205,    -2,     1,   232,   211,
     351,   212,   111,    -2,    75,   -28,   213,   291,   379,    -2,
     145,   246,    -2,   246,    -2,     4,   112,   -38,   122,    71,
     123,   124,   380,    -2,   126,    72,   263,     4,    73,   150,
      74,    99,   249,   362,   266,   216,     4,   103,    -2,    75,
     143,   342,   194,   -93,     4,   195,   144,     4,   240,   343,
     308,    -2,    88,   -91,    76,   116,    -2,   374,   107,    69,
     278,    89,   177,   352,   178,   179,   238,    77,     4,   207,
     209,   316,    78,    -2,   108,   267,   268,   317,   332,   109,
     334,   110,   341,   129,     4,   318,   117,   121,   319,    69,
     253,   135,   136,   127,   128,   262,   140,   141,   125,   131,
     130,   134,   220,   221,   222,   143,   320,   132,   288,   137,
     289,   144,   313,   321,   322,    69,   277,    69,    69,   138,
     290,   166,   145,   167,   168,   174,   324,   293,   173,   147,
     175,   184,   176,   185,   188,   365,   186,   192,   189,   197,
     190,   150,   207,   209,   191,   200,   295,   152,   153,   210,
     139,   282,   140,   141,   201,   258,   214,   288,   142,   289,
     217,   143,   223,   225,   226,   234,   230,   144,   225,   290,
     228,   239,   373,   291,   235,   292,   293,   294,   145,   241,
     242,   250,   248,   254,   146,   147,   256,   269,   257,   265,
     148,   270,   273,   276,   271,   295,   149,   150,   296,   151,
     297,   358,   359,   152,   153,   181,   275,   154,   279,   264,
     281,   264,   286,   314,   287,   311,   330,   309,   310,   155,
     333,   329,   335,   338,   342,   372,   336,   354,   353,   355,
     225,   356,   337,   360,   376,   357,   375,   363,   230,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,   369,    20,    21,    22,   371,    23,    24,
     377,    25,    26,    27,    28,    29,    30,    31,   378,    32,
     383,    33,    34,    35,    36,   384,    37,    38,    39,   385,
     386,   105,    40,   233,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,   364,    52,    53,    54,    55,
      56,   285,    57,   331,    58,    59,    60,   299,    61,   280,
      62,    63,    64,    65,   368,    66,    67,     6,     7,     8,
       9,    10,    11,   366,    13,    14,    15,    16,    17,    18,
      19,   345,    20,    21,    22,   348,    23,    24,   215,    25,
      26,    27,    28,    29,    30,    31,   306,    32,   247,   243,
      34,    35,    36,   180,    37,    38,    39,   367,   284,   204,
      40,   203,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,     0,    52,    53,    54,    55,    56,     0,
      90,   236,    58,    59,    60,    91,    92,     0,    62,    63,
      64,    93,     0,    66,    67,   139,     0,   140,   141,     0,
       0,     0,     0,   142,     0,     0,   143,     0,     0,     0,
       0,     0,   144,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   145,     0,     0,     0,     0,     0,   146,
     147,     0,     0,     0,     0,   148,     0,   338,     0,     0,
       0,   149,   150,   139,   151,   140,   141,     0,   152,   153,
     181,   142,   154,     0,   143,     0,     0,     0,     0,     0,
     144,     0,     0,     0,   155,     0,     0,     0,     0,     0,
       0,   145,     0,     0,     0,     0,     0,   146,   147,     0,
     170,     0,     0,   148,     0,     0,     0,     0,     0,   149,
     150,   139,   151,   140,   141,     0,   152,   153,     0,   142,
     154,     0,   143,     0,     0,     0,     0,     0,   144,     0,
       0,     0,   155,     0,     0,     0,     0,     0,     0,   145,
       0,     0,     0,     0,     0,   146,   147,     0,     0,     0,
       0,   148,     0,     0,     0,     0,     0,   149,   150,   139,
     151,   140,   141,     0,   152,   153,   181,   142,   154,     0,
     143,     0,     0,     0,     0,     0,   144,     0,     0,     0,
     155,     0,     0,     0,     0,     0,     0,   145,     0,     0,
       0,     0,     0,   146,   147,     0,     0,     0,     0,   148,
       0,     0,   251,     0,     0,   149,   150,     0,   151,     0,
       0,     0,   152,   153,     0,   139,   154,   140,   141,     0,
     258,     0,     0,   142,     0,     0,   143,     0,   155,     0,
       0,     0,   144,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   145,     0,     0,     0,     0,     0,   146,
     147,     0,     0,     0,     0,   148,     0,     0,     0,     0,
       0,   149,   150,   139,   151,   140,   141,     0,   152,   153,
       0,   142,   154,     0,   143,     0,     0,     0,     0,     0,
     144,     0,     0,     0,   155,     0,     0,     0,     0,     0,
       0,   145,     0,     0,     0,     0,     0,   146,   147,     0,
       0,     0,     0,   148,     0,     0,     0,     0,     0,   149,
     150,   206,   151,   140,   141,     0,   152,   153,     0,   142,
     154,     0,   143,     0,     0,     0,     0,     0,   144,     0,
       0,     0,   155,     0,     0,     0,     0,     0,     0,   145,
       0,     0,     0,     0,     0,   146,   147,     0,     0,     0,
       0,   148,     0,     0,     0,     0,   316,   149,   150,     0,
     151,     0,   317,     7,   152,   153,    10,    11,   154,    13,
     318,    15,    16,   319,     0,    19,     0,    20,     0,     0,
     155,    23,    24,     0,     0,    26,    27,    28,    29,    30,
       0,   320,     0,     0,     0,     0,     0,     0,   321,   322,
     323,    39,     0,     0,     0,    40,     0,    41,     0,     0,
      44,   324,    46,     0,    48,    49,     0,     0,     0,   244
};

static const yytype_int16 yycheck[] =
{
       2,   116,   173,   155,   155,   120,   200,   146,     4,    61,
     241,     0,   151,    65,     4,   154,     0,     1,   190,   158,
     308,   160,    13,     7,    27,    67,   165,    26,    46,    13,
      31,   216,    16,   218,    18,    77,    27,    67,    90,     7,
      92,    93,    60,    27,    96,    13,    67,    77,    16,    50,
      18,    71,   223,   341,    67,   170,    77,    60,    42,    27,
      14,    60,    77,    73,    77,    80,    20,    77,   207,    68,
     264,    55,    72,    72,    42,    77,    60,   365,    60,    69,
     252,    69,   125,   314,   127,   128,   201,    55,    77,   241,
     241,    19,    60,    77,    60,   234,   235,    25,   282,    60,
     284,    60,   296,    99,    77,    33,    23,    60,    36,    99,
     225,   109,   110,    73,    78,   230,     5,     6,    67,    69,
      78,    66,   174,   175,   176,    14,    54,    70,    10,    60,
      12,    20,   271,    61,    62,   125,   251,   127,   128,    60,
      22,    60,    31,    73,    71,    67,    74,    29,    72,    38,
      73,    60,    78,    66,    66,   349,    63,    77,    66,    31,
      66,    50,   314,   314,    66,    75,    48,    56,    57,    60,
       3,    60,     5,     6,    75,     8,    68,    10,    11,    12,
      15,    14,    72,   185,    71,    78,   188,    20,   190,    22,
      67,    77,   363,    26,    78,    28,    29,    30,    31,    41,
      68,    67,    73,    63,    37,    38,    68,    76,    67,    67,
      43,    76,    69,    68,    78,    48,    49,    50,    51,    52,
      53,   336,   337,    56,    57,    58,    73,    60,    60,   231,
      68,   233,    60,    41,    68,    80,    60,    68,    68,    72,
      60,    68,    68,    45,    60,   360,    75,    25,    73,    68,
     252,    68,    75,     4,   369,    69,    68,    72,   260,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    71,    18,    19,    20,    76,    22,    23,
      76,    25,    26,    27,    28,    29,    30,    31,    68,    33,
      73,    35,    36,    37,    38,    68,    40,    41,    42,    68,
      68,    71,    46,   191,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,   348,    60,    61,    62,    63,
      64,   260,    66,   279,    68,    69,    70,   264,    72,   254,
      74,    75,    76,    77,   357,    79,    80,     3,     4,     5,
       6,     7,     8,   350,    10,    11,    12,    13,    14,    15,
      16,   297,    18,    19,    20,   303,    22,    23,   168,    25,
      26,    27,    28,    29,    30,    31,   264,    33,   218,   215,
      36,    37,    38,   129,    40,    41,    42,   352,   258,   153,
      46,   152,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    -1,    60,    61,    62,    63,    64,    -1,
      66,   195,    68,    69,    70,    71,    72,    -1,    74,    75,
      76,    77,    -1,    79,    80,     3,    -1,     5,     6,    -1,
      -1,    -1,    -1,    11,    -1,    -1,    14,    -1,    -1,    -1,
      -1,    -1,    20,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    31,    -1,    -1,    -1,    -1,    -1,    37,
      38,    -1,    -1,    -1,    -1,    43,    -1,    45,    -1,    -1,
      -1,    49,    50,     3,    52,     5,     6,    -1,    56,    57,
      58,    11,    60,    -1,    14,    -1,    -1,    -1,    -1,    -1,
      20,    -1,    -1,    -1,    72,    -1,    -1,    -1,    -1,    -1,
      -1,    31,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,
      40,    -1,    -1,    43,    -1,    -1,    -1,    -1,    -1,    49,
      50,     3,    52,     5,     6,    -1,    56,    57,    -1,    11,
      60,    -1,    14,    -1,    -1,    -1,    -1,    -1,    20,    -1,
      -1,    -1,    72,    -1,    -1,    -1,    -1,    -1,    -1,    31,
      -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    -1,
      -1,    43,    -1,    -1,    -1,    -1,    -1,    49,    50,     3,
      52,     5,     6,    -1,    56,    57,    58,    11,    60,    -1,
      14,    -1,    -1,    -1,    -1,    -1,    20,    -1,    -1,    -1,
      72,    -1,    -1,    -1,    -1,    -1,    -1,    31,    -1,    -1,
      -1,    -1,    -1,    37,    38,    -1,    -1,    -1,    -1,    43,
      -1,    -1,    46,    -1,    -1,    49,    50,    -1,    52,    -1,
      -1,    -1,    56,    57,    -1,     3,    60,     5,     6,    -1,
       8,    -1,    -1,    11,    -1,    -1,    14,    -1,    72,    -1,
      -1,    -1,    20,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    31,    -1,    -1,    -1,    -1,    -1,    37,
      38,    -1,    -1,    -1,    -1,    43,    -1,    -1,    -1,    -1,
      -1,    49,    50,     3,    52,     5,     6,    -1,    56,    57,
      -1,    11,    60,    -1,    14,    -1,    -1,    -1,    -1,    -1,
      20,    -1,    -1,    -1,    72,    -1,    -1,    -1,    -1,    -1,
      -1,    31,    -1,    -1,    -1,    -1,    -1,    37,    38,    -1,
      -1,    -1,    -1,    43,    -1,    -1,    -1,    -1,    -1,    49,
      50,     3,    52,     5,     6,    -1,    56,    57,    -1,    11,
      60,    -1,    14,    -1,    -1,    -1,    -1,    -1,    20,    -1,
      -1,    -1,    72,    -1,    -1,    -1,    -1,    -1,    -1,    31,
      -1,    -1,    -1,    -1,    -1,    37,    38,    -1,    -1,    -1,
      -1,    43,    -1,    -1,    -1,    -1,    19,    49,    50,    -1,
      52,    -1,    25,     4,    56,    57,     7,     8,    60,    10,
      33,    12,    13,    36,    -1,    16,    -1,    18,    -1,    -1,
      72,    22,    23,    -1,    -1,    26,    27,    28,    29,    30,
      -1,    54,    -1,    -1,    -1,    -1,    -1,    -1,    61,    62,
      63,    42,    -1,    -1,    -1,    46,    -1,    48,    -1,    -1,
      51,    74,    53,    -1,    55,    56,    -1,    -1,    -1,    60
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     1,    82,     0,    77,   138,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      18,    19,    20,    22,    23,    25,    26,    27,    28,    29,
      30,    31,    33,    35,    36,    37,    38,    40,    41,    42,
      46,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    60,    61,    62,    63,    64,    66,    68,    69,
      70,    72,    74,    75,    76,    77,    79,    80,   140,   143,
     144,     7,    13,    16,    18,    27,    42,    55,    60,    83,
      84,    86,    87,    92,    98,   101,   106,   107,    72,    69,
      66,    71,    72,    77,   142,   143,   145,   142,   142,    71,
     139,   140,   141,    60,    85,    86,   105,    60,    60,    60,
      60,    13,    27,    88,    89,    95,   138,    23,   128,   130,
     138,    60,   142,   142,   142,    67,   142,    73,    78,   140,
      78,    69,    70,   100,    66,   100,   100,    60,    60,     3,
       5,     6,    11,    14,    20,    31,    37,    38,    43,    49,
      50,    52,    56,    57,    60,    72,   146,   147,   148,   151,
     153,   154,   155,   156,   157,   159,    60,    73,    71,   129,
      40,   131,   146,    72,    67,    73,    78,   141,   141,   141,
     139,    58,   146,   163,    60,    66,    63,   102,    66,    66,
      66,    66,    77,   161,    77,    80,   160,    31,   158,   160,
      75,    75,   160,   155,   157,   160,     3,   148,   149,   151,
      60,   160,   160,   160,    68,   130,   146,    15,   133,   128,
     142,   142,   142,    72,    93,   138,    71,   103,    67,    99,
     138,    90,    93,    90,    78,    78,   161,   163,   146,    77,
     160,    41,    68,   129,    60,   132,   144,   132,    73,   128,
      67,    46,    94,   146,    63,   104,    68,    67,     8,   108,
     136,   137,   146,    67,   138,    67,    67,   160,   160,    76,
      76,    78,   149,    69,    96,    73,    68,   146,    93,    60,
     103,    68,    60,   152,   153,    99,    60,    68,    10,    12,
      22,    26,    28,    29,    30,    48,    51,    53,    91,   108,
     112,   114,   115,   120,   122,   123,   125,   134,   163,    68,
      68,    80,   162,   160,    41,   150,    19,    25,    33,    36,
      54,    61,    62,    63,    74,    97,   109,   110,   111,    68,
      60,    96,   162,    60,   162,    68,    75,    75,    45,   116,
     121,   163,    60,    68,   113,   114,   126,   127,   121,   124,
     125,   126,   149,    73,    25,    68,    68,    69,   146,   146,
       4,   117,   126,    72,   117,   163,   124,   150,   109,    71,
     135,    76,   146,   128,   126,    68,   146,    76,    68,    46,
      60,   118,   119,    73,    68,    68,    68
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    81,    82,    82,    82,    83,    83,    83,    83,    83,
      83,    83,    84,    84,    85,    85,    86,    87,    88,    88,
      89,    90,    90,    91,    91,    91,    91,    92,    93,    93,
      94,    94,    95,    96,    96,    97,    97,    98,    99,    99,
     100,   100,   101,   102,   103,   103,   104,   104,   105,   106,
     107,   108,   109,   109,   109,   109,   110,   110,   111,   111,
     111,   111,   112,   112,   112,   113,   113,   113,   114,   115,
     116,   116,   117,   118,   118,   119,   120,   120,   121,   121,
     122,   122,   123,   124,   124,   125,   125,   125,   125,   125,
     126,   127,   127,   128,   128,   129,   129,   130,   131,   131,
     132,   132,   133,   133,   134,   134,   135,   135,   136,   136,
     137,   138,   138,   139,   139,   140,   140,   140,   140,   140,
     140,   140,   141,   141,   142,   142,   142,   142,   142,   143,
     143,   143,   143,   143,   143,   143,   143,   143,   143,   143,
     143,   143,   143,   143,   143,   143,   143,   143,   143,   143,
     143,   143,   143,   143,   143,   143,   143,   143,   143,   143,
     143,   143,   143,   143,   143,   144,   144,   144,   144,   144,
     144,   144,   144,   144,   144,   144,   144,   144,   144,   144,
     144,   144,   144,   144,   144,   144,   144,   145,   145,   146,
     146,   147,   147,   148,   149,   149,   149,   150,   150,   151,
     151,   151,   151,   151,   151,   151,   152,   152,   153,   153,
     153,   153,   153,   154,   154,   155,   155,   156,   156,   157,
     157,   158,   158,   159,   160,   160,   160,   161,   161,   162,
     162,   163,   163
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     0,     3,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     1,     1,     1,     7,     2,     1,     1,
       6,     0,     3,     1,     1,     1,     1,     7,     0,     3,
       4,     4,     6,     0,     2,     1,     1,     7,     0,     3,
       0,     2,     6,     2,     2,     0,     2,     0,     7,     5,
       4,     6,     1,     1,     1,     1,     1,     1,     1,     2,
       1,     1,     2,     1,     1,     1,     1,     1,     3,     2,
       2,     2,     4,     1,     1,     1,     0,     1,     0,     1,
       2,     1,     4,     0,     2,     1,     1,     1,     1,     1,
       5,     0,     1,     0,     2,     0,     3,     2,     4,     3,
       1,     1,     0,     1,     6,     5,     0,     2,     1,     1,
       3,     0,     4,     0,     3,     4,     4,     4,     1,     4,
       6,     2,     0,     1,     0,     4,     4,     4,     2,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     1,     2,     6,     1,     2,     4,     0,     3,     2,
       2,     2,     2,     5,     2,     2,     2,     2,     1,     1,
       1,     1,     1,     2,     1,     1,     1,     2,     1,     1,
       2,     0,     1,     4,     0,     3,     2,     0,     3,     0,
       1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = WEBIDL_EMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == WEBIDL_EMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (&yylloc, webidl_ast, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use WEBIDL_error or WEBIDL_UNDEF. */
#define YYERRCODE WEBIDL_UNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if WEBIDL_DEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined WEBIDL_LTYPE_IS_TRIVIAL && WEBIDL_LTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location, webidl_ast); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct webidl_node **webidl_ast)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  YY_USE (webidl_ast);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct webidl_node **webidl_ast)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp, webidl_ast);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule, struct webidl_node **webidl_ast)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]), webidl_ast);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule, webidl_ast); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !WEBIDL_DEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !WEBIDL_DEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp, struct webidl_node **webidl_ast)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  YY_USE (webidl_ast);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (struct webidl_node **webidl_ast)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

/* Location data for the lookahead symbol.  */
static YYLTYPE yyloc_default
# if defined WEBIDL_LTYPE_IS_TRIVIAL && WEBIDL_LTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
YYLTYPE yylloc = yyloc_default;

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = WEBIDL_EMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == WEBIDL_EMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, &yylloc);
    }

  if (yychar <= WEBIDL_EOF)
    {
      yychar = WEBIDL_EOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == WEBIDL_error)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = WEBIDL_UNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = WEBIDL_EMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* Definitions: %empty  */
#line 230 "webidl-parser.y"
        {
          (yyval.node) = NULL;
        }
#line 2064 "webidl-parser.c"
    break;

  case 3: /* Definitions: Definitions ExtendedAttributeList Definition  */
#line 235 "webidl-parser.y"
        {
            webidl_node_add((yyvsp[0].node), (yyvsp[-1].node));
            (yyval.node) = *webidl_ast = webidl_node_prepend(*webidl_ast, (yyvsp[0].node));
        }
#line 2073 "webidl-parser.c"
    break;

  case 4: /* Definitions: error  */
#line 241 "webidl-parser.y"
        {
            fprintf(stderr, "%d: %s\n", yylloc.first_line, errtxt);
            free(errtxt);
            YYABORT ;
        }
#line 2083 "webidl-parser.c"
    break;

  case 12: /* CallbackOrInterface: TOK_CALLBACK CallbackRestOrInterface  */
#line 269 "webidl-parser.y"
        {
            (yyval.node) = (yyvsp[0].node);
        }
#line 2091 "webidl-parser.c"
    break;

  case 16: /* Interface: TOK_INTERFACE TOK_IDENTIFIER Inheritance '{' InterfaceMembers '}' ';'  */
#line 286 "webidl-parser.y"
        {
                /* extend interface with additional members */
                struct webidl_node *interface_node;
                struct webidl_node *members = NULL;

                if ((yyvsp[-4].text) != NULL) {
                        members = webidl_node_new(WEBIDL_NODE_TYPE_INHERITANCE,
                                                  members,
                                                  (yyvsp[-4].text));
                }

                members = webidl_node_new(WEBIDL_NODE_TYPE_LIST, members, (yyvsp[-2].node));


                interface_node = webidl_node_find_type_ident(*webidl_ast,
                                                     WEBIDL_NODE_TYPE_INTERFACE,
                                                             (yyvsp[-5].text));

                if (interface_node == NULL) {
                        /* no existing interface - create one with ident */
                        members = webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                  members,
                                                  (yyvsp[-5].text));

                        (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_INTERFACE,
                                             NULL,
                                             members);
                } else {
                        /* update the existing interface */

                        /* link member node into interfaces_node */
                        webidl_node_add(interface_node, members);

                        (yyval.node) = NULL; /* updating so no need to add a new node */
                }
        }
#line 2132 "webidl-parser.c"
    break;

  case 17: /* Partial: TOK_PARTIAL PartialDefinition  */
#line 327 "webidl-parser.y"
        {
            (yyval.node) = (yyvsp[0].node);
        }
#line 2140 "webidl-parser.c"
    break;

  case 20: /* PartialInterface: TOK_INTERFACE TOK_IDENTIFIER '{' InterfaceMembers '}' ';'  */
#line 342 "webidl-parser.y"
        {
            /* extend interface with additional members */
            struct webidl_node *members;
            struct webidl_node *interface_node;

            interface_node = webidl_node_find_type_ident(*webidl_ast,
                                                     WEBIDL_NODE_TYPE_INTERFACE,
                                                     (yyvsp[-4].text));

            members = webidl_node_new(WEBIDL_NODE_TYPE_LIST, NULL, (yyvsp[-2].node));

            if (interface_node == NULL) {
                /* doesnt already exist so create it */

                members = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, members, (yyvsp[-4].text));

                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_INTERFACE, NULL, members);
            } else {
                /* update the existing interface */

                /* link member node into interfaces_node */
                webidl_node_add(interface_node, members);

                (yyval.node) = NULL; /* updating so no need to add a new node */
            }
        }
#line 2171 "webidl-parser.c"
    break;

  case 21: /* InterfaceMembers: %empty  */
#line 372 "webidl-parser.y"
        {
                (yyval.node) = NULL; /* empty */
        }
#line 2179 "webidl-parser.c"
    break;

  case 22: /* InterfaceMembers: InterfaceMembers ExtendedAttributeList InterfaceMember  */
#line 377 "webidl-parser.y"
        {
                /* This needs to deal with members with the same
                 * identifier which indicate polymorphism. this is
                 * handled in the AST by adding the argument lists for
                 * each polymorphism to the same
                 * WEBIDL_NODE_TYPE_OPERATION
                 *
                 * @todo need to consider qualifer/stringifier compatibility
                 */
                struct webidl_node *ident_node;
                struct webidl_node *list_node;

                ident_node = webidl_node_find_type(webidl_node_getnode((yyvsp[0].node)),
                                                   NULL,
                                                   WEBIDL_NODE_TYPE_IDENT);

                list_node = webidl_node_find_type(webidl_node_getnode((yyvsp[0].node)),
                                                  NULL,
                                                  WEBIDL_NODE_TYPE_LIST);

                if (ident_node == NULL) {
                        /* something with no ident - possibly constructors? */
                        /* @todo understand this better */

                        (yyval.node) = webidl_node_prepend((yyvsp[-2].node), (yyvsp[0].node));

                } else if (list_node == NULL) {
                        /* member with no argument list, usually an
                         * attribute, cannot be polymorphic
                         */

                        /* add extended attributes to parameter list */
                        webidl_node_add((yyvsp[0].node), (yyvsp[-1].node));

                        (yyval.node) = webidl_node_prepend((yyvsp[-2].node), (yyvsp[0].node));

                } else {
                        /* add extended attributes to parameter list */
                        webidl_node_add(list_node, (yyvsp[-1].node));

                        (yyval.node) = webidl_node_prepend((yyvsp[-2].node), (yyvsp[0].node));
                }
        }
#line 2227 "webidl-parser.c"
    break;

  case 27: /* Dictionary: TOK_DICTIONARY TOK_IDENTIFIER Inheritance '{' DictionaryMembers '}' ';'  */
#line 440 "webidl-parser.y"
        {
                /* extend dictionary with additional members */
                struct webidl_node *dictionary_node;
                struct webidl_node *members = NULL;

                if ((yyvsp[-4].text) != NULL) {
                        members = webidl_node_new(WEBIDL_NODE_TYPE_INHERITANCE,
                                                  members,
                                                  (yyvsp[-4].text));
                }

                members = webidl_node_new(WEBIDL_NODE_TYPE_LIST, members, (yyvsp[-2].node));

                dictionary_node = webidl_node_find_type_ident(
                                         *webidl_ast,
                                         WEBIDL_NODE_TYPE_DICTIONARY,
                                         (yyvsp[-5].text));

                if (dictionary_node == NULL) {
                        /* no existing interface - create one with ident */
                        members = webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                  members,
                                                  (yyvsp[-5].text));

                        (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_DICTIONARY,
                                             NULL,
                                             members);
                } else {
                        /* update the existing interface */

                        /* link member node into interfaces_node */
                        webidl_node_add(dictionary_node, members);

                        (yyval.node) = NULL; /* updating so no need to add a new node */
                }
        }
#line 2268 "webidl-parser.c"
    break;

  case 28: /* DictionaryMembers: %empty  */
#line 480 "webidl-parser.y"
        {
                (yyval.node) = NULL; /* empty */
        }
#line 2276 "webidl-parser.c"
    break;

  case 29: /* DictionaryMembers: ExtendedAttributeList DictionaryMember DictionaryMembers  */
#line 485 "webidl-parser.y"
        {
                /** \todo handle ExtendedAttributeList */
                (yyval.node) = webidl_node_append((yyvsp[0].node), (yyvsp[-1].node));
        }
#line 2285 "webidl-parser.c"
    break;

  case 30: /* DictionaryMember: TOK_REQUIRED Type TOK_IDENTIFIER ';'  */
#line 498 "webidl-parser.y"
        {
            struct webidl_node *member;
            /* add name */
            member = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, NULL, (yyvsp[-1].text));
            /* add type node */
            member = webidl_node_prepend(member, (yyvsp[-2].node));

            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_ARGUMENT, NULL, member);
        }
#line 2299 "webidl-parser.c"
    break;

  case 31: /* DictionaryMember: Type TOK_IDENTIFIER Default ';'  */
#line 509 "webidl-parser.y"
        {
            struct webidl_node *member;
            /* add name */
            member = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, NULL, (yyvsp[-2].text));
            /* add default */
            member = webidl_node_new(WEBIDL_NODE_TYPE_OPTIONAL, member, (yyvsp[-1].node));
            /* add type node */
            member = webidl_node_prepend(member, (yyvsp[-3].node));

            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_ARGUMENT, NULL, member);
        }
#line 2315 "webidl-parser.c"
    break;

  case 32: /* PartialDictionary: TOK_DICTIONARY TOK_IDENTIFIER '{' DictionaryMembers '}' ';'  */
#line 537 "webidl-parser.y"
        {
                /* extend dictionary with additional members */
                struct webidl_node *members;
                struct webidl_node *dictionary_node;

                dictionary_node = webidl_node_find_type_ident(
                                         *webidl_ast,
                                         WEBIDL_NODE_TYPE_DICTIONARY,
                                         (yyvsp[-4].text));

                members = webidl_node_new(WEBIDL_NODE_TYPE_LIST, NULL, (yyvsp[-2].node));

                if (dictionary_node == NULL) {
                        /* doesnt already exist so create it */

                        members = webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                  members,
                                                  (yyvsp[-4].text));

                        (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_DICTIONARY,
                                             NULL,
                                             members);
                } else {
                        /* update the existing dictionary */

                        /* link member node into dictionary node */
                        webidl_node_add(dictionary_node, members);

                        (yyval.node) = NULL; /* updating so no need to add a new node */
                }
        }
#line 2351 "webidl-parser.c"
    break;

  case 33: /* Default: %empty  */
#line 573 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2359 "webidl-parser.c"
    break;

  case 34: /* Default: '=' DefaultValue  */
#line 578 "webidl-parser.y"
        {
                (yyval.node) = (yyvsp[0].node);
        }
#line 2367 "webidl-parser.c"
    break;

  case 36: /* DefaultValue: TOK_STRING_LITERAL  */
#line 589 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_LITERAL_STRING, NULL, (yyvsp[0].text));
        }
#line 2375 "webidl-parser.c"
    break;

  case 37: /* Exception: TOK_EXCEPTION TOK_IDENTIFIER Inheritance '{' ExceptionMembers '}' ';'  */
#line 597 "webidl-parser.y"
        {
            (yyval.node) = NULL;
        }
#line 2383 "webidl-parser.c"
    break;

  case 40: /* Inheritance: %empty  */
#line 612 "webidl-parser.y"
        {
                (yyval.text) = NULL;
        }
#line 2391 "webidl-parser.c"
    break;

  case 41: /* Inheritance: ':' TOK_IDENTIFIER  */
#line 617 "webidl-parser.y"
        {
                (yyval.text) = (yyvsp[0].text);
        }
#line 2399 "webidl-parser.c"
    break;

  case 42: /* Enum: TOK_ENUM TOK_IDENTIFIER '{' EnumValueList '}' ';'  */
#line 625 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2407 "webidl-parser.c"
    break;

  case 48: /* CallbackRest: TOK_IDENTIFIER '=' ReturnType '(' ArgumentList ')' ';'  */
#line 655 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2415 "webidl-parser.c"
    break;

  case 49: /* Typedef: TOK_TYPEDEF ExtendedAttributeList Type TOK_IDENTIFIER ';'  */
#line 663 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2423 "webidl-parser.c"
    break;

  case 50: /* ImplementsStatement: TOK_IDENTIFIER TOK_IMPLEMENTS TOK_IDENTIFIER ';'  */
#line 671 "webidl-parser.y"
        {
            /* extend interface with implements members */
            struct webidl_node *implements;
            struct webidl_node *interface_node;


            interface_node = webidl_node_find_type_ident(*webidl_ast,
                                                     WEBIDL_NODE_TYPE_INTERFACE,
                                                     (yyvsp[-3].text));

            implements = webidl_node_new(WEBIDL_NODE_TYPE_INTERFACE_IMPLEMENTS, NULL, (yyvsp[-1].text));

            if (interface_node == NULL) {
                /* interface doesnt already exist so create it */

                implements = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, implements, (yyvsp[-3].text));

                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_INTERFACE, NULL, implements);
            } else {
                /* update the existing interface */

                /* link implements node into interfaces_node */
                webidl_node_add(interface_node, implements);

                (yyval.node) = NULL; /* updating so no need to add a new node */
            }
        }
#line 2455 "webidl-parser.c"
    break;

  case 51: /* Const: TOK_CONST ConstType TOK_IDENTIFIER '=' ConstValue ';'  */
#line 703 "webidl-parser.y"
        {
            struct webidl_node *constant;

            constant = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, NULL, (yyvsp[-3].text));

            /* add constant type */
            constant = webidl_node_prepend(constant, (yyvsp[-4].node));

            /* add constant value */
            constant = webidl_node_prepend(constant, (yyvsp[-1].node));

            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_CONST, NULL, constant);
        }
#line 2473 "webidl-parser.c"
    break;

  case 54: /* ConstValue: TOK_INT_LITERAL  */
#line 725 "webidl-parser.y"
        {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_LITERAL_INT,
                                            NULL,
                                            (yyvsp[0].value));
        }
#line 2483 "webidl-parser.c"
    break;

  case 55: /* ConstValue: TOK_NULL_LITERAL  */
#line 732 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_LITERAL_NULL, NULL, NULL);
        }
#line 2491 "webidl-parser.c"
    break;

  case 56: /* BooleanLiteral: TOK_TRUE  */
#line 740 "webidl-parser.y"
        {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_LITERAL_BOOL,
                                            NULL,
                                            (int)true);
        }
#line 2501 "webidl-parser.c"
    break;

  case 57: /* BooleanLiteral: TOK_FALSE  */
#line 747 "webidl-parser.y"
        {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_LITERAL_BOOL,
                                            NULL,
                                            (int)false);
        }
#line 2511 "webidl-parser.c"
    break;

  case 58: /* FloatLiteral: TOK_FLOAT_LITERAL  */
#line 757 "webidl-parser.y"
        {
                float *value;
                value = malloc(sizeof(float));
                *value = strtof((yyvsp[0].text), NULL);
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_LITERAL_FLOAT,
                                     NULL,
                                     value);
        }
#line 2524 "webidl-parser.c"
    break;

  case 59: /* FloatLiteral: '-' TOK_INFINITY  */
#line 767 "webidl-parser.y"
        {
                float *value;
                value = malloc(sizeof(float));
                *value = -INFINITY;
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_LITERAL_FLOAT,
                                     NULL,
                                     value);
        }
#line 2537 "webidl-parser.c"
    break;

  case 60: /* FloatLiteral: TOK_INFINITY  */
#line 777 "webidl-parser.y"
        {
                float *value;
                value = malloc(sizeof(float));
                *value = INFINITY;
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_LITERAL_FLOAT,
                                     NULL,
                                     value);
        }
#line 2550 "webidl-parser.c"
    break;

  case 61: /* FloatLiteral: TOK_NAN  */
#line 787 "webidl-parser.y"
        {
                float *value;
                value = malloc(sizeof(float));
                *value = NAN;
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_LITERAL_FLOAT,
                                     NULL,
                                     value);
        }
#line 2563 "webidl-parser.c"
    break;

  case 62: /* AttributeOrOperation: TOK_STRINGIFIER StringifierAttributeOrOperation  */
#line 800 "webidl-parser.y"
        {
                (yyval.node) = (yyvsp[0].node);
        }
#line 2571 "webidl-parser.c"
    break;

  case 66: /* StringifierAttributeOrOperation: OperationRest  */
#line 814 "webidl-parser.y"
        {
                /* @todo deal with stringifier */
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_OPERATION, NULL, (yyvsp[0].node));
        }
#line 2580 "webidl-parser.c"
    break;

  case 67: /* StringifierAttributeOrOperation: ';'  */
#line 820 "webidl-parser.y"
        {
                (yyval.node)=NULL;
        }
#line 2588 "webidl-parser.c"
    break;

  case 68: /* Attribute: Inherit ReadOnly AttributeRest  */
#line 830 "webidl-parser.y"
        {
                struct webidl_node *attribute;

                attribute = (yyvsp[0].node);

                /* deal with inherit modifier */
                if ((yyvsp[-2].isit)) {
                        attribute = webidl_new_number_node(
                                          WEBIDL_NODE_TYPE_MODIFIER,
                                          attribute,
                                          WEBIDL_TYPE_MODIFIER_INHERIT);
                }

                /* deal with readonly modifier */
                if ((yyvsp[-1].isit)) {
                        attribute = webidl_new_number_node(
                                         WEBIDL_NODE_TYPE_MODIFIER,
                                         attribute,
                                         WEBIDL_TYPE_MODIFIER_READONLY);
                }

                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_ATTRIBUTE,
                                     NULL,
                                     attribute);
        }
#line 2618 "webidl-parser.c"
    break;

  case 69: /* StaticMember: TOK_STATIC StaticMemberRest  */
#line 861 "webidl-parser.y"
        {
                (yyval.node) = (yyvsp[0].node);
        }
#line 2626 "webidl-parser.c"
    break;

  case 70: /* StaticMemberRest: ReadOnly AttributeRest  */
#line 870 "webidl-parser.y"
        {
                struct webidl_node *attribute;

                attribute = webidl_new_number_node(WEBIDL_NODE_TYPE_MODIFIER,
                                                   (yyvsp[0].node),
                                                   WEBIDL_TYPE_MODIFIER_STATIC);

                /* deal with readonly modifier */
                if ((yyvsp[-1].isit)) {
                        attribute = webidl_new_number_node(
                                         WEBIDL_NODE_TYPE_MODIFIER,
                                         attribute,
                                         WEBIDL_TYPE_MODIFIER_READONLY);
                }

                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_ATTRIBUTE,
                                     NULL,
                                     attribute);
        }
#line 2650 "webidl-parser.c"
    break;

  case 71: /* StaticMemberRest: ReturnType OperationRest  */
#line 891 "webidl-parser.y"
        {
                struct webidl_node *operation;

                /* add static modifier */
                operation = webidl_new_number_node(WEBIDL_NODE_TYPE_MODIFIER,
                                                   (yyvsp[0].node),
                                                   WEBIDL_TYPE_MODIFIER_STATIC);

                /* put return type on the operation */
                operation = webidl_node_prepend((yyvsp[-1].node), operation);

                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_OPERATION,
                                     NULL,
                                     operation);
        }
#line 2670 "webidl-parser.c"
    break;

  case 72: /* AttributeRest: TOK_ATTRIBUTE Type AttributeName ';'  */
#line 912 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, (yyvsp[-2].node), (yyvsp[-1].text));
        }
#line 2678 "webidl-parser.c"
    break;

  case 75: /* AttributeNameKeyword: TOK_REQUIRED  */
#line 927 "webidl-parser.y"
        {
                (yyval.text) = strdup("required");
        }
#line 2686 "webidl-parser.c"
    break;

  case 76: /* Inherit: %empty  */
#line 936 "webidl-parser.y"
        {
                (yyval.isit) = false;
        }
#line 2694 "webidl-parser.c"
    break;

  case 77: /* Inherit: TOK_INHERIT  */
#line 941 "webidl-parser.y"
        {
                (yyval.isit) = true;
        }
#line 2702 "webidl-parser.c"
    break;

  case 78: /* ReadOnly: %empty  */
#line 952 "webidl-parser.y"
        {
                (yyval.isit) = false;
        }
#line 2710 "webidl-parser.c"
    break;

  case 79: /* ReadOnly: TOK_READONLY  */
#line 957 "webidl-parser.y"
        {
                (yyval.isit) = true;
        }
#line 2718 "webidl-parser.c"
    break;

  case 80: /* Operation: ReturnType OperationRest  */
#line 966 "webidl-parser.y"
        {
                /* put return type on the operation */
                (yyvsp[0].node) = webidl_node_prepend((yyvsp[-1].node), (yyvsp[0].node));

                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_OPERATION, NULL, (yyvsp[0].node));
        }
#line 2729 "webidl-parser.c"
    break;

  case 81: /* Operation: SpecialOperation  */
#line 974 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_OPERATION, NULL, (yyvsp[0].node));
        }
#line 2737 "webidl-parser.c"
    break;

  case 82: /* SpecialOperation: Special Specials ReturnType OperationRest  */
#line 982 "webidl-parser.y"
        {
                /* put return type on the operation */
                (yyval.node) = webidl_node_prepend((yyvsp[0].node), (yyvsp[-1].node));

                /* specials */
                (yyval.node) = webidl_node_prepend((yyval.node), (yyvsp[-2].node));

                /* special */
                (yyval.node) = webidl_node_prepend((yyval.node), (yyvsp[-3].node));
        }
#line 2752 "webidl-parser.c"
    break;

  case 83: /* Specials: %empty  */
#line 997 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2760 "webidl-parser.c"
    break;

  case 84: /* Specials: Special Specials  */
#line 1002 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_prepend((yyvsp[0].node), (yyvsp[-1].node));
        }
#line 2768 "webidl-parser.c"
    break;

  case 85: /* Special: TOK_GETTER  */
#line 1010 "webidl-parser.y"
        {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_SPECIAL,
                                            NULL,
                                            WEBIDL_TYPE_SPECIAL_GETTER);
        }
#line 2778 "webidl-parser.c"
    break;

  case 86: /* Special: TOK_SETTER  */
#line 1017 "webidl-parser.y"
        {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_SPECIAL,
                                            NULL,
                                            WEBIDL_TYPE_SPECIAL_SETTER);
        }
#line 2788 "webidl-parser.c"
    break;

  case 87: /* Special: TOK_CREATOR  */
#line 1024 "webidl-parser.y"
        {
                /* second edition removed this special but the
                 * specifications still use it!
                 */
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_SPECIAL,
                                            NULL,
                                            WEBIDL_TYPE_SPECIAL_CREATOR);
        }
#line 2801 "webidl-parser.c"
    break;

  case 88: /* Special: TOK_DELETER  */
#line 1034 "webidl-parser.y"
        {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_SPECIAL,
                                            NULL,
                                            WEBIDL_TYPE_SPECIAL_DELETER);
        }
#line 2811 "webidl-parser.c"
    break;

  case 89: /* Special: TOK_LEGACYCALLER  */
#line 1041 "webidl-parser.y"
        {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_SPECIAL,
                                            NULL,
                                            WEBIDL_TYPE_SPECIAL_LEGACYCALLER);
        }
#line 2821 "webidl-parser.c"
    break;

  case 90: /* OperationRest: OptionalIdentifier '(' ArgumentList ')' ';'  */
#line 1051 "webidl-parser.y"
        {
                /* argument list */
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_LIST, NULL, (yyvsp[-2].node));

                (yyval.node) = webidl_node_prepend((yyvsp[-4].node), (yyval.node)); /* identifier */
        }
#line 2832 "webidl-parser.c"
    break;

  case 91: /* OptionalIdentifier: %empty  */
#line 1062 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2840 "webidl-parser.c"
    break;

  case 92: /* OptionalIdentifier: TOK_IDENTIFIER  */
#line 1067 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, NULL, (yyvsp[0].text));
        }
#line 2848 "webidl-parser.c"
    break;

  case 93: /* ArgumentList: %empty  */
#line 1078 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2856 "webidl-parser.c"
    break;

  case 94: /* ArgumentList: Argument Arguments  */
#line 1083 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_append((yyvsp[0].node), (yyvsp[-1].node));
        }
#line 2864 "webidl-parser.c"
    break;

  case 95: /* Arguments: %empty  */
#line 1091 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2872 "webidl-parser.c"
    break;

  case 96: /* Arguments: ',' Argument Arguments  */
#line 1096 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_append((yyvsp[0].node), (yyvsp[-1].node));
        }
#line 2880 "webidl-parser.c"
    break;

  case 97: /* Argument: ExtendedAttributeList OptionalOrRequiredArgument  */
#line 1105 "webidl-parser.y"
        {
                (yyval.node) = (yyvsp[0].node);
        }
#line 2888 "webidl-parser.c"
    break;

  case 98: /* OptionalOrRequiredArgument: TOK_OPTIONAL Type ArgumentName Default  */
#line 1113 "webidl-parser.y"
        {
            struct webidl_node *argument;
            argument = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, NULL, (yyvsp[-1].text));
            argument = webidl_node_new(WEBIDL_NODE_TYPE_OPTIONAL, argument, (yyvsp[0].node));
            argument = webidl_node_prepend(argument, (yyvsp[-2].node)); /* add type node */
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_ARGUMENT, NULL, argument);
        }
#line 2900 "webidl-parser.c"
    break;

  case 99: /* OptionalOrRequiredArgument: Type Ellipsis ArgumentName  */
#line 1122 "webidl-parser.y"
        {
            struct webidl_node *argument;
            argument = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, NULL, (yyvsp[0].text));
            argument = webidl_node_prepend(argument, (yyvsp[-1].node)); /* ellipsis node */
            argument = webidl_node_prepend(argument, (yyvsp[-2].node)); /* add type node */
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_ARGUMENT, NULL, argument);
        }
#line 2912 "webidl-parser.c"
    break;

  case 102: /* Ellipsis: %empty  */
#line 1141 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2920 "webidl-parser.c"
    break;

  case 103: /* Ellipsis: TOK_ELLIPSIS  */
#line 1146 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_ELLIPSIS, NULL, NULL);
        }
#line 2928 "webidl-parser.c"
    break;

  case 104: /* Iterable: TOK_ITERABLE '<' Type OptionalType '>' ';'  */
#line 1154 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2936 "webidl-parser.c"
    break;

  case 105: /* Iterable: TOK_LEGACYITERABLE '<' Type '>' ';'  */
#line 1159 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2944 "webidl-parser.c"
    break;

  case 106: /* OptionalType: %empty  */
#line 1167 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2952 "webidl-parser.c"
    break;

  case 107: /* OptionalType: ',' Type  */
#line 1172 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2960 "webidl-parser.c"
    break;

  case 111: /* ExtendedAttributeList: %empty  */
#line 1192 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2968 "webidl-parser.c"
    break;

  case 112: /* ExtendedAttributeList: '[' ExtendedAttribute ExtendedAttributes ']'  */
#line 1197 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE,
                                     (yyvsp[-1].node),
                                     (yyvsp[-2].node));
        }
#line 2978 "webidl-parser.c"
    break;

  case 113: /* ExtendedAttributes: %empty  */
#line 1207 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 2986 "webidl-parser.c"
    break;

  case 114: /* ExtendedAttributes: ',' ExtendedAttribute ExtendedAttributes  */
#line 1212 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE,
                                     (yyvsp[0].node),
                                     (yyvsp[-1].node));
        }
#line 2996 "webidl-parser.c"
    break;

  case 115: /* ExtendedAttribute: '(' ExtendedAttributeInner ')' ExtendedAttributeRest  */
#line 1224 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE, (yyvsp[0].node), (yyvsp[-2].node));
        }
#line 3004 "webidl-parser.c"
    break;

  case 116: /* ExtendedAttribute: '[' ExtendedAttributeInner ']' ExtendedAttributeRest  */
#line 1229 "webidl-parser.y"
        {
            /* @todo should be a WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE_SQUARE */
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE, (yyvsp[0].node), (yyvsp[-2].node));
        }
#line 3013 "webidl-parser.c"
    break;

  case 117: /* ExtendedAttribute: '{' ExtendedAttributeInner '}' ExtendedAttributeRest  */
#line 1235 "webidl-parser.y"
        {
            /* @todo should be a WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE_CURLY */
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE, (yyvsp[0].node), (yyvsp[-2].node));
        }
#line 3022 "webidl-parser.c"
    break;

  case 118: /* ExtendedAttribute: TOK_CONSTRUCTOR  */
#line 1241 "webidl-parser.y"
        {
                /* Constructor */
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                     NULL,
                                     strdup("Constructor"));
        }
#line 3033 "webidl-parser.c"
    break;

  case 119: /* ExtendedAttribute: TOK_CONSTRUCTOR '(' ArgumentList ')'  */
#line 1249 "webidl-parser.y"
        {
                /* Constructor */
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_OPERATION,
                                     webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                     NULL,
                                                     strdup("Constructor")),
                                     webidl_node_new(WEBIDL_NODE_TYPE_LIST,
                                                     NULL,
                                                     (yyvsp[-1].node)));
        }
#line 3048 "webidl-parser.c"
    break;

  case 120: /* ExtendedAttribute: TOK_NAMEDCONSTRUCTOR '=' TOK_IDENTIFIER '(' ArgumentList ')'  */
#line 1261 "webidl-parser.y"
        {
                /* Constructor */
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_OPERATION,
                                     webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                     NULL,
                                                     strdup("NamedConstructor")),
                                     webidl_node_new(WEBIDL_NODE_TYPE_LIST,
                                                     webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                                     NULL,
                                                                     (yyvsp[-3].text)),
                                                     (yyvsp[-1].node)));
        }
#line 3065 "webidl-parser.c"
    break;

  case 121: /* ExtendedAttribute: Other ExtendedAttributeRest  */
#line 1275 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_append((yyvsp[0].node),
                                        webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                        NULL,
                                                        (yyvsp[-1].text)));
        }
#line 3076 "webidl-parser.c"
    break;

  case 122: /* ExtendedAttributeRest: %empty  */
#line 1286 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 3084 "webidl-parser.c"
    break;

  case 123: /* ExtendedAttributeRest: ExtendedAttribute  */
#line 1291 "webidl-parser.y"
        {
                (yyval.node) = (yyvsp[0].node);
        }
#line 3092 "webidl-parser.c"
    break;

  case 124: /* ExtendedAttributeInner: %empty  */
#line 1299 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 3100 "webidl-parser.c"
    break;

  case 125: /* ExtendedAttributeInner: '(' ExtendedAttributeInner ')' ExtendedAttributeInner  */
#line 1304 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_prepend(
                        webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE,
                                        NULL,
                                        (yyvsp[-2].node)),
                        (yyvsp[0].node));
        }
#line 3112 "webidl-parser.c"
    break;

  case 126: /* ExtendedAttributeInner: '[' ExtendedAttributeInner ']' ExtendedAttributeInner  */
#line 1313 "webidl-parser.y"
        {
            /* @todo should be a WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE_SQUARE */
            (yyval.node) = webidl_node_prepend(
                     webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE,
                                     NULL,
                                     (yyvsp[-2].node)),
                     (yyvsp[0].node));
        }
#line 3125 "webidl-parser.c"
    break;

  case 127: /* ExtendedAttributeInner: '{' ExtendedAttributeInner '}' ExtendedAttributeInner  */
#line 1323 "webidl-parser.y"
        {
            /* @todo should be a WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE_CURLY */
            (yyval.node) = webidl_node_prepend(
                     webidl_node_new(WEBIDL_NODE_TYPE_EXTENDED_ATTRIBUTE,
                                     NULL,
                                     (yyvsp[-2].node)),
                     (yyvsp[0].node));
        }
#line 3138 "webidl-parser.c"
    break;

  case 128: /* ExtendedAttributeInner: OtherOrComma ExtendedAttributeInner  */
#line 1333 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_append((yyvsp[0].node),
                                        webidl_node_new(WEBIDL_NODE_TYPE_IDENT,
                                                        NULL,
                                                        (yyvsp[-1].text)));
        }
#line 3149 "webidl-parser.c"
    break;

  case 129: /* Other: TOK_INT_LITERAL  */
#line 1344 "webidl-parser.y"
        {
                /* @todo loosing base info here might break the attribute */
                (yyval.text) = calloc(1, 32);
                snprintf((yyval.text), 32, "%ld", (yyvsp[0].value));
        }
#line 3159 "webidl-parser.c"
    break;

  case 130: /* Other: TOK_FLOAT_LITERAL  */
#line 1351 "webidl-parser.y"
        {
                (yyval.text) = (yyvsp[0].text);
        }
#line 3167 "webidl-parser.c"
    break;

  case 131: /* Other: TOK_IDENTIFIER  */
#line 1356 "webidl-parser.y"
        {
                (yyval.text) = (yyvsp[0].text);
        }
#line 3175 "webidl-parser.c"
    break;

  case 132: /* Other: TOK_STRING_LITERAL  */
#line 1361 "webidl-parser.y"
        {
                (yyval.text) = (yyvsp[0].text);
        }
#line 3183 "webidl-parser.c"
    break;

  case 133: /* Other: TOK_OTHER_LITERAL  */
#line 1366 "webidl-parser.y"
        {
                (yyval.text) = (yyvsp[0].text);
        }
#line 3191 "webidl-parser.c"
    break;

  case 134: /* Other: '-'  */
#line 1371 "webidl-parser.y"
        {
                (yyval.text) = strdup("-");
        }
#line 3199 "webidl-parser.c"
    break;

  case 135: /* Other: '.'  */
#line 1376 "webidl-parser.y"
        {
                (yyval.text) = strdup(".");
        }
#line 3207 "webidl-parser.c"
    break;

  case 136: /* Other: TOK_ELLIPSIS  */
#line 1381 "webidl-parser.y"
        {
                (yyval.text) = strdup("...");
        }
#line 3215 "webidl-parser.c"
    break;

  case 137: /* Other: ':'  */
#line 1386 "webidl-parser.y"
        {
                (yyval.text) = strdup(":");
        }
#line 3223 "webidl-parser.c"
    break;

  case 138: /* Other: ';'  */
#line 1391 "webidl-parser.y"
        {
                (yyval.text) = strdup(";");
        }
#line 3231 "webidl-parser.c"
    break;

  case 139: /* Other: '<'  */
#line 1396 "webidl-parser.y"
        {
                (yyval.text) = strdup("<");
        }
#line 3239 "webidl-parser.c"
    break;

  case 140: /* Other: '='  */
#line 1401 "webidl-parser.y"
        {
                (yyval.text) = strdup("=");
        }
#line 3247 "webidl-parser.c"
    break;

  case 141: /* Other: '>'  */
#line 1406 "webidl-parser.y"
        {
                (yyval.text) = strdup(">");
        }
#line 3255 "webidl-parser.c"
    break;

  case 142: /* Other: '?'  */
#line 1411 "webidl-parser.y"
        {
                (yyval.text) = strdup("?");
        }
#line 3263 "webidl-parser.c"
    break;

  case 143: /* Other: TOK_DATE  */
#line 1416 "webidl-parser.y"
        {
                (yyval.text) = strdup("Date");
        }
#line 3271 "webidl-parser.c"
    break;

  case 144: /* Other: TOK_STRING  */
#line 1421 "webidl-parser.y"
        {
                (yyval.text) = strdup("DOMString");
        }
#line 3279 "webidl-parser.c"
    break;

  case 145: /* Other: TOK_INFINITY  */
#line 1426 "webidl-parser.y"
        {
                (yyval.text) = strdup("Infinity");
        }
#line 3287 "webidl-parser.c"
    break;

  case 146: /* Other: TOK_NAN  */
#line 1431 "webidl-parser.y"
        {
                (yyval.text) = strdup("NaN");
        }
#line 3295 "webidl-parser.c"
    break;

  case 147: /* Other: TOK_ANY  */
#line 1436 "webidl-parser.y"
        {
                (yyval.text) = strdup("any");
        }
#line 3303 "webidl-parser.c"
    break;

  case 148: /* Other: TOK_BOOLEAN  */
#line 1441 "webidl-parser.y"
        {
                (yyval.text) = strdup("boolean");
        }
#line 3311 "webidl-parser.c"
    break;

  case 149: /* Other: TOK_BYTE  */
#line 1446 "webidl-parser.y"
        {
                (yyval.text) = strdup("byte");
        }
#line 3319 "webidl-parser.c"
    break;

  case 150: /* Other: TOK_DOUBLE  */
#line 1451 "webidl-parser.y"
        {
                (yyval.text) = strdup("double");
        }
#line 3327 "webidl-parser.c"
    break;

  case 151: /* Other: TOK_FALSE  */
#line 1456 "webidl-parser.y"
        {
                (yyval.text) = strdup("false");
        }
#line 3335 "webidl-parser.c"
    break;

  case 152: /* Other: TOK_FLOAT  */
#line 1461 "webidl-parser.y"
        {
            (yyval.text) = strdup("float");
        }
#line 3343 "webidl-parser.c"
    break;

  case 153: /* Other: TOK_LONG  */
#line 1466 "webidl-parser.y"
        {
            (yyval.text) = strdup("long");
        }
#line 3351 "webidl-parser.c"
    break;

  case 154: /* Other: TOK_NULL_LITERAL  */
#line 1471 "webidl-parser.y"
        {
            (yyval.text) = strdup("null");
        }
#line 3359 "webidl-parser.c"
    break;

  case 155: /* Other: TOK_OBJECT  */
#line 1476 "webidl-parser.y"
        {
            (yyval.text) = strdup("object");
        }
#line 3367 "webidl-parser.c"
    break;

  case 156: /* Other: TOK_OCTET  */
#line 1481 "webidl-parser.y"
        {
            (yyval.text) = strdup("octet");
        }
#line 3375 "webidl-parser.c"
    break;

  case 157: /* Other: TOK_OR  */
#line 1486 "webidl-parser.y"
        {
            (yyval.text) = strdup("or");
        }
#line 3383 "webidl-parser.c"
    break;

  case 158: /* Other: TOK_OPTIONAL  */
#line 1491 "webidl-parser.y"
        {
            (yyval.text) = strdup("optional");
        }
#line 3391 "webidl-parser.c"
    break;

  case 159: /* Other: TOK_SEQUENCE  */
#line 1496 "webidl-parser.y"
        {
            (yyval.text) = strdup("sequence");
        }
#line 3399 "webidl-parser.c"
    break;

  case 160: /* Other: TOK_SHORT  */
#line 1501 "webidl-parser.y"
        {
            (yyval.text) = strdup("short");
        }
#line 3407 "webidl-parser.c"
    break;

  case 161: /* Other: TOK_TRUE  */
#line 1506 "webidl-parser.y"
        {
            (yyval.text) = strdup("true");
        }
#line 3415 "webidl-parser.c"
    break;

  case 162: /* Other: TOK_UNSIGNED  */
#line 1511 "webidl-parser.y"
        {
            (yyval.text) = strdup("unsigned");
        }
#line 3423 "webidl-parser.c"
    break;

  case 163: /* Other: TOK_VOID  */
#line 1516 "webidl-parser.y"
        {
            (yyval.text) = strdup("void");
        }
#line 3431 "webidl-parser.c"
    break;

  case 164: /* Other: ArgumentNameKeyword  */
#line 1521 "webidl-parser.y"
        {
            (yyval.text) = (yyvsp[0].text);
        }
#line 3439 "webidl-parser.c"
    break;

  case 165: /* ArgumentNameKeyword: TOK_ATTRIBUTE  */
#line 1531 "webidl-parser.y"
        {
            (yyval.text) = strdup("attribute");
        }
#line 3447 "webidl-parser.c"
    break;

  case 166: /* ArgumentNameKeyword: TOK_CALLBACK  */
#line 1536 "webidl-parser.y"
        {
            (yyval.text) = strdup("callback");
        }
#line 3455 "webidl-parser.c"
    break;

  case 167: /* ArgumentNameKeyword: TOK_CONST  */
#line 1541 "webidl-parser.y"
        {
            (yyval.text) = strdup("const");
        }
#line 3463 "webidl-parser.c"
    break;

  case 168: /* ArgumentNameKeyword: TOK_CREATOR  */
#line 1546 "webidl-parser.y"
        {
            (yyval.text) = strdup("creator");
        }
#line 3471 "webidl-parser.c"
    break;

  case 169: /* ArgumentNameKeyword: TOK_DELETER  */
#line 1551 "webidl-parser.y"
        {
            (yyval.text) = strdup("deleter");
        }
#line 3479 "webidl-parser.c"
    break;

  case 170: /* ArgumentNameKeyword: TOK_DICTIONARY  */
#line 1556 "webidl-parser.y"
        {
            (yyval.text) = strdup("dictionary");
        }
#line 3487 "webidl-parser.c"
    break;

  case 171: /* ArgumentNameKeyword: TOK_ENUM  */
#line 1561 "webidl-parser.y"
        {
            (yyval.text) = strdup("enum");
        }
#line 3495 "webidl-parser.c"
    break;

  case 172: /* ArgumentNameKeyword: TOK_EXCEPTION  */
#line 1566 "webidl-parser.y"
        {
            (yyval.text) = strdup("exception");
        }
#line 3503 "webidl-parser.c"
    break;

  case 173: /* ArgumentNameKeyword: TOK_GETTER  */
#line 1571 "webidl-parser.y"
        {
            (yyval.text) = strdup("getter");
        }
#line 3511 "webidl-parser.c"
    break;

  case 174: /* ArgumentNameKeyword: TOK_IMPLEMENTS  */
#line 1576 "webidl-parser.y"
        {
            (yyval.text) = strdup("implements");
        }
#line 3519 "webidl-parser.c"
    break;

  case 175: /* ArgumentNameKeyword: TOK_INHERIT  */
#line 1581 "webidl-parser.y"
        {
            (yyval.text) = strdup("inherit");
        }
#line 3527 "webidl-parser.c"
    break;

  case 176: /* ArgumentNameKeyword: TOK_INTERFACE  */
#line 1586 "webidl-parser.y"
        {
            (yyval.text) = strdup("interface");
        }
#line 3535 "webidl-parser.c"
    break;

  case 177: /* ArgumentNameKeyword: TOK_ITERABLE  */
#line 1591 "webidl-parser.y"
        {
            (yyval.text) = strdup("iterable");
        }
#line 3543 "webidl-parser.c"
    break;

  case 178: /* ArgumentNameKeyword: TOK_LEGACYCALLER  */
#line 1596 "webidl-parser.y"
        {
            (yyval.text) = strdup("legacycaller");
        }
#line 3551 "webidl-parser.c"
    break;

  case 179: /* ArgumentNameKeyword: TOK_LEGACYITERABLE  */
#line 1601 "webidl-parser.y"
        {
            (yyval.text) = strdup("legacyiterable");
        }
#line 3559 "webidl-parser.c"
    break;

  case 180: /* ArgumentNameKeyword: TOK_PARTIAL  */
#line 1606 "webidl-parser.y"
        {
            (yyval.text) = strdup("partial");
        }
#line 3567 "webidl-parser.c"
    break;

  case 181: /* ArgumentNameKeyword: TOK_REQUIRED  */
#line 1611 "webidl-parser.y"
        {
            (yyval.text) = strdup("required");
        }
#line 3575 "webidl-parser.c"
    break;

  case 182: /* ArgumentNameKeyword: TOK_SETTER  */
#line 1616 "webidl-parser.y"
        {
            (yyval.text) = strdup("setter");
        }
#line 3583 "webidl-parser.c"
    break;

  case 183: /* ArgumentNameKeyword: TOK_STATIC  */
#line 1621 "webidl-parser.y"
        {
            (yyval.text) = strdup("static");
        }
#line 3591 "webidl-parser.c"
    break;

  case 184: /* ArgumentNameKeyword: TOK_STRINGIFIER  */
#line 1626 "webidl-parser.y"
        {
            (yyval.text) = strdup("stringifier");
        }
#line 3599 "webidl-parser.c"
    break;

  case 185: /* ArgumentNameKeyword: TOK_TYPEDEF  */
#line 1631 "webidl-parser.y"
        {
            (yyval.text) = strdup("typedef");
        }
#line 3607 "webidl-parser.c"
    break;

  case 186: /* ArgumentNameKeyword: TOK_UNRESTRICTED  */
#line 1636 "webidl-parser.y"
        {
            (yyval.text) = strdup("unrestricted");
        }
#line 3615 "webidl-parser.c"
    break;

  case 187: /* OtherOrComma: Other  */
#line 1644 "webidl-parser.y"
        {
            (yyval.text) = (yyvsp[0].text);
        }
#line 3623 "webidl-parser.c"
    break;

  case 188: /* OtherOrComma: ','  */
#line 1649 "webidl-parser.y"
        {
            (yyval.text) = strdup(",");
        }
#line 3631 "webidl-parser.c"
    break;

  case 189: /* Type: SingleType  */
#line 1657 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE, NULL, (yyvsp[0].node));
        }
#line 3639 "webidl-parser.c"
    break;

  case 190: /* Type: UnionType TypeSuffix  */
#line 1662 "webidl-parser.y"
        {
            /* todo handle suffix */
            (yyval.node) = (yyvsp[-1].node);
        }
#line 3648 "webidl-parser.c"
    break;

  case 192: /* SingleType: TOK_ANY TypeSuffixStartingWithArray  */
#line 1673 "webidl-parser.y"
        {
                /* todo deal with TypeSuffixStartingWithArray */
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                            NULL,
                                            WEBIDL_TYPE_ANY);
        }
#line 3659 "webidl-parser.c"
    break;

  case 193: /* UnionType: '(' UnionMemberType TOK_OR UnionMemberType UnionMemberTypes ')'  */
#line 1684 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_prepend((yyvsp[-4].node), webidl_node_prepend((yyvsp[-2].node), (yyvsp[-1].node)));
        }
#line 3667 "webidl-parser.c"
    break;

  case 194: /* UnionMemberType: NonAnyType  */
#line 1692 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE, NULL, (yyvsp[0].node));
        }
#line 3675 "webidl-parser.c"
    break;

  case 195: /* UnionMemberType: UnionType TypeSuffix  */
#line 1697 "webidl-parser.y"
        {
            /* todo handle suffix */
            (yyval.node) = (yyvsp[-1].node);
        }
#line 3684 "webidl-parser.c"
    break;

  case 196: /* UnionMemberType: TOK_ANY '[' ']' TypeSuffix  */
#line 1703 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 3692 "webidl-parser.c"
    break;

  case 197: /* UnionMemberTypes: %empty  */
#line 1711 "webidl-parser.y"
        {
                (yyval.node) = NULL;
        }
#line 3700 "webidl-parser.c"
    break;

  case 198: /* UnionMemberTypes: TOK_OR UnionMemberType UnionMemberTypes  */
#line 1716 "webidl-parser.y"
        {
                (yyval.node) = webidl_node_prepend((yyvsp[-1].node), (yyvsp[0].node));
        }
#line 3708 "webidl-parser.c"
    break;

  case 199: /* NonAnyType: PrimitiveType TypeSuffix  */
#line 1727 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_prepend((yyvsp[-1].node), (yyvsp[0].node));
        }
#line 3716 "webidl-parser.c"
    break;

  case 200: /* NonAnyType: PromiseType TypeSuffix  */
#line 1732 "webidl-parser.y"
        {
            /* second edition adds promise types */
            (yyval.node) = webidl_node_prepend((yyvsp[-1].node), (yyvsp[0].node));
        }
#line 3725 "webidl-parser.c"
    break;

  case 201: /* NonAnyType: TOK_STRING TypeSuffix  */
#line 1738 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        (yyvsp[0].node),
                                        WEBIDL_TYPE_STRING);
        }
#line 3735 "webidl-parser.c"
    break;

  case 202: /* NonAnyType: TOK_IDENTIFIER TypeSuffix  */
#line 1745 "webidl-parser.y"
        {
            struct webidl_node *type;
            type = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                          (yyvsp[0].node),
                                          WEBIDL_TYPE_USER);
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, type, (yyvsp[-1].text));
        }
#line 3747 "webidl-parser.c"
    break;

  case 203: /* NonAnyType: TOK_SEQUENCE '<' Type '>' Null  */
#line 1754 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        (yyvsp[-2].node),
                                        WEBIDL_TYPE_SEQUENCE);
        }
#line 3757 "webidl-parser.c"
    break;

  case 204: /* NonAnyType: TOK_OBJECT TypeSuffix  */
#line 1761 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        (yyvsp[0].node),
                                        WEBIDL_TYPE_OBJECT);
        }
#line 3767 "webidl-parser.c"
    break;

  case 205: /* NonAnyType: TOK_DATE TypeSuffix  */
#line 1768 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        (yyvsp[0].node),
                                        WEBIDL_TYPE_DATE);
        }
#line 3777 "webidl-parser.c"
    break;

  case 206: /* ConstType: PrimitiveType Null  */
#line 1778 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE, NULL, (yyvsp[-1].node));
        }
#line 3785 "webidl-parser.c"
    break;

  case 207: /* ConstType: TOK_IDENTIFIER Null  */
#line 1783 "webidl-parser.y"
        {
            struct webidl_node *type;
            type = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                          NULL,
                                          WEBIDL_TYPE_USER);
            type = webidl_node_new(WEBIDL_NODE_TYPE_IDENT, type, (yyvsp[-1].text));
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE, NULL, type);
        }
#line 3798 "webidl-parser.c"
    break;

  case 210: /* PrimitiveType: TOK_BOOLEAN  */
#line 1801 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        NULL,
                                        WEBIDL_TYPE_BOOL);
        }
#line 3808 "webidl-parser.c"
    break;

  case 211: /* PrimitiveType: TOK_BYTE  */
#line 1808 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        NULL,
                                        WEBIDL_TYPE_BYTE);
        }
#line 3818 "webidl-parser.c"
    break;

  case 212: /* PrimitiveType: TOK_OCTET  */
#line 1815 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        NULL,
                                        WEBIDL_TYPE_OCTET);
        }
#line 3828 "webidl-parser.c"
    break;

  case 213: /* UnrestrictedFloatType: TOK_UNRESTRICTED FloatType  */
#line 1825 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_MODIFIER,
                                        (yyvsp[0].node),
                                        WEBIDL_TYPE_MODIFIER_UNRESTRICTED);
        }
#line 3838 "webidl-parser.c"
    break;

  case 215: /* FloatType: TOK_FLOAT  */
#line 1837 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        NULL,
                                        WEBIDL_TYPE_FLOAT);
        }
#line 3848 "webidl-parser.c"
    break;

  case 216: /* FloatType: TOK_DOUBLE  */
#line 1844 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        NULL,
                                        WEBIDL_TYPE_DOUBLE);
        }
#line 3858 "webidl-parser.c"
    break;

  case 217: /* UnsignedIntegerType: TOK_UNSIGNED IntegerType  */
#line 1854 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_MODIFIER,
                                        (yyvsp[0].node),
                                        WEBIDL_TYPE_MODIFIER_UNSIGNED);
        }
#line 3868 "webidl-parser.c"
    break;

  case 219: /* IntegerType: TOK_SHORT  */
#line 1866 "webidl-parser.y"
        {
            (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                        NULL,
                                        WEBIDL_TYPE_SHORT);
        }
#line 3878 "webidl-parser.c"
    break;

  case 220: /* IntegerType: TOK_LONG OptionalLong  */
#line 1873 "webidl-parser.y"
        {
            if ((yyvsp[0].isit)) {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                            NULL,
                                            WEBIDL_TYPE_LONGLONG);
            } else {
                (yyval.node) = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                            NULL,
                                            WEBIDL_TYPE_LONG);
            }
        }
#line 3894 "webidl-parser.c"
    break;

  case 221: /* OptionalLong: %empty  */
#line 1889 "webidl-parser.y"
        {
            (yyval.isit) = false;
        }
#line 3902 "webidl-parser.c"
    break;

  case 222: /* OptionalLong: TOK_LONG  */
#line 1894 "webidl-parser.y"
        {
            (yyval.isit) = true;
        }
#line 3910 "webidl-parser.c"
    break;

  case 223: /* PromiseType: TOK_PROMISE '<' ReturnType '>'  */
#line 1902 "webidl-parser.y"
        {
            (yyval.node) = NULL;
        }
#line 3918 "webidl-parser.c"
    break;

  case 224: /* TypeSuffix: %empty  */
#line 1910 "webidl-parser.y"
        {
            (yyval.node) = NULL;
        }
#line 3926 "webidl-parser.c"
    break;

  case 225: /* TypeSuffix: '[' ']' TypeSuffix  */
#line 1915 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE_ARRAY, (yyvsp[0].node), NULL);
        }
#line 3934 "webidl-parser.c"
    break;

  case 226: /* TypeSuffix: '?' TypeSuffixStartingWithArray  */
#line 1920 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE_NULLABLE, (yyvsp[0].node), NULL);
        }
#line 3942 "webidl-parser.c"
    break;

  case 227: /* TypeSuffixStartingWithArray: %empty  */
#line 1928 "webidl-parser.y"
        {
            (yyval.node) = NULL;
        }
#line 3950 "webidl-parser.c"
    break;

  case 228: /* TypeSuffixStartingWithArray: '[' ']' TypeSuffix  */
#line 1933 "webidl-parser.y"
        {
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE_ARRAY, (yyvsp[0].node), NULL);
        }
#line 3958 "webidl-parser.c"
    break;

  case 232: /* ReturnType: TOK_VOID  */
#line 1950 "webidl-parser.y"
        {
            struct webidl_node *type;
            type = webidl_new_number_node(WEBIDL_NODE_TYPE_TYPE_BASE,
                                          NULL,
                                          WEBIDL_TYPE_VOID);
            (yyval.node) = webidl_node_new(WEBIDL_NODE_TYPE_TYPE, NULL, type);
        }
#line 3970 "webidl-parser.c"
    break;


#line 3974 "webidl-parser.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == WEBIDL_EMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (&yylloc, webidl_ast, yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= WEBIDL_EOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == WEBIDL_EOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc, webidl_ast);
          yychar = WEBIDL_EMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp, webidl_ast);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, webidl_ast, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != WEBIDL_EMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc, webidl_ast);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp, webidl_ast);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 1960 "webidl-parser.y"

