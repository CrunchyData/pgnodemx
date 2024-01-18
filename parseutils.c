/*
 * parseutils.c
 *
 * Functions specific to parsing various common string formats
 *
 * Joe Conway <joe@crunchydata.com>
 *
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2020-2024 Crunchy Data Solutions, Inc.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL CRUNCHY DATA SOLUTIONS, INC. BE LIABLE TO ANY PARTY
 * FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
 * INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE CRUNCHY DATA SOLUTIONS, INC. HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE CRUNCHY DATA SOLUTIONS, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE CRUNCHY DATA SOLUTIONS, INC. HAS NO
 * OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
 * MODIFICATIONS.
 */

#include "postgres.h"

#include <float.h>

#if PG_VERSION_NUM < 150000
#include "utils/int8.h"
#endif
#if PG_VERSION_NUM >= 120000
#include "utils/float.h"
#else
#include "utils/builtins.h"
#endif
#include "mb/pg_wchar.h"
#include "fileutils.h"
#include "kdapi.h"
#include "parseutils.h"

#define is_hex_digit(ch) ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))
#define hex_value(ch) ((ch >= '0' && ch <= '9') ? (ch & 0x0F) : (ch & 0x0F) + 9) /* assumes ascii */

/*
 * Funtions to parse the various virtual file output formats.
 * See https://www.kernel.org/doc/Documentation/cgroup-v2.txt
 * for examples of the types of output formats to be parsed.
 */

/*
 * Read lines from a "new-line separated values" virtual file. Returns
 * the lines as an array of strings (char *), and populates nlines
 * with the line count.
 */
char **
read_nlsv(char *ftr, int *nlines)
{
	char   *rawstr = read_vfs(ftr);
	char   *token;
	char  **lines = (char **) palloc(0);

	*nlines = 0;
	for (token = strtok(rawstr, "\n"); token; token = strtok(NULL, "\n"))
	{
		lines = repalloc(lines, (*nlines + 1) * sizeof(char *));
		lines[*nlines] = pstrdup(token);
		*nlines += 1;
	}

	return lines;
}

/*
 * Read one value from a "new-line separated values" virtual file
 */
char *
read_one_nlsv(char *ftr)
{
	int		nlines;
	char  **lines = read_nlsv(ftr, &nlines);

	if (nlines != 1)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: expected 1, got %d, lines from file %s", nlines, ftr)));

	return lines[0];
}

/*
 * Parse columns from a "nested keyed" virtual file line
 */
kvpairs *
parse_nested_keyed_line(char *line)
{
	char			   *token;
	char			   *lstate;
	char			   *subtoken;
	char			   *cstate;
	kvpairs			   *nkl = (kvpairs *) palloc(sizeof(kvpairs));

	nkl->nkvp = 0;
	nkl->keys = (char **) palloc(0);
	nkl->values = (char **) palloc(0);

	for (token = strtok_r(line, " ", &lstate); token; token = strtok_r(NULL, " ", &lstate))
	{
		nkl->keys = repalloc(nkl->keys, (nkl->nkvp + 1) * sizeof(char *));
		nkl->values = repalloc(nkl->values, (nkl->nkvp + 1) * sizeof(char *));

		if (nkl->nkvp > 0)
		{
			subtoken = strtok_r(token, "=", &cstate);
			if (subtoken)
				nkl->keys[nkl->nkvp] = pstrdup(subtoken);
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: missing key in nested keyed line")));

			subtoken = strtok_r(NULL, "=", &cstate);
			if (subtoken)
				nkl->values[nkl->nkvp] = pstrdup(subtoken);
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: missing value in nested keyed line")));
		}
		else
		{
			/* first column has value only (not in form key=value) */
			nkl->keys[nkl->nkvp] = pstrdup("key");
			nkl->values[nkl->nkvp] = pstrdup(token);
		}

		nkl->nkvp += 1;
	}

	return nkl;
}

