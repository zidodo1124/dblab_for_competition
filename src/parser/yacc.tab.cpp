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
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"

#include "ast.h"
#include "yacc.tab.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <string>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

static std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static bool parse_compatible_isolation_identifier(const std::string &level, bool *serializable) {
    std::string normalized = lower_copy(level);
    if (normalized == "read_committed" || normalized == "read_uncommitted" ||
        normalized == "repeatable_read") {
        *serializable = false;
        return true;
    }
    return false;
}

static bool parse_compatible_isolation_pair(
    const std::string &first, const std::string &second, bool *serializable) {
    std::string left = lower_copy(first);
    std::string right = lower_copy(second);
    if (left == "read" && (right == "committed" || right == "uncommitted")) {
        *serializable = false;
        return true;
    }
    if (left == "repeatable" && right == "read") {
        *serializable = false;
        return true;
    }
    return false;
}

static std::shared_ptr<Value> negate_numeric_value(std::shared_ptr<Value> value) {
    if (auto int_lit = std::dynamic_pointer_cast<IntLit>(value)) {
        return std::make_shared<IntLit>(-int_lit->val);
    }
    if (auto float_lit = std::dynamic_pointer_cast<FloatLit>(value)) {
        return std::make_shared<FloatLit>(-float_lit->val);
    }
    return value;
}

#line 131 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"

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

