/*
 * parseutils.c
 *
 * Functions specific to parsing various common string formats
 * 
 * Joe Conway <joe@crunchydata.com>
 *
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2020-2021 Crunchy Data Solutions, Inc.
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

#if PG_VERSION_NUM >= 120000
#include "utils/float.h"
#else
#include "utils/builtins.h"
#endif
#include "utils/int8.h"

#include "fileutils.h"
#include "kdapi.h"
#include "parseutils.h"

#if PG_VERSION_NUM < 90600
#include <math.h>

/* Convenience macro: set *have_error flag (if provided) or throw error */
#define RETURN_ERROR(throw_error, have_error) \
do { \
	if (have_error) { \
		*have_error = true; \
		return 0.0; \
	} else { \
		throw_error; \
	} \
} while (0)

/*
 * float8in_internal_opt_error - guts of float8in()
 *
 * This is exposed for use by functions that want a reasonably
 * platform-independent way of inputting doubles.  The behavior is
 * essentially like strtod + ereport on error, but note the following
 * differences:
 * 1. Both leading and trailing whitespace are skipped.
 * 2. If endptr_p is NULL, we throw error if there's trailing junk.
 * Otherwise, it's up to the caller to complain about trailing junk.
 * 3. In event of a syntax error, the report mentions the given type_name
 * and prints orig_string as the input; this is meant to support use of
 * this function with types such as "box" and "point", where what we are
 * parsing here is just a substring of orig_string.
 *
 * "num" could validly be declared "const char *", but that results in an
 * unreasonable amount of extra casting both here and in callers, so we don't.
 *
 * When "*have_error" flag is provided, it's set instead of throwing an
 * error.  This is helpful when caller need to handle errors by itself.
 */
static double
float8in_internal_opt_error(char *num, char **endptr_p,
							const char *type_name, const char *orig_string,
							bool *have_error)
{
	double		val;
	char	   *endptr;

	if (have_error)
		*have_error = false;

	/* skip leading whitespace */
	while (*num != '\0' && isspace((unsigned char) *num))
		num++;

	/*
	 * Check for an empty-string input to begin with, to avoid the vagaries of
	 * strtod() on different platforms.
	 */
	if (*num == '\0')
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							  errmsg("invalid input syntax for type %s: \"%s\"",
									 type_name, orig_string))),
					 have_error);

	errno = 0;
	val = strtod(num, &endptr);

	/* did we not see anything that looks like a double? */
	if (endptr == num || errno != 0)
	{
		int			save_errno = errno;

		/*
		 * C99 requires that strtod() accept NaN, [+-]Infinity, and [+-]Inf,
		 * but not all platforms support all of these (and some accept them
		 * but set ERANGE anyway...)  Therefore, we check for these inputs
		 * ourselves if strtod() fails.
		 *
		 * Note: C99 also requires hexadecimal input as well as some extended
		 * forms of NaN, but we consider these forms unportable and don't try
		 * to support them.  You can use 'em if your strtod() takes 'em.
		 */
		if (pg_strncasecmp(num, "NaN", 3) == 0)
		{
			val = get_float8_nan();
			endptr = num + 3;
		}
		else if (pg_strncasecmp(num, "Infinity", 8) == 0)
		{
			val = get_float8_infinity();
			endptr = num + 8;
		}
		else if (pg_strncasecmp(num, "+Infinity", 9) == 0)
		{
			val = get_float8_infinity();
			endptr = num + 9;
		}
		else if (pg_strncasecmp(num, "-Infinity", 9) == 0)
		{
			val = -get_float8_infinity();
			endptr = num + 9;
		}
		else if (pg_strncasecmp(num, "inf", 3) == 0)
		{
			val = get_float8_infinity();
			endptr = num + 3;
		}
		else if (pg_strncasecmp(num, "+inf", 4) == 0)
		{
			val = get_float8_infinity();
			endptr = num + 4;
		}
		else if (pg_strncasecmp(num, "-inf", 4) == 0)
		{
			val = -get_float8_infinity();
			endptr = num + 4;
		}
		else if (save_errno == ERANGE)
		{
			/*
			 * Some platforms return ERANGE for denormalized numbers (those
			 * that are not zero, but are too close to zero to have full
			 * precision).  We'd prefer not to throw error for that, so try to
			 * detect whether it's a "real" out-of-range condition by checking
			 * to see if the result is zero or huge.
			 *
			 * On error, we intentionally complain about double precision not
			 * the given type name, and we print only the part of the string
			 * that is the current number.
			 */
			if (val == 0.0 || val >= HUGE_VAL || val <= -HUGE_VAL)
			{
				char	   *errnumber = pstrdup(num);

				errnumber[endptr - num] = '\0';
				RETURN_ERROR(ereport(ERROR,
									 (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
									  errmsg("\"%s\" is out of range for type double precision",
											 errnumber))),
							 have_error);
			}
		}
		else
			RETURN_ERROR(ereport(ERROR,
								 (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								  errmsg("invalid input syntax for type "
										 "%s: \"%s\"",
										 type_name, orig_string))),
						 have_error);
	}
#ifdef HAVE_BUGGY_SOLARIS_STRTOD
	else
	{
		/*
		 * Many versions of Solaris have a bug wherein strtod sets endptr to
		 * point one byte beyond the end of the string when given "inf" or
		 * "infinity".
		 */
		if (endptr != num && endptr[-1] == '\0')
			endptr--;
	}
#endif							/* HAVE_BUGGY_SOLARIS_STRTOD */

	/* skip trailing whitespace */
	while (*endptr != '\0' && isspace((unsigned char) *endptr))
		endptr++;

	/* report stopping point if wanted, else complain if not end of string */
	if (endptr_p)
		*endptr_p = endptr;
	else if (*endptr != '\0')
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							  errmsg("invalid input syntax for type "
									 "%s: \"%s\"",
									 type_name, orig_string))),
					 have_error);

	return val;
}

/*
 * Interface to float8in_internal_opt_error() without "have_error" argument.
 */
static double
float8in_internal(char *num, char **endptr_p,
				  const char *type_name, const char *orig_string)
{
	return float8in_internal_opt_error(num, endptr_p, type_name,
									   orig_string, NULL);
}
#endif

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
	const char  quote = '"';

	Assert(source != NULL);
	Assert(*source != NULL);
	Assert(quote != '\0');

	src = *source;
	ret = dst = palloc0(strlen(src));

	if (*src && *src == quote)
		src++;					/* skip leading quote */

	while (*src)
	{
		char		c = *src;

		if (lastSlash)
		{
			switch (c) {
				case '\\':
					*dst++ = '\\';
					break;
				case 'n':
					*dst++ = '\n';
					break;
				case 't':
					*dst++ = '\t';
					break;
				case quote:
					*dst++ = quote;
					break;
				default:			/* unrecognized escape just pass through; XXX: add back in the slash? */
					*dst++ = *src++;
					break;
			}
			lastSlash = false;
		}
		else
		{
			lastSlash = (c == '\\');
			
			if (c == quote && src[1] == '\0')
				break;				/* skip trailing quote */

			if (!lastSlash)
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

	rawstr = read_one_nlsv(ftr);

	/* cgroup v2 reports literal "max" instead of largest possible value */
	if (strcasecmp(rawstr, "max") == 0)
		result = PG_INT64_MAX;
	else
	{
		success = scanint8(rawstr, true, &result);
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
		result = float8in_internal(rawstr, NULL, "double precision", rawstr);

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
