/**
 * Copyright (c) 2014, Zhiyong Liu <NeeseNK at gmail dot com>
 * All rights reserved.
 */
#define __GNU_SOURCE
#include <math.h>
#include <sys/types.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "express.h"

typedef struct token_value value_t;
// token类型
enum {
    OP_BEG = 1,     // (
    OP_END,         // )
    OP_NUM,         // number
    OP_ID,          // id
    OP_STR,         // string
    OP_FUNC,        // function
    OP_NOT,         // !
    OP_BITCOMP,     // ~
    OP_MULTI,       // *
    OP_DIVI,        // /
    OP_MOD,         // %
    OP_ADD,         // +
    OP_SUB,         // -
    OP_SHIFTLEFT,   // <<
    OP_SHIFTRIGHT,  // >>
    OP_LT,          // <
    OP_GT,          // >
    OP_GE,          // >=
    OP_LE,          // <=
    OP_EQ,          // ==
    OP_NOTEQ,       // !=
    OP_REGEX,       // ~=
    OP_BITAND,      // &
    OP_BITXOR,      // ^
    OP_BITOR,       // |
    OP_AND,         // &&
    OP_OR,          // ||
    OP_SEP,         // ,
    OP_MAX,
};

// 运算符优先级
static char level[OP_MAX] = {
    -1,-1,-1,-1,-1,-1,      // ()
    127,                    // function
    126, 126,               // ! ~
    125, 125, 125,          // * / %
    124, 124,               // + -
    123, 123,               // << >>
    122, 122, 122, 122,     // < <= > >=
    121, 121, 121,          // == != ~=
    120,                    // &
    119,                    // ^
    118,                    // |
    117,                    // &&
    116,                    // ||
};

struct token {
    unsigned char   type;       // 类型
    unsigned char   nparam;     // 参数个数
    unsigned short  subtype;    // 子类型
    union {
        struct { uint32_t pos; uint32_t len; } str;
        const char *ptr;
        double num;
    };
};

struct bufflist { struct bufflist *next; char buff[]; };
struct express {
    struct token *rpn;          // 运算符逆波兰表示
    size_t size;                // rpn的长度
    char *strbuff;              // 保存token中的id和str
    value_t *stack;  // 计算时的参数栈
    struct bufflist *list;      // 保存计算时分配的内存，计算结束是释放
};

struct token_buff {
    struct token *tokens;
    size_t capacity;
    size_t size;
};

#define TYPE(n) (arg[n].type)
#define NUM_VAL(_NUM) (value_t) { .type = TV_NUM, .num = (_NUM) }
#define STR_VAL(_STR) (value_t) { .type = TV_STR, .str = (_STR) }
#define NUM(n) (TYPE(n)==TV_NUM?arg[n].num:(arg[n].str ? atof(arg[n].str):0))
#define STR(n) (TYPE(n)==TV_STR?arg[n].str:"")
#define LONG(n) (TYPE(n)==TV_NUM?(long)arg[n].num:(arg[n].str?atol(arg[n].str):0))
#define COMP(i,j,ST,OP) \
    ((TYPE(i)==TV_NUM||TYPE(j)==TV_NUM)?(ST(i) OP ST(j)):(strcmp(STR(i), STR(j)) OP 0))

typedef value_t (*token_fn)(value_t *arg, size_t narg, struct express *expr);

// 函数结构定义
struct function
{
    char     *name;
    token_fn  func;
    size_t    min;
    size_t    max;
};

// 分配一段内存保存在expr上，计算完之后会自动释放
static inline char *express_alloc(struct express *expr, size_t size)
{
    struct bufflist *n = calloc(1, sizeof(*n) + size);
    assert(n != NULL);
    n->next = expr->list, expr->list = n;

    return n->buff;
}

// strcmp封装
static value_t fn_strcmp(value_t *arg, size_t argc, struct express *expr)
{
    assert(argc == 2);
    return NUM_VAL(strcmp(STR(0), STR(1)));
}