#include "yacc.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_SHOW = 3,                       /* SHOW  */
  YYSYMBOL_TABLES = 4,                     /* TABLES  */
  YYSYMBOL_CREATE = 5,                     /* CREATE  */
  YYSYMBOL_TABLE = 6,                      /* TABLE  */
  YYSYMBOL_DROP = 7,                       /* DROP  */
  YYSYMBOL_DESC = 8,                       /* DESC  */
  YYSYMBOL_INSERT = 9,                     /* INSERT  */
  YYSYMBOL_INTO = 10,                      /* INTO  */
  YYSYMBOL_VALUES = 11,                    /* VALUES  */
  YYSYMBOL_DELETE = 12,                    /* DELETE  */
  YYSYMBOL_FROM = 13,                      /* FROM  */
  YYSYMBOL_ASC = 14,                       /* ASC  */
  YYSYMBOL_ORDER = 15,                     /* ORDER  */
  YYSYMBOL_BY = 16,                        /* BY  */
  YYSYMBOL_WHERE = 17,                     /* WHERE  */
  YYSYMBOL_UPDATE = 18,                    /* UPDATE  */
  YYSYMBOL_SET = 19,                       /* SET  */
  YYSYMBOL_SELECT = 20,                    /* SELECT  */
  YYSYMBOL_INT = 21,                       /* INT  */
  YYSYMBOL_CHAR = 22,                      /* CHAR  */
  YYSYMBOL_FLOAT = 23,                     /* FLOAT  */
  YYSYMBOL_INDEX = 24,                     /* INDEX  */
  YYSYMBOL_AND = 25,                       /* AND  */
  YYSYMBOL_JOIN = 26,                      /* JOIN  */
  YYSYMBOL_EXIT = 27,                      /* EXIT  */
  YYSYMBOL_HELP = 28,                      /* HELP  */
  YYSYMBOL_TXN_BEGIN = 29,                 /* TXN_BEGIN  */
  YYSYMBOL_TXN_COMMIT = 30,                /* TXN_COMMIT  */
  YYSYMBOL_TXN_ABORT = 31,                 /* TXN_ABORT  */
  YYSYMBOL_TXN_ROLLBACK = 32,              /* TXN_ROLLBACK  */
  YYSYMBOL_ORDER_BY = 33,                  /* ORDER_BY  */
  YYSYMBOL_ENABLE_NESTLOOP = 34,           /* ENABLE_NESTLOOP  */
  YYSYMBOL_ENABLE_SORTMERGE = 35,          /* ENABLE_SORTMERGE  */
  YYSYMBOL_STATIC_CHECKPOINT = 36,         /* STATIC_CHECKPOINT  */
  YYSYMBOL_TRANSACTION = 37,               /* TRANSACTION  */
  YYSYMBOL_ISOLATION = 38,                 /* ISOLATION  */
  YYSYMBOL_LEVEL = 39,                     /* LEVEL  */
  YYSYMBOL_SNAPSHOT = 40,                  /* SNAPSHOT  */
  YYSYMBOL_SNAPSHOT_ISOLATION = 41,        /* SNAPSHOT_ISOLATION  */
  YYSYMBOL_SERIALIZABLE = 42,              /* SERIALIZABLE  */
  YYSYMBOL_SESSION = 43,                   /* SESSION  */
  YYSYMBOL_CHARACTERISTICS = 44,           /* CHARACTERISTICS  */
  YYSYMBOL_GROUP = 45,                     /* GROUP  */
  YYSYMBOL_HAVING = 46,                    /* HAVING  */
  YYSYMBOL_AS = 47,                        /* AS  */
  YYSYMBOL_COUNT = 48,                     /* COUNT  */
  YYSYMBOL_MAX = 49,                       /* MAX  */
  YYSYMBOL_MIN = 50,                       /* MIN  */
  YYSYMBOL_SUM = 51,                       /* SUM  */
  YYSYMBOL_AVG = 52,                       /* AVG  */
  YYSYMBOL_LIMIT = 53,                     /* LIMIT  */
  YYSYMBOL_UNION = 54,                     /* UNION  */
  YYSYMBOL_LEQ = 55,                       /* LEQ  */
  YYSYMBOL_NEQ = 56,                       /* NEQ  */
  YYSYMBOL_GEQ = 57,                       /* GEQ  */
  YYSYMBOL_T_EOF = 58,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 59,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 60,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 61,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 62,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 63,                /* VALUE_BOOL  */
  YYSYMBOL_64_ = 64,                       /* ';'  */
  YYSYMBOL_65_ = 65,                       /* '='  */
  YYSYMBOL_66_ = 66,                       /* '('  */
  YYSYMBOL_67_ = 67,                       /* ')'  */
  YYSYMBOL_68_ = 68,                       /* ','  */
  YYSYMBOL_69_ = 69,                       /* '.'  */
  YYSYMBOL_70_ = 70,                       /* '<'  */
  YYSYMBOL_71_ = 71,                       /* '>'  */
  YYSYMBOL_72_ = 72,                       /* '+'  */
  YYSYMBOL_73_ = 73,                       /* '-'  */
  YYSYMBOL_74_ = 74,                       /* '*'  */
  YYSYMBOL_YYACCEPT = 75,                  /* $accept  */
  YYSYMBOL_start = 76,                     /* start  */
  YYSYMBOL_stmt = 77,                      /* stmt  */
  YYSYMBOL_txnStmt = 78,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 79,                    /* dbStmt  */
  YYSYMBOL_setStmt = 80,                   /* setStmt  */
  YYSYMBOL_isolationLevel = 81,            /* isolationLevel  */
  YYSYMBOL_ddl = 82,                       /* ddl  */
  YYSYMBOL_dml = 83,                       /* dml  */
  YYSYMBOL_select_stmt = 84,               /* select_stmt  */
  YYSYMBOL_union_query = 85,               /* union_query  */
  YYSYMBOL_fieldList = 86,                 /* fieldList  */
  YYSYMBOL_colNameList = 87,               /* colNameList  */
  YYSYMBOL_field = 88,                     /* field  */
  YYSYMBOL_type = 89,                      /* type  */
  YYSYMBOL_valueList = 90,                 /* valueList  */
  YYSYMBOL_value = 91,                     /* value  */
  YYSYMBOL_condition = 92,                 /* condition  */
  YYSYMBOL_optWhereClause = 93,            /* optWhereClause  */
  YYSYMBOL_whereClause = 94,               /* whereClause  */
  YYSYMBOL_col = 95,                       /* col  */
  YYSYMBOL_colList = 96,                   /* colList  */
  YYSYMBOL_op = 97,                        /* op  */
  YYSYMBOL_expr = 98,                      /* expr  */
  YYSYMBOL_setClauses = 99,                /* setClauses  */
  YYSYMBOL_setClause = 100,                /* setClause  */
  YYSYMBOL_selector = 101,                 /* selector  */
  YYSYMBOL_select_item_list = 102,         /* select_item_list  */
  YYSYMBOL_select_item = 103,              /* select_item  */
  YYSYMBOL_opt_alias = 104,                /* opt_alias  */
  YYSYMBOL_agg_func = 105,                 /* agg_func  */
  YYSYMBOL_opt_group_clause = 106,         /* opt_group_clause  */
  YYSYMBOL_opt_having_clause = 107,        /* opt_having_clause  */
  YYSYMBOL_having_clause = 108,            /* having_clause  */
  YYSYMBOL_having_condition = 109,         /* having_condition  */
  YYSYMBOL_having_term = 110,              /* having_term  */
  YYSYMBOL_tableList = 111,                /* tableList  */
  YYSYMBOL_opt_order_clause = 112,         /* opt_order_clause  */
  YYSYMBOL_order_clause = 113,             /* order_clause  */
  YYSYMBOL_order_item = 114,               /* order_item  */
  YYSYMBOL_opt_limit_clause = 115,         /* opt_limit_clause  */
  YYSYMBOL_opt_asc_desc = 116,             /* opt_asc_desc  */
  YYSYMBOL_set_knob_type = 117,            /* set_knob_type  */
  YYSYMBOL_tbName = 118,                   /* tbName  */
  YYSYMBOL_colName = 119                   /* colName  */
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
typedef yytype_uint8 yy_state_t;

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
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

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
#define YYFINAL  55
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   246

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  75
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  45
/* YYNRULES -- Number of rules.  */
#define YYNRULES  122
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  240

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   318


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
      66,    67,    74,    72,    68,    73,    69,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    64,
      70,    65,    71,     2,     2,     2,     2,     2,     2,     2,
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
      55,    56,    57,    58,    59,    60,    61,    62,    63
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   116,   116,   121,   126,   131,   136,   144,   145,   146,
     147,   148,   152,   156,   160,   164,   171,   178,   182,   186,
     190,   197,   201,   205,   209,   213,   222,   234,   238,   242,
     246,   250,   254,   261,   265,   269,   273,   277,   284,   291,
     295,   302,   306,   313,   317,   324,   331,   335,   339,   346,
     350,   357,   361,   365,   369,   376,   380,   384,   389,   394,
     402,   403,   410,   414,   421,   425,   432,   436,   443,   447,
     451,   455,   459,   463,   470,   474,   481,   485,   492,   496,
     500,   504,   511,   515,   519,   523,   530,   534,   541,   546,
     552,   556,   560,   564,   568,   572,   579,   584,   590,   595,
     601,   605,   612,   619,   623,   627,   634,   638,   642,   649,
     653,   657,   661,   668,   675,   680,   686,   687,   688,   692,
     693,   696,   698
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
  "\"end of file\"", "error", "\"invalid token\"", "SHOW", "TABLES",
  "CREATE", "TABLE", "DROP", "DESC", "INSERT", "INTO", "VALUES", "DELETE",
  "FROM", "ASC", "ORDER", "BY", "WHERE", "UPDATE", "SET", "SELECT", "INT",
  "CHAR", "FLOAT", "INDEX", "AND", "JOIN", "EXIT", "HELP", "TXN_BEGIN",
  "TXN_COMMIT", "TXN_ABORT", "TXN_ROLLBACK", "ORDER_BY", "ENABLE_NESTLOOP",
  "ENABLE_SORTMERGE", "STATIC_CHECKPOINT", "TRANSACTION", "ISOLATION",
  "LEVEL", "SNAPSHOT", "SNAPSHOT_ISOLATION", "SERIALIZABLE", "SESSION",
  "CHARACTERISTICS", "GROUP", "HAVING", "AS", "COUNT", "MAX", "MIN", "SUM",
  "AVG", "LIMIT", "UNION", "LEQ", "NEQ", "GEQ", "T_EOF", "IDENTIFIER",
  "VALUE_STRING", "VALUE_INT", "VALUE_FLOAT", "VALUE_BOOL", "';'", "'='",
  "'('", "')'", "','", "'.'", "'<'", "'>'", "'+'", "'-'", "'*'", "$accept",
  "start", "stmt", "txnStmt", "dbStmt", "setStmt", "isolationLevel", "ddl",
  "dml", "select_stmt", "union_query", "fieldList", "colNameList", "field",
  "type", "valueList", "value", "condition", "optWhereClause",
  "whereClause", "col", "colList", "op", "expr", "setClauses", "setClause",
  "selector", "select_item_list", "select_item", "opt_alias", "agg_func",
  "opt_group_clause", "opt_having_clause", "having_clause",
  "having_condition", "having_term", "tableList", "opt_order_clause",
  "order_clause", "order_item", "opt_limit_clause", "opt_asc_desc",
  "set_knob_type", "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-111)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-122)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     170,    57,    14,    18,   -25,    27,    63,   -25,   113,     3,
    -111,  -111,  -111,  -111,  -111,  -111,  -111,    86,    24,  -111,
    -111,  -111,  -111,  -111,  -111,  -111,   -25,   -25,  -111,   -25,
     -25,  -111,  -111,   -25,   -25,    71,  -111,  -111,    54,    -1,
      42,    49,    50,    51,    64,    83,    82,  -111,   105,   141,
      87,  -111,   105,    89,  -111,  -111,  -111,    91,   106,  -111,
     114,   163,   164,   124,   145,   147,   139,   128,   -46,   134,
     134,   134,   134,   124,  -111,     4,    94,  -111,   124,   124,
     124,   124,   121,    72,  -111,  -111,    -2,  -111,   129,   -24,
     157,   166,  -111,   137,   138,   140,   142,   143,   144,  -111,
     186,   -12,  -111,  -111,  -111,    -3,  -111,    88,     1,  -111,
      30,   108,  -111,  -111,  -111,  -111,     2,  -111,   183,     2,
       2,   124,  -111,    66,   174,  -111,  -111,   154,  -111,   -24,
     176,  -111,  -111,  -111,  -111,  -111,  -111,     3,   161,   -27,
     -25,   -25,   171,  -111,   124,  -111,   151,  -111,  -111,  -111,
     124,  -111,    35,  -111,  -111,  -111,  -111,  -111,  -111,  -111,
     115,    72,    72,    72,  -111,  -111,    33,  -111,  -111,  -111,
     179,   206,   186,   186,   173,  -111,  -111,   205,   177,  -111,
     165,  -111,  -111,   108,  -111,  -111,  -111,  -111,  -111,  -111,
    -111,  -111,   108,   108,  -111,   -24,   -25,  -111,  -111,   -25,
     134,    72,   207,   158,  -111,  -111,  -111,  -111,   207,  -111,
     156,  -111,  -111,  -111,   202,  -111,     2,   213,   178,  -111,
     178,   134,    72,    72,   134,   169,  -111,  -111,  -111,  -111,
    -111,    25,   167,  -111,  -111,  -111,  -111,  -111,   134,  -111
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       5,     4,    12,    13,    14,    15,     6,     0,     3,    10,
       7,    11,     8,     9,    36,    16,     0,     0,    28,     0,
       0,   121,    30,     0,     0,     0,   119,   120,     0,     0,
       0,     0,     0,     0,     0,     0,   122,    82,    89,     0,
      83,    84,    89,     0,    65,     1,     2,     0,     0,    29,
       0,     0,    60,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    86,     0,     0,    87,     0,     0,
       0,     0,     0,     0,    34,   122,    60,    76,     0,     0,
       0,     0,    17,     0,     0,     0,     0,     0,     0,    88,
       0,    60,   106,    85,    64,     0,    41,     0,     0,    43,
       0,     0,    53,    51,    52,    54,     0,    62,    61,     0,
       0,     0,    35,     0,    23,    22,    24,    25,    18,     0,
       0,    91,    90,    92,    93,    94,    95,     0,     0,     0,
       0,     0,    97,    27,     0,    46,     0,    48,    45,    31,
       0,    32,     0,    49,    72,    71,    73,    68,    69,    70,
       0,     0,     0,     0,    77,    78,     0,    21,    26,    19,
       0,     0,     0,     0,     0,   108,   107,     0,    99,    42,
       0,    44,    33,     0,    58,    63,    74,    75,    55,    57,
      56,    59,     0,     0,    81,     0,     0,    39,    40,     0,
       0,     0,   110,     0,    50,    79,    80,    20,   110,    66,
      96,   103,   104,   105,    98,   100,     0,     0,   115,    47,
     115,     0,     0,     0,     0,     0,    38,    37,    67,   101,
     102,   118,   109,   111,   114,   117,   116,   113,     0,   112
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -111,  -111,  -111,  -111,  -111,  -111,  -110,  -111,  -111,   -98,
    -111,  -111,   152,    90,  -111,  -111,   -79,    75,   -41,  -111,
     -62,  -111,  -108,    69,  -111,   116,   101,  -111,   168,   187,
     -82,  -111,  -111,  -111,    19,    17,  -111,    34,  -111,     5,
      26,  -111,  -111,    -4,   -32
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    17,    18,    19,    20,    21,   128,    22,    23,    24,
     139,   105,   108,   106,   148,   152,   211,   117,    84,   118,
      48,   210,   160,   188,    86,    87,    49,    50,    51,    74,
      52,   178,   202,   214,   215,   216,   101,   218,   232,   233,
     226,   237,    40,    53,    54
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      32,   120,   138,    35,   116,    83,    94,    95,    96,    97,
      98,   162,   163,    46,   140,    83,   124,   125,   126,   169,
      26,   119,    57,    58,    29,    59,    60,   173,    93,    61,
      62,    88,   153,   235,    31,   127,    65,    33,    27,   236,
     174,    99,    30,    66,   165,   122,   104,   107,   109,   109,
      28,    41,    42,    43,    44,    45,   141,   154,   155,   156,
     142,    25,    46,    31,   143,   144,   121,   157,   149,   150,
     100,   102,   158,   159,   197,   198,    34,    47,   184,   120,
     189,   191,   116,   186,   186,   207,    55,   194,    56,    88,
      63,   166,    64,   112,   113,   114,   115,   151,   150,   119,
     187,   187,   182,   183,   204,   192,   193,    67,   223,   145,
     146,   147,   107,   205,   206,    68,    69,    70,   181,   213,
      41,    42,    43,    44,    45,    85,   112,   113,   114,   115,
      71,    46,   112,   113,   114,   115,   175,   176,   209,   212,
     213,   213,    41,    42,    43,    44,    45,    36,    37,    72,
      38,  -121,    73,    46,    75,    76,    39,    79,    78,   228,
     212,   212,   231,    41,    42,    43,    44,    45,   112,   113,
     114,   115,    80,     1,    82,     2,   231,     3,     4,     5,
      81,    83,     6,    85,    89,    90,    91,   111,     7,     8,
       9,    92,   102,    46,   123,   208,   129,    10,    11,    12,
      13,    14,    15,   130,   131,   132,   137,   133,   161,   134,
     135,   136,   167,   168,   170,   172,   177,   180,   195,   196,
     199,   200,   217,   201,   221,   219,   203,   222,    16,   224,
     234,   225,   190,   110,   179,   238,   185,   164,   171,    77,
     230,   229,   220,   239,   103,     0,   227
};