/*
 * Parse tokens from a space separated line.
 * Return tokens and set ntok to number found.
 */
char **
parse_ss_line(char *line, int *ntok)
{
	char   *token;
	char   *lstate;
	char  **values = (char **) palloc(0);

	*ntok = 0;

	for (token = strtok_r(line, " ", &lstate); token; token = strtok_r(NULL, " ", &lstate))
	{
		values = (char **) repalloc(values, (*ntok + 1) * sizeof(char *));
		values[*ntok] = pstrdup(token);
		*ntok += 1;
	}

	return values;
}

/*
 * parse_quoted_string
 *
 * Remove quotes and escapes from the string at **source.  Returns a new palloc() string with the contents.
 *
 */
char*
parse_quoted_string(char **source)
{
	char	   *src;
	char	   *dst;
	char	   *ret;
	bool        lastSlash = false;

	Assert(source != NULL);
	Assert(*source != NULL);

	src = *source;
	ret = dst = palloc0(strlen(src));

	if (*src && *src == '"')
		src++;					/* skip leading quote */

	while (*src)
	{
		char		c = *src;
		pg_wchar	cp = 0;
		int			i;

		if (lastSlash)
		{
			switch (c) {
			case '\\':
				*dst++ = '\\';
				src++;
				break;
			case 'a':
				*dst++ = '\a';
				src++;
				break;
			case 'b':
				*dst++ = '\b';
				src++;
				break;
			case 'f':
				*dst++ = '\f';
				src++;
				break;
			case 'n':
				*dst++ = '\n';
				src++;
				break;
			case 'r':
				*dst++ = '\r';
				src++;
				break;
			case 't':
				*dst++ = '\t';
				src++;
				break;
			case 'v':
				*dst++ = '\v';
				src++;
				break;
			case '"':
				*dst++ = '"';
				src++;
				break;
			case 'x':
				/* next 2 chars are hex bytes */
				if (is_hex_digit(src[1]) && is_hex_digit(src[2]))
				{
					*dst++ = (hex_value(src[1])<<4) + hex_value(src[2]);
					src+=3;
				}
				else
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							 errmsg("malformed \\x literal")));
				break;
			case 'u':
			case 'U':
				/* unicode code point handling */
				for (i = 1; i <= (c == 'u' ? 4 : 8); i++)
				{
					if (is_hex_digit(src[i]))
					{
						cp <<= 4;
						cp += hex_value(src[i]);
					}
					else
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								 errmsg("malformed unicode literal")));
				}
				src += (c == 'u' ? 5 : 9);

				/* append our multibyte encoded codepoint */
				dst += pg_wchar2mb_with_len(&cp, dst, 1);

				break;
			default:			/* unrecognized escape just pass through */
				*dst++ = '\\';
				*dst++ = *src++;
				break;
			}
			lastSlash = false;
		}
		else
		{
			lastSlash = (c == '\\');

			if (c == '"' && src[1] == '\0')
			{
				src++;
				break;				/* skip trailing quote without copying */
			}

			if (lastSlash)
				src++;
			else
				*dst++ = *src++;

		}
	}

	*dst = '\0';
	*source = src;

	return ret;
}

/*
 * Parse tokens from a "key equals quoted value" line.
 * Examples (from Kubernetes Downward API):
 *
 *   cluster="test-cluster1"
 *   rack="rack-22"
 *   zone="us-est-coast"
 *   var="abc=123"
 *   multiline="multi\nline"
 *   quoted="{\"quoted\":\"json\"}"
 *
 * Return two tokens; strip the quotes around the second one.
 * If exactly two tokens are not found, throw an error.
 */