// strlen封装
static value_t fn_strlen(value_t *arg, size_t argc, struct express *expr)
{
    assert(argc == 1);
    return NUM_VAL(strlen(STR(0)));
}

// 判断第一个参数是否和剩余参数中的一个相等
static value_t fn_in(value_t *arg, size_t argc, struct express *expr)
{
    int rc = 0;
    size_t i = 0;
    assert(argc >= 2);
    for (i = 1; i < argc; i++) {
        if ((rc = COMP(0, i, NUM, ==)))
            break;
    }
    return NUM_VAL(rc);
}

// strstr封装
static value_t fn_strstr(value_t *arg, size_t argc, struct express *expr)
{
    assert(argc == 2);
    return STR_VAL(strstr(STR(0), STR(1)));
}

// 返回字串
static value_t fn_substr(value_t *arg, size_t argc, struct express *expr)
{
    assert(argc == 2 || argc == 3);
    ssize_t off, len, sublen;
    const char *str = STR(0);
    char *ptr = NULL;
    if (str == NULL)
        return STR_VAL("");
    len = strlen(str);
    if ((off = LONG(1)) < 0)
        off += len;
    if (off >= len || off < 0)
        return STR_VAL("");

    sublen = (argc == 3) ? LONG(2) : len;
    if (sublen > (len - off))
        sublen = len - off;
    ptr = express_alloc(expr, sublen + 1);

    memcpy(ptr, str + off, sublen);
    ptr[sublen] = 0;

    return STR_VAL(ptr);
}

// pow封装
static value_t fn_pow(value_t *arg, size_t argc, struct express *expr)
{
    assert(argc == 2);
    return NUM_VAL(pow(NUM(0), NUM(1)));
}

// x ? y : z 计算
static value_t fn_case(value_t *arg, size_t argc, struct express *expr)
{
    assert(argc == 3);
    return (arg[0].type == TV_NUM ? !arg[0].num : !arg[0].str) ? arg[2] : arg[1];
}

// time(NULL) 封装
static value_t fn_time(value_t *arg, size_t argc, struct express *expr)
{
    assert(argc == 0);
    return NUM_VAL(time(NULL));
}

enum {
    F_STRCMP = 1, // strcmp
    F_STRLEN,     // strlen
    F_STRSTR,     // strstr
    F_POW,        // pow
    F_IN,         // in
    F_CASE,       // case
    F_TIME,       // time
    F_SUBSTR,     // substr
    F_MAX,
};

static const struct function token_funcs[F_MAX] = {
    { NULL,     NULL,       0,      0 },
    { "strcmp", fn_strcmp,  2,      2 },
    { "strlen", fn_strlen,  1,      1 },
    { "strstr", fn_strstr,  2,      2 },
    { "pow",    fn_pow,     2,      2 },
    { "in",     fn_in,      2,      ~0},
    { "case",   fn_case,    3,      3 },
    { "time",   fn_time,    0,      0 },
    { "substr", fn_substr,  2,      3 },
};

#define FUNC(ID) (len == strlen(token_funcs[ID].name) && memcmp(func, token_funcs[ID].name, len) == 0) ? (ID) : 0
static inline int check_function(const char *func, size_t len)
{
    switch (*func) {
    case 'p': return FUNC(F_POW);
    case 'i': return FUNC(F_IN);
    case 'c': return FUNC(F_CASE);
    case 't': return FUNC(F_TIME);
    case 's':
        if (len <= 3)
            return 0;
        switch (func[3]) {
        case 'c': return FUNC(F_STRCMP);
        case 'l': return FUNC(F_STRLEN);
        case 's':
            switch (func[1]) {
            case 't': return FUNC(F_STRSTR);
            case 'u': return FUNC(F_SUBSTR);
            }
        }
        return 0;
    }

    return 0;
}