static const yytype_int16 yycheck[] =
{
       4,    83,   100,     7,    83,    17,    68,    69,    70,    71,
      72,   119,   120,    59,    26,    17,    40,    41,    42,   129,
       6,    83,    26,    27,     6,    29,    30,    54,    74,    33,
      34,    63,   111,     8,    59,    59,    37,    10,    24,    14,
      67,    73,    24,    44,   123,    86,    78,    79,    80,    81,
      36,    48,    49,    50,    51,    52,    68,    55,    56,    57,
     101,     4,    59,    59,    67,    68,    68,    65,    67,    68,
      66,    75,    70,    71,   172,   173,    13,    74,   160,   161,
     162,   163,   161,   162,   163,   195,     0,   166,    64,   121,
      19,   123,    38,    60,    61,    62,    63,    67,    68,   161,
     162,   163,    67,    68,   183,    72,    73,    65,   216,    21,
      22,    23,   144,   192,   193,    66,    66,    66,   150,   201,
      48,    49,    50,    51,    52,    59,    60,    61,    62,    63,
      66,    59,    60,    61,    62,    63,   140,   141,   200,   201,
     222,   223,    48,    49,    50,    51,    52,    34,    35,    66,
      37,    69,    47,    59,    13,    68,    43,    66,    69,   221,
     222,   223,   224,    48,    49,    50,    51,    52,    60,    61,
      62,    63,    66,     3,    11,     5,   238,     7,     8,     9,
      66,    17,    12,    59,    39,    38,    47,    66,    18,    19,
      20,    63,   196,    59,    65,   199,    39,    27,    28,    29,
      30,    31,    32,    37,    67,    67,    20,    67,    25,    67,
      67,    67,    38,    59,    38,    54,    45,    66,    39,    13,
      47,    16,    15,    46,    68,    67,    61,    25,    58,    16,
      61,    53,   163,    81,   144,    68,   161,   121,   137,    52,
     223,   222,   208,   238,    76,    -1,   220
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      27,    28,    29,    30,    31,    32,    58,    76,    77,    78,
      79,    80,    82,    83,    84,     4,     6,    24,    36,     6,
      24,    59,   118,    10,    13,   118,    34,    35,    37,    43,
     117,    48,    49,    50,    51,    52,    59,    74,    95,   101,
     102,   103,   105,   118,   119,     0,    64,   118,   118,   118,
     118,   118,   118,    19,    38,    37,    44,    65,    66,    66,
      66,    66,    66,    47,   104,    13,    68,   104,    69,    66,
      66,    66,    11,    17,    93,    59,    99,   100,   119,    39,
      38,    47,    63,    74,    95,    95,    95,    95,    95,   119,
      66,   111,   118,   103,   119,    86,    88,   119,    87,   119,
      87,    66,    60,    61,    62,    63,    91,    92,    94,    95,
     105,    68,    93,    65,    40,    41,    42,    59,    81,    39,
      37,    67,    67,    67,    67,    67,    67,    20,    84,    85,
      26,    68,    93,    67,    68,    21,    22,    23,    89,    67,
      68,    67,    90,    91,    55,    56,    57,    65,    70,    71,
      97,    25,    97,    97,   100,    91,   119,    38,    59,    81,
      38,   101,    54,    54,    67,   118,   118,    45,   106,    88,
      66,   119,    67,    68,   105,    92,    91,    95,    98,   105,
      98,   105,    72,    73,    91,    39,    13,    84,    84,    47,
      16,    46,   107,    61,    91,    91,    91,    81,   118,    95,
      96,    91,    95,   105,   108,   109,   110,    15,   112,    67,
     112,    68,    25,    97,    16,    53,   115,   115,    95,   109,
     110,    95,   113,   114,    61,     8,    14,   116,    68,   114
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    75,    76,    76,    76,    76,    76,    77,    77,    77,
      77,    77,    78,    78,    78,    78,    79,    80,    80,    80,
      80,    81,    81,    81,    81,    81,    81,    82,    82,    82,
      82,    82,    82,    83,    83,    83,    83,    83,    84,    85,
      85,    86,    86,    87,    87,    88,    89,    89,    89,    90,
      90,    91,    91,    91,    91,    92,    92,    92,    92,    92,
      93,    93,    94,    94,    95,    95,    96,    96,    97,    97,
      97,    97,    97,    97,    98,    98,    99,    99,   100,   100,
     100,   100,   101,   101,   102,   102,   103,   103,   104,   104,
     105,   105,   105,   105,   105,   105,   106,   106,   107,   107,
     108,   108,   109,   110,   110,   110,   111,   111,   111,   112,
     112,   113,   113,   114,   115,   115,   116,   116,   116,   117,
     117,   118,   119
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     2,     4,     5,     6,
       8,     2,     1,     1,     1,     1,     2,     6,     2,     3,
       2,     6,     6,     7,     4,     5,     1,    10,     9,     3,
       3,     1,     3,     1,     3,     2,     1,     4,     1,     1,
       3,     1,     1,     1,     1,     3,     3,     3,     3,     3,
       0,     2,     1,     3,     3,     1,     1,     3,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     3,     3,     5,
       5,     4,     1,     1,     1,     3,     2,     2,     2,     0,
       4,     4,     4,     4,     4,     4,     3,     0,     2,     0,
       1,     3,     3,     1,     1,     1,     1,     3,     3,     3,
       0,     1,     3,     2,     2,     0,     1,     1,     0,     1,
       1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (&yylloc, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

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
#if YYDEBUG

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

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

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
                  Kind, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
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
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp);
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
                 int yyrule)
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
                       &(yylsp[(yyi + 1) - (yynrhs)]));
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


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
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
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
yyparse (void)
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
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
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

  yychar = YYEMPTY; /* Cause a token to be read.  */

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
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, &yylloc);
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
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
  yychar = YYEMPTY;
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
  case 2: /* start: stmt ';'  */
