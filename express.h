/**
 * Copyright (c) 2014, Zhiyong Liu <NeeseNK at gmail dot com>
 * All rights reserved.
 */

/**
 * 表达式解析， 支持 + - * / % < << > >> | || & || ! ^ ~ == != 等操作符,
 * 支持自定义函数和变量定义。变量类型只支持数字(整数和浮点数)和字符串。
 */

#ifndef __EXPRESS_H__
#define __EXPRESS_H__

enum {
    TV_NONE= 0, // 未赋值
    TV_NUM = 1, // 数字
    TV_STR = 2, // 字符串
};

struct token_value
{
    int type;               // TV_NUM OR TV_STR
    union {
        double num;         // 保存数字
        const char *str;    // 保存字符串
    };
};

typedef struct express express_t;

/**
 * 用户提供的获取变量的回调, 如果返回的是字符串，
 * 需要用户分配并在express_calculate调用结束之后释放
 * @ctx 获取变量的上下文，由express_calculate透传过来
 * @name 变量的名字
 * @return 返回变量的值
 */
typedef struct token_value (*fetch_value_fn)(void *ctx, const char *name);

/**
 * 计算表达式， 返回结果
 * @expr 要计算的表达式
 * @fetcher 表达式中一些变量的获取函数
 * @ctx 获取变量的上下文对象，透传给fetcher
 * @return 返回计算结果
 */
struct token_value express_calculate(express_t *expr, fetch_value_fn fetcher, void *ctx);

/**
 * 创建一个表达式
 * @expr 要解析的表达式字符串
 * @return 解析成功返回表达式对象，expr有错误返回NULL
 */
express_t *express_create(const char *expr);

/**
 * 销毁表达式对象
 */
void express_destroy(express_t *expr);

#endif /* __EXPRESS_H__ */