static inline struct token *token_pushback(struct token_buff *buff)
{
    if (buff->capacity <= buff->size) {
        size_t size = buff->capacity * 1.5 + 8;
        buff->tokens = realloc(buff->tokens, size * sizeof(struct token));
        assert(buff->tokens != NULL);
        buff->capacity = size;
    }

    assert(buff->size < buff->capacity);
    memset(&buff->tokens[buff->size], 0, sizeof(struct token));
    return &buff->tokens[buff->size++];
}

static inline const char *skip_blank(const char *pos)
{
    while (isspace(*pos))
        pos++;
    return pos;
}

static inline const char *skip_digits(const char *pos)
{
	while (isdigit(*pos))
		pos++;
	return pos;
}

static inline int parse_number(const char *expr, const char **ppos, struct token *token)
{
    char *pos = NULL;
    double v = strtod(*ppos, &pos);
    if (pos == *ppos)
        return false;
    token->type = OP_NUM;
    token->num  = v;
    *ppos   = pos;
    return true;
}

static inline const char *find_quot(const char *pos)
{
    while (*pos) {
        if (*pos == '"')
            return pos;
        if (*pos == '\\' && pos[1])
            pos++;
        pos++;
    }

    return NULL;
}

static inline int parse_str(const char *expr, const char **ppos, struct token *token)
{
    const char *pos = strchr(*ppos + 1, (*ppos)[0]);
    if (pos && (*ppos)[0] == '"' && *(pos - 1) == '\\')
        pos = find_quot(pos + 1);
    if (pos == NULL)
        return false;
    token->type = OP_STR;
    token->str.pos = (*ppos - expr);
    token->str.len = (pos - *ppos) + 1;
    *ppos = pos + 1;
    return true;
}

// 解析函数名和变量名
static inline int parse_id(const char *expr, const char **ppos, struct token_buff *rpn, struct token_buff *stack)
{
    const char *pos = *ppos, *end = NULL;
    struct token *token = NULL;
    // a-zA-z 0-9 _ $ .
    while (isalnum(*pos) || isalpha(*pos) || *pos == '.')
        pos++;
    end = pos, pos = skip_blank(pos);
    if (*pos == '(') { // 是函数
        token = token_pushback(stack);
        if ((token->subtype = check_function(*ppos, end - *ppos)) == 0)
            return false;
        token->type = OP_FUNC;
    } else { // 变量名
        token = token_pushback(rpn);
        token->type = OP_ID;
        token->str.pos = *ppos - expr;
        token->str.len = end - *ppos;
    }
    *ppos = pos;

    return token->type;
}

// 检查rpn是否合法
static inline int check_RPN(struct token *rpn, size_t size)
{
    size_t nparam = 0, i;
    for (i = 0; i < size; i++) {
        struct token *t = &rpn[i];
        if (t->type == OP_NUM || t->type == OP_ID || t->type == OP_STR) {
            nparam++;
        } else {
            const struct function *fn = &token_funcs[t->subtype];
            if (nparam < t->nparam)
                return false;
            if (t->type == OP_FUNC && (t->nparam < fn->min || t->nparam > fn->max))
                return false;
            nparam -= t->nparam - 1;
        }
    }

    return nparam == 1;
}

// 返回token的长度
static inline int token_len(int type)
{
    switch (type) {
    case OP_NUM:case OP_ID:case OP_STR:case OP_FUNC: return 0;
    case OP_GE :case OP_LE:case OP_EQ :case OP_NOTEQ:case OP_REGEX:case OP_AND:
    case OP_OR:case OP_SHIFTLEFT:case OP_SHIFTRIGHT: return 2;
    default: return 1;
    }
}