#line 117 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1806 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: stmt  */
#line 122 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[0].sv_node);
        YYACCEPT;
    }
#line 1815 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: HELP  */
#line 127 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1824 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: EXIT  */
#line 132 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1833 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 6: /* start: T_EOF  */
#line 137 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1842 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_BEGIN  */
#line 153 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1850 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_COMMIT  */
#line 157 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1858 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ABORT  */
#line 161 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1866 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 15: /* txnStmt: TXN_ROLLBACK  */
#line 165 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1874 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW TABLES  */
#line 172 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1882 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 179 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1890 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 18: /* setStmt: SET TRANSACTION ISOLATION LEVEL isolationLevel  */
#line 183 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetTransactionIsolation>((yyvsp[0].sv_bool));
    }
#line 1898 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 19: /* setStmt: SET SESSION TRANSACTION ISOLATION LEVEL isolationLevel  */
#line 187 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetTransactionIsolation>((yyvsp[0].sv_bool));
    }
#line 1906 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 20: /* setStmt: SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL isolationLevel  */
#line 191 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetTransactionIsolation>((yyvsp[0].sv_bool));
    }
#line 1914 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 21: /* isolationLevel: SNAPSHOT ISOLATION  */
#line 198 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_bool) = false;
    }
#line 1922 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 22: /* isolationLevel: SNAPSHOT_ISOLATION  */
#line 202 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_bool) = false;
    }