char **
parse_keqv_line(char *line)
{
	int    ntok = 0;
	char   *token;
	char   *lstate;
	char  **values = (char **) palloc(2 * sizeof(char *));

	/* find the initial key portion of the code */
	token = strtok_r(line, "=", &lstate);

	/* invalid will fall through */
	if (token)
	{
		values[ntok++] = pstrdup(token);

		/* punt the hard work to this routine */
		token = parse_quoted_string(&lstate);
		if (token)
		{
			values[ntok++] = pstrdup(token);

			/* if we have any extra chars, then it's actually a parse error */
			if (strlen(lstate))
			{
				ntok++;
			}
		}
	}

	/* line should have exactly two tokens */
	if (ntok != 2)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: incorrect format for key equals quoted value line"),
				errdetail("pgnodemx: expected 2 tokens, found %d", ntok)));

	return values;
}

/*
 * Read provided file to obtain one int64 value
 */
int64
get_int64_from_file(char *ftr)
{
	char	   *rawstr;
	bool		success = false;
	int64		result;
#if PG_VERSION_NUM >= 150000
	char       *endptr = NULL;
#endif

	rawstr = read_one_nlsv(ftr);

	/* cgroup v2 reports literal "max" instead of largest possible value */
	if (strcasecmp(rawstr, "max") == 0)
		result = PG_INT64_MAX;
	else
	{
#if PG_VERSION_NUM < 150000
		success = scanint8(rawstr, true, &result);
#else
		errno = 0;
		result = strtoi64(rawstr, &endptr, 10);
		if (errno == 0 && *endptr == '\0')
			success = true;
#endif
		if (!success)
			ereport(ERROR,
					(errcode_for_file_access(),
					errmsg("contents not an integer, file \"%s\"",
					ftr)));
	}

	return result;
}

/*
 * Read provided file to obtain one double precision value
 */
double
get_double_from_file(char *ftr)
{
	char	   *rawstr = read_one_nlsv(ftr);
	double		result;

	/* cgroup v2 reports literal "max" instead of largest possible value */
	if (strcmp(rawstr, "max") == 0)
		result = DBL_MAX;
	else
        #if PG_VERSION_NUM < 160000
		result = float8in_internal(rawstr, NULL, "double precision", rawstr);
        #else
		result = float8in_internal(rawstr, NULL, "double precision", rawstr, NULL);
        #endif
	return result;
}

/*
 * Read provided file to obtain one string value
 */
char *
get_string_from_file(char *ftr)
{
	return read_one_nlsv(ftr);
}

/*
 * Parse a "space separated values" virtual file.
 * Must be exactly one line with tokens separated by a space.
 * Returns tokens as array of strings, and number of tokens
 * found in nvals.
 */
char **
parse_space_sep_val_file(char *ftr, int *nvals)
{
	char   *line;
	char   *token;
	char   *lstate;
	char  **values = (char **) palloc(0);

	line = read_one_nlsv(ftr);

	*nvals = 0;
	for (token = strtok_r(line, " ", &lstate); token; token = strtok_r(NULL, " ", &lstate))
	{
		values = repalloc(values, (*nvals + 1) * sizeof(char *));
		values[*nvals] = pstrdup(token);
		*nvals += 1;
	}

	return values;
}

/*
 * Parse a "key value" virtual file.
 * 
 * Must be one or more lines with 2 tokens separated by a space.
 * Returns tokens as array of strings, and number of tokens
 * found in nlines.
 * 
 * Currently makes no attempt to strip a trailing character from
 * the "key". Possibly that should be added later.
 */
char ***
read_kv_file(char *fname, int *nlines)
{
	char **lines = read_nlsv(fname, nlines);	

	if (nlines > 0)
	{
		char ***values;
		int		nrow = *nlines;
		int		ncol = 2;
		int		i;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			int	ntok;

			values[i] = parse_ss_line(lines[i], &ntok);
			/* line should have exactly ncol tokens */
			if (ntok != ncol)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: incorrect format for key value line"),
						errdetail("pgnodemx: expected 2 tokens, found %d, file %s", ntok, fname)));
		}
		return values;
	}
	return NULL;
}