// 返回token的类型
static inline int token_type(const char *pos, int last)
{
    switch (*pos) {
    case '(': return OP_BEG;
    case ')': return OP_END;
    case '^': return OP_BITXOR;
    case '*': return OP_MULTI;
    case '/': return OP_DIVI;
    case '%': return OP_MOD;
    case ',': return OP_SEP;
    case '+': return (last == OP_END || last == OP_NUM || last == OP_ID) ? OP_ADD : OP_NUM;
    case '-': return (last == OP_END || last == OP_NUM || last == OP_ID) ? OP_SUB : OP_NUM;
    case '=': return pos[1] == '=' ? OP_EQ : 0;
    case '<': return pos[1] == '<' ? OP_SHIFTLEFT : (pos[1] == '=' ? OP_LE : OP_LT);
    case '>': return pos[1] == '>' ? OP_SHIFTRIGHT: (pos[1] == '=' ? OP_GE : OP_GT);
    case '|': return pos[1] == '|' ? OP_OR : OP_BITOR;
    case '&': return pos[1] == '&' ? OP_AND: OP_BITAND;
    case '!': return pos[1] == '=' ? OP_NOTEQ : OP_NOT;
    case '~': return pos[1] == '=' ? OP_REGEX : OP_BITCOMP;
    case '\'':case '\"': return OP_STR;
    case '0' ... '9': return OP_NUM;
    case 'a' ... 'z':case 'A' ... 'Z':case '_':case '$': return OP_ID;
    default : return 0;
    }
}

// 返回运算符需要的参数个数
static inline int token_params(int type)
{
    switch (type) {
    case OP_NOT:case OP_BITCOMP: return 1;
    case OP_BEG:case OP_END:case OP_NUM:case OP_STR:case OP_ID: return 0;
    default: return 2;
    }
}

static inline int argument_push(struct token_buff *rpn, struct token_buff *stack)
{
    while (stack->size > 0) {
        if (stack->tokens[stack->size - 1].type == OP_BEG)
            break;
        *token_pushback(rpn) = stack->tokens[--stack->size];
    }
    if (stack->size < 2 || stack->tokens[stack->size - 2].type != OP_FUNC)
        return false;
    stack->tokens[stack->size - 2].nparam++;
    return true;
}

static inline int parenth_end(int last, struct token_buff *rpn, struct token_buff *stack)
{
    ssize_t ss = stack->size;
    while (ss-- > 0) {
        if (stack->tokens[ss].type == OP_BEG)
            break;
        *token_pushback(rpn) = stack->tokens[ss];
    }

    if (ss < 0)
        return false;
    if (ss > 0 && stack->tokens[ss - 1].type == OP_FUNC) {
        stack->tokens[ss - 1].nparam += (last != OP_BEG);
        *token_pushback(rpn) = stack->tokens[--ss];
    }
    stack->size = ss;
    return true;
}

#define ISLOW(l,r) ((level[l]<level[r])||(((l)!=OP_NOT&&(l)!=OP_BITCOMP)&&level[l]==level[r]))
static inline void opera_push(int type, struct token_buff *rpn, struct token_buff *stack)
{
    struct token *token = NULL;
    while (stack->size > 0 && ISLOW(type, stack->tokens[stack->size - 1].type))
        *token_pushback(rpn) = stack->tokens[--stack->size];
    token           = token_pushback(stack);
    token->type     = type;
    token->nparam   = token_params(type);
}