#line 1930 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 23: /* isolationLevel: SNAPSHOT  */
#line 206 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_bool) = false;
    }
#line 1938 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 24: /* isolationLevel: SERIALIZABLE  */
#line 210 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_bool) = true;
    }
#line 1946 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 25: /* isolationLevel: IDENTIFIER  */
#line 214 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        bool serializable = false;
        if (!parse_compatible_isolation_identifier((yyvsp[0].sv_str), &serializable)) {
            yyerror(&(yyloc), "unsupported isolation level");
            YYERROR;
        }
        (yyval.sv_bool) = serializable;
    }
#line 1959 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 26: /* isolationLevel: IDENTIFIER IDENTIFIER  */
#line 223 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        bool serializable = false;
        if (!parse_compatible_isolation_pair((yyvsp[-1].sv_str), (yyvsp[0].sv_str), &serializable)) {
            yyerror(&(yyloc), "unsupported isolation level");
            YYERROR;
        }
        (yyval.sv_bool) = serializable;
    }
#line 1972 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 27: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 235 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1980 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 28: /* ddl: CREATE STATIC_CHECKPOINT  */
#line 239 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateStaticCheckpoint>();
    }
#line 1988 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 29: /* ddl: DROP TABLE tbName  */
#line 243 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1996 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 30: /* ddl: DESC tbName  */
#line 247 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 2004 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 31: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 251 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2012 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 32: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 255 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 2020 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 33: /* dml: INSERT INTO tbName VALUES '(' valueList ')'  */
#line 262 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-4].sv_str), (yyvsp[-1].sv_vals));
    }
