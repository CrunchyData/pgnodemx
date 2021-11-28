/*
 * Copyright (C) 2008 Mark Wong
 */

#ifndef _PG_PROCTAB_H_
#define _PG_PROCTAB_H_


#define BIGINT_LEN 20
#define FLOAT_LEN 20
#define INTEGER_LEN 10

#include <ctype.h>
#include <linux/magic.h>

#define PROCFS "/proc"

#define GET_NEXT_VALUE(p, q, value, length, msg, delim) \
        if ((q = strchr(p, delim)) == NULL) \
        { \
            elog(ERROR, msg); \
            return 0; \
        } \
        length = q - p; \
        strncpy(value, p, length); \
        value[length] = '\0'; \
        p = q + 1;

#define SKIP_TOKEN(p) \
		/* Skipping leading white space. */ \
		while (isspace(*p)) \
			p++; \
		/* Skip token. */ \
		while (*p && !isspace(*p)) \
			p++; \
		/* Skipping trailing white space. */ \
		while (isspace(*p)) \
			p++;

#endif /* _PG_PROCTAB_H_ */