// http://en.wikipedia.org/wiki/Shunting-yard_algorithm
static int express_parse(const char *expr, struct token_buff *rpn, struct token_buff *stack)
{
    int last = 0, type = 0;
    const char *pos = expr;

    for (;;) {
        last = type, type = 0;
        if (*(pos = skip_blank(pos)) == 0)
            break;
        switch ((type = token_type(pos, last))) {
        case OP_ID:
            if (!(type = parse_id(expr, &pos, rpn, stack)))
                return false;
            break;
        case OP_NUM:
            if (!parse_number(expr, &pos, token_pushback(rpn)))
                return false;
            break;
        case OP_STR:
            if (!parse_str(expr, &pos, token_pushback(rpn)))
                return false;
            break;
        case OP_SEP:
            if (!argument_push(rpn, stack))
                return false;
            break;
        case OP_END: // )
            if (!parenth_end(last, rpn, stack))
                return false;
            break;
        case OP_BEG: // (
            token_pushback(stack)->type = type;
            break;
        case 0:
            return false;
        default:
            opera_push(type, rpn, stack);
            break;
        }
        pos += token_len(type);
    }

    if (stack->size > 0) {
        if (stack->tokens[stack->size - 1].type == OP_BEG)
            return false;
        while (stack->size)
            *token_pushback(rpn) = stack->tokens[--stack->size];
    }

    return check_RPN(rpn->tokens, rpn->size);
}

static inline void bufflist_clean(struct express *expr, const char *except)
{
    struct bufflist *save = NULL;
    while (expr->list) {
        struct bufflist *ptr = expr->list;
        expr->list = ptr->next;
        if (except == ptr->buff) {
            assert(save == NULL);
            save = ptr;
        } else {
            free(ptr);
        }
    }

    if (save)
        expr->list = save, save->next = NULL;
}

void express_destroy(struct express *expr)
{
    if (expr) {
        bufflist_clean(expr, NULL);
        free(expr->rpn);
        free(expr->strbuff);
        free(expr->stack);
        free(expr);
    }
}

// 去除字符串中的转义符
static inline size_t copystr(char *dest, const char *str, size_t len)
{
    if (str[0] == '"') {
        size_t l = 0;
        while (*(++str) != '"') {
            if (*str == '\\')
                str++;
            dest[l++] = *str;
        }
        len = l;
    } else {
        if (str[0] == '\'')
            str++, len -= 2;
        memcpy(dest, str, len);
    }
    dest[len] = 0;
    return len + 1;
}

struct express *express_create(const char *str)
{
    struct express *expr = NULL;
    struct token_buff rpn, stack;
    size_t i = 0, len = 0, off = 0;

    memset(&rpn, 0, sizeof(rpn));
    memset(&stack, 0, sizeof(stack));
    if (!express_parse(str, &rpn, &stack))
        goto DONE;

    expr = calloc(1, sizeof(*expr));
    assert(expr);
    expr->rpn = calloc(rpn.size, sizeof(*expr->rpn));
    assert(expr->rpn);

    // 计算需要保存的字符串的总长度
    for (i = 0; i < rpn.size; i++) {
        struct token *token = &rpn.tokens[i];
        if (token->type == OP_ID || token->type == OP_STR)
            len += token->str.len + 1;
    }

    expr->strbuff = calloc(len, 1);
    assert(expr->strbuff);
    // 复制token
    for (i = 0; i < rpn.size; i++) {
        struct token *token = &rpn.tokens[i];
        expr->rpn[i] = *token;
        if (token->type == OP_ID || token->type == OP_STR) {
            expr->rpn[i].ptr = expr->strbuff + off;
            // 复制字符串
            off += copystr(expr->strbuff + off, str + token->str.pos, token->str.len);
            assert(off <= len);
        }
    }
    expr->size = rpn.size;

    // 分配计算时使用的栈
    expr->stack = calloc(expr->size, sizeof(value_t));
    assert(expr->stack);
DONE:
    free(stack.tokens);
    free(rpn.tokens);
    return expr;
}

static inline value_t FUNC_OPT(struct token *token, value_t *arg, struct express *expr)
{
    return token_funcs[token->subtype].func(arg, token->nparam, expr);
}

static inline value_t REGEX_OPT(value_t *arg)
{
    int rc = 0;
    if (TYPE(0) == TV_STR && TYPE(0) == TV_STR) {
        regex_t reg[1];
        if (regcomp(reg, STR(1), REG_EXTENDED) == 0) {
            regmatch_t m[1];
            rc = !regexec(reg, STR(0), 1, m, REG_EXTENDED);
        }
        regfree(reg);
    }

    return NUM_VAL(rc);
}