#line 2028 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 34: /* dml: DELETE FROM tbName optWhereClause  */
#line 266 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 2036 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 35: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 270 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 2044 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 36: /* dml: select_stmt  */
#line 274 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_select_stmt);
    }
#line 2052 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 37: /* dml: SELECT selector FROM '(' union_query ')' AS tbName opt_order_clause opt_limit_clause  */
#line 278 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UnionStmt>((yyvsp[-5].sv_select_stmts), (yyvsp[-2].sv_str), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int));
    }
#line 2060 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 38: /* select_stmt: SELECT selector FROM tableList optWhereClause opt_group_clause opt_having_clause opt_order_clause opt_limit_clause  */
#line 285 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_stmt) = std::make_shared<SelectStmt>((yyvsp[-7].sv_select_items), (yyvsp[-5].sv_strs), (yyvsp[-4].sv_conds), (yyvsp[-3].sv_cols), (yyvsp[-2].sv_having_conds), (yyvsp[-1].sv_orderbys), (yyvsp[0].sv_int) >= 0, (yyvsp[0].sv_int));
    }
#line 2068 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 39: /* union_query: select_stmt UNION select_stmt  */
#line 292 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_stmts) = std::vector<std::shared_ptr<SelectStmt>>{(yyvsp[-2].sv_select_stmt), (yyvsp[0].sv_select_stmt)};
    }
