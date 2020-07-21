/*
 * parseutils.c
 *
 * Functions specific to parsing various common string formats
 * 
 * Joe Conway <joe@crunchydata.com>
 *
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2020 Crunchy Data Solutions, Inc.
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

#include "utils/float.h"
#include "utils/int8.h"

#include "fileutils.h"
#include "parseutils.h"

/*
 * Read lines from a "new-line separated values" virtual file. Returns
 * the lines as an array of strings (char *), and populates nlines
 * with the line count.
 */
char **
read_nlsv(char *ftr, int *nlines)
{
	char   *rawstr = read_vfs(ftr);
	char    *token;
	char   **lines = (char **) palloc(0);

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
 * Parse columns from a "flat keyed" virtual file line.
 * These lines must be exactly two tokens separated by a space.
 */
char **
parse_flat_keyed_line(char *line)
{
	char   *token;
	char   *lstate;
	char  **values = (char **) palloc(2 * sizeof(char *));
	int		ncol = 0;

	for (token = strtok_r(line, " ", &lstate); token; token = strtok_r(NULL, " ", &lstate))
	{
		if (ncol < 2)
			values[ncol] = pstrdup(token);
		else
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					errmsg("pgnodemx: too many tokens in flat keyed line")));

		ncol += 1;
	}

	if (ncol != 2)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: not enough tokens in flat keyed line")));

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