static inline value_t FETCH_OPT(struct token *token, fetch_value_fn fetcher, void *ctx)
{
    value_t v = { .type = TV_NONE };
    assert(token->ptr != NULL);
    if (fetcher) {
        v = fetcher(ctx, token->ptr);
        assert(v.type == TV_NONE || v.type == TV_NUM || v.type == TV_STR);
    }
    if (v.type == TV_NONE)
        v = STR_VAL(token->ptr);

    return v;
}

static inline value_t NOT_OPT(value_t *arg)
{
    return NUM_VAL(arg[0].type == TV_NUM ? !arg[0].num : !arg[0].str);
}

#define NUM_OPT(ST, OP) NUM_VAL(ST(0) OP ST(1))
#define STR_OPT(ST, OP) NUM_VAL(COMP(0, 1, ST, OP))
value_t express_calculate(struct express *expr, fetch_value_fn fetcher, void *ctx)
{
    size_t i = 0, ss = 0;
    value_t *stack = expr->stack, *arg = NULL;
    struct token *t = NULL;
    for (i = 0; i < expr->size; i++) {
        t = &expr->rpn[i];
        assert(t->nparam <= ss);
        arg = stack + ss - t->nparam;
        switch (t->type) {
        case OP_BITCOMP:    arg[0] = NUM_VAL(~LONG(0));break;
        case OP_NOT:        arg[0] = NOT_OPT(arg);     break;
        case OP_MULTI:      arg[0] = NUM_OPT(NUM,  *); break;
        case OP_DIVI:       arg[0] = NUM_OPT(NUM,  /); break;
        case OP_MOD:        arg[0] = NUM_OPT(LONG, %); break;
        case OP_ADD:        arg[0] = NUM_OPT(NUM,  +); break;
        case OP_SUB:        arg[0] = NUM_OPT(NUM,  -); break;
        case OP_SHIFTLEFT:  arg[0] = NUM_OPT(LONG,<<); break;
        case OP_SHIFTRIGHT: arg[0] = NUM_OPT(LONG,>>); break;
        case OP_BITAND:     arg[0] = NUM_OPT(LONG, &); break;
        case OP_BITXOR:     arg[0] = NUM_OPT(LONG, ^); break;
        case OP_BITOR:      arg[0] = NUM_OPT(LONG, |); break;
        case OP_AND:        arg[0] = NUM_OPT(NUM, &&); break;
        case OP_OR:         arg[0] = NUM_OPT(NUM, ||); break;
        case OP_LT:         arg[0] = STR_OPT(NUM,  <); break;
        case OP_LE:         arg[0] = STR_OPT(NUM, <=); break;
        case OP_GT:         arg[0] = STR_OPT(NUM,  >); break;
        case OP_GE:         arg[0] = STR_OPT(NUM, >=); break;
        case OP_EQ:         arg[0] = STR_OPT(NUM, ==); break;
        case OP_NOTEQ:      arg[0] = STR_OPT(NUM, !=); break;
        case OP_REGEX:      arg[0] = REGEX_OPT(arg);   break;
        case OP_NUM:        arg[0] = NUM_VAL(t->num);  break;
        case OP_STR:        arg[0] = STR_VAL(t->ptr);  break;
        case OP_FUNC:       arg[0] = FUNC_OPT(t, arg, expr); break;
        case OP_ID:         arg[0] = FETCH_OPT(t, fetcher, ctx); break;
        default: assert(0 && "unknow type");
        }
        ss = ss + 1 - t->nparam;
        assert(ss <= expr->size);
    }

    assert(ss == 1);
    // 清空临时分配的内存
    bufflist_clean(expr, stack[0].type == TV_STR ? stack[0].str : NULL);

    return stack[0];
}