#line 2076 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 40: /* union_query: union_query UNION select_stmt  */
#line 296 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_stmts).push_back((yyvsp[0].sv_select_stmt));
    }
#line 2084 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 41: /* fieldList: field  */
#line 303 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 2092 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 42: /* fieldList: fieldList ',' field  */
#line 307 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 2100 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 43: /* colNameList: colName  */
#line 314 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2108 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 44: /* colNameList: colNameList ',' colName  */
#line 318 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2116 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 45: /* field: colName type  */
#line 325 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 2124 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 46: /* type: INT  */
#line 332 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2132 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 47: /* type: CHAR '(' VALUE_INT ')'  */
#line 336 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2140 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 48: /* type: FLOAT  */
#line 340 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2148 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 49: /* valueList: value  */
#line 347 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2156 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 50: /* valueList: valueList ',' value  */
#line 351 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2164 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 51: /* value: VALUE_INT  */
#line 358 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2172 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 52: /* value: VALUE_FLOAT  */
#line 362 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2180 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 53: /* value: VALUE_STRING  */
#line 366 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2188 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 54: /* value: VALUE_BOOL  */
#line 370 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2196 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 55: /* condition: col op expr  */
#line 377 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2204 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 56: /* condition: agg_func op expr  */
#line 381 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>(std::make_shared<Col>("", "__invalid_where_agg__"), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2212 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 57: /* condition: col op agg_func  */
#line 385 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op),
                std::static_pointer_cast<Expr>(std::make_shared<Col>("", "__invalid_where_agg__")));
    }
#line 2221 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 58: /* condition: value op agg_func  */
#line 390 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>(std::make_shared<Col>("", "__invalid_where_agg__"), (yyvsp[-1].sv_comp_op),
                std::static_pointer_cast<Expr>(std::make_shared<Col>("", "__invalid_where_agg__")));
    }
#line 2230 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 59: /* condition: agg_func op agg_func  */
#line 395 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>(std::make_shared<Col>("", "__invalid_where_agg__"), (yyvsp[-1].sv_comp_op),
                std::static_pointer_cast<Expr>(std::make_shared<Col>("", "__invalid_where_agg__")));
    }
#line 2239 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 60: /* optWhereClause: %empty  */
#line 402 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2245 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 61: /* optWhereClause: WHERE whereClause  */
#line 404 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2253 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 62: /* whereClause: condition  */
#line 411 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2261 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 63: /* whereClause: whereClause AND condition  */
#line 415 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2269 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 64: /* col: tbName '.' colName  */
#line 422 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2277 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 65: /* col: colName  */
#line 426 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2285 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 66: /* colList: col  */
#line 433 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2293 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 67: /* colList: colList ',' col  */
#line 437 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2301 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 68: /* op: '='  */
#line 444 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2309 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 69: /* op: '<'  */
#line 448 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2317 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 70: /* op: '>'  */
#line 452 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2325 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 71: /* op: NEQ  */
#line 456 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2333 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 72: /* op: LEQ  */
#line 460 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2341 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 73: /* op: GEQ  */
#line 464 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2349 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 74: /* expr: value  */
#line 471 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2357 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 75: /* expr: col  */
#line 475 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2365 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 76: /* setClauses: setClause  */
#line 482 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2373 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 77: /* setClauses: setClauses ',' setClause  */
#line 486 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2381 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 78: /* setClause: colName '=' value  */
#line 493 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2389 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 79: /* setClause: colName '=' colName '+' value  */
#line 497 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2397 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 80: /* setClause: colName '=' colName '-' value  */
#line 501 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), negate_numeric_value((yyvsp[0].sv_val)));
    }
#line 2405 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 81: /* setClause: colName '=' colName value  */
#line 505 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), (yyvsp[0].sv_val));
    }
#line 2413 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 82: /* selector: '*'  */
#line 512 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_items) = {};
    }
#line 2421 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 84: /* select_item_list: select_item  */
#line 520 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_items) = std::vector<std::shared_ptr<SelectItem>>{(yyvsp[0].sv_select_item)};
    }
#line 2429 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 85: /* select_item_list: select_item_list ',' select_item  */
#line 524 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_items).push_back((yyvsp[0].sv_select_item));
    }
