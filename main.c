#include <stdio.h>
#include <stdlib.h>
#include "express.h"

int main(int argc ,char *argv[])
{
    struct token_value ret;
    struct express *expr = NULL;

    if (argc < 2) {
        printf("Usage: %s expr\n", argv[1]);
        exit(1);
    }

    if ((expr = express_create(argv[1])) == NULL) {
        printf("parse failed\n");
        exit(1);
    }

    ret = express_calculate(expr, NULL, NULL);
    if (ret.type == TV_NUM)
        printf("result = %lf\n", ret.num);
    else if (ret.type == TV_STR)
        printf("result = \"%s\"\n", ret.str);
    else if (ret.type == TV_NONE)
        printf("result = NONE\n");

    express_destroy(expr);

    return 0;
}