#line 2437 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 86: /* select_item: col opt_alias  */
#line 531 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[-1].sv_col), (yyvsp[0].sv_str));
    }
#line 2445 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 87: /* select_item: agg_func opt_alias  */
#line 535 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[-1].sv_agg_func), (yyvsp[0].sv_str));
    }
#line 2453 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 88: /* opt_alias: AS colName  */
#line 542 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_str) = (yyvsp[0].sv_str);
    }
#line 2461 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 89: /* opt_alias: %empty  */
#line 546 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_str) = "";
    }
#line 2469 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 90: /* agg_func: COUNT '(' col ')'  */
#line 553 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, (yyvsp[-1].sv_col), false);
    }
#line 2477 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 91: /* agg_func: COUNT '(' '*' ')'  */
#line 557 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, nullptr, true);
    }
#line 2485 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 92: /* agg_func: MAX '(' col ')'  */
#line 561 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_MAX, (yyvsp[-1].sv_col), false);
    }
#line 2493 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 93: /* agg_func: MIN '(' col ')'  */
#line 565 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_MIN, (yyvsp[-1].sv_col), false);
    }
#line 2501 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 94: /* agg_func: SUM '(' col ')'  */
#line 569 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_SUM, (yyvsp[-1].sv_col), false);
    }
#line 2509 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 95: /* agg_func: AVG '(' col ')'  */
#line 573 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_AVG, (yyvsp[-1].sv_col), false);
    }
#line 2517 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 96: /* opt_group_clause: GROUP BY colList  */
#line 580 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cols) = (yyvsp[0].sv_cols);
    }
#line 2525 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 97: /* opt_group_clause: %empty  */
#line 584 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 2533 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 98: /* opt_having_clause: HAVING having_clause  */
#line 591 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_having_conds) = (yyvsp[0].sv_having_conds);
    }
#line 2541 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 99: /* opt_having_clause: %empty  */
#line 595 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_having_conds) = {};
    }
#line 2549 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 100: /* having_clause: having_condition  */
#line 602 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_having_conds) = std::vector<std::shared_ptr<HavingExpr>>{(yyvsp[0].sv_having_cond)};
    }
#line 2557 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 101: /* having_clause: having_clause AND having_condition  */
#line 606 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_having_conds).push_back((yyvsp[0].sv_having_cond));
    }
#line 2565 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 102: /* having_condition: having_term op having_term  */
#line 613 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_having_cond) = std::make_shared<HavingExpr>((yyvsp[-2].sv_expr), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2573 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 103: /* having_term: value  */
#line 620 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2581 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 104: /* having_term: col  */
#line 624 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2589 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 105: /* having_term: agg_func  */
#line 628 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_agg_func));
    }
#line 2597 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 106: /* tableList: tbName  */
#line 635 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 2605 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 107: /* tableList: tableList ',' tbName  */
#line 639 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2613 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 108: /* tableList: tableList JOIN tbName  */
#line 643 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 2621 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 109: /* opt_order_clause: ORDER BY order_clause  */
#line 650 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    { 
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys); 
    }
#line 2629 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 110: /* opt_order_clause: %empty  */
#line 653 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
                      { (yyval.sv_orderbys) = {}; }
#line 2635 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 111: /* order_clause: order_item  */
#line 658 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = std::vector<std::shared_ptr<OrderBy>>{(yyvsp[0].sv_orderby)};
    }
#line 2643 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 112: /* order_clause: order_clause ',' order_item  */
#line 662 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_orderbys).push_back((yyvsp[0].sv_orderby));
    }
#line 2651 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 113: /* order_item: col opt_asc_desc  */
#line 669 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    { 
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 2659 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 114: /* opt_limit_clause: LIMIT VALUE_INT  */
#line 676 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_int) = (yyvsp[0].sv_int);
    }
#line 2667 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 115: /* opt_limit_clause: %empty  */
#line 680 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
    {
        (yyval.sv_int) = -1;
    }
#line 2675 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 116: /* opt_asc_desc: ASC  */
#line 686 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2681 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 117: /* opt_asc_desc: DESC  */
#line 687 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2687 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 118: /* opt_asc_desc: %empty  */
#line 688 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2693 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 119: /* set_knob_type: ENABLE_NESTLOOP  */
#line 692 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2699 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;

  case 120: /* set_knob_type: ENABLE_SORTMERGE  */
#line 693 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2705 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"
    break;


#line 2709 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.tab.cpp"

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
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
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
        yyerror (&yylloc, yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc);
          yychar = YYEMPTY;
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
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp);
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
  yyerror (&yylloc, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp);
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

#line 699 "/mnt/c/Users/lincom/Documents/Codex/2026-06-27/5-2026-https-gitlab-eduxiji-net/perf_work/dblab_zmy/src/parser/yacc.y"

