/*
 * genutils.h
 *
 * General utility functions
 * 
 * Joe Conway <joe@crunchydata.com>
 *
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2020-2022 Crunchy Data Solutions, Inc.
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

#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if PG_VERSION_NUM >= 110000
#include "catalog/pg_collation_d.h"
#include "catalog/pg_type_d.h"
#else
#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#endif
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/array.h"
#include "utils/builtins.h"
#if PG_VERSION_NUM < 130000
/* currently in builtins.h but locally defined prior to pg13 */
#define MAXINT8LEN              25
#endif /* PG_VERSION_NUM < 130000 */
#include "utils/guc_tables.h"
#include "utils/lsyscache.h"
#include "utils/numeric.h"

#include "fileutils.h"
#include "genutils.h"
#include "parseutils.h"
#include "srfsigs.h"

static int guc_var_compare(const void *a, const void *b);
static int guc_name_compare(const char *namea, const char *nameb);

#if PG_VERSION_NUM < 140000
static Numeric
int64_to_numeric(int64 v)
{
        Datum           d = Int64GetDatum(v);

        return DatumGetNumeric(DirectFunctionCall1(int8_numeric, d));
}
#endif /* PG_VERSION_NUM < 140000 */

#if PG_VERSION_NUM < 130000
/*
 * A table of all two-digit numbers. This is used to speed up decimal digit
 * generation by copying pairs of digits into the final output.
 */
static const char DIGIT_TABLE[200] =
"00" "01" "02" "03" "04" "05" "06" "07" "08" "09"
"10" "11" "12" "13" "14" "15" "16" "17" "18" "19"
"20" "21" "22" "23" "24" "25" "26" "27" "28" "29"
"30" "31" "32" "33" "34" "35" "36" "37" "38" "39"
"40" "41" "42" "43" "44" "45" "46" "47" "48" "49"
"50" "51" "52" "53" "54" "55" "56" "57" "58" "59"
"60" "61" "62" "63" "64" "65" "66" "67" "68" "69"
"70" "71" "72" "73" "74" "75" "76" "77" "78" "79"
"80" "81" "82" "83" "84" "85" "86" "87" "88" "89"
"90" "91" "92" "93" "94" "95" "96" "97" "98" "99";

static int pg_ulltoa_n(uint64 value, char *a);

#if PG_VERSION_NUM >= 120000
#include "port/pg_bitutils.h"
#else

/*
 * Array giving the position of the left-most set bit for each possible
 * byte value.  We count the right-most position as the 0th bit, and the
 * left-most the 7th bit.  The 0th entry of the array should not be used.
 *
 * Note: this is not used by the functions in pg_bitutils.h when
 * HAVE__BUILTIN_CLZ is defined, but we provide it anyway, so that
 * extensions possibly compiled with a different compiler can use it.
 */
static const uint8 pg_leftmost_one_pos[256] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

/*
 * pg_leftmost_one_pos64
 *		As above, but for a 64-bit word.
 */
static inline int
pg_leftmost_one_pos64(uint64 word)
{
#ifdef HAVE__BUILTIN_CLZ
	Assert(word != 0);

#if defined(HAVE_LONG_INT_64)
	return 63 - __builtin_clzl(word);
#elif defined(HAVE_LONG_LONG_INT_64)
	return 63 - __builtin_clzll(word);
#else
#error must have a working 64-bit integer datatype
#endif
#else							/* !HAVE__BUILTIN_CLZ */
	int			shift = 64 - 8;

	Assert(word != 0);

	while ((word >> shift) == 0)
		shift -= 8;

	return shift + pg_leftmost_one_pos[(word >> shift) & 255];
#endif							/* HAVE__BUILTIN_CLZ */
}
#endif /* PG_VERSION_NUM >= 120000 */

static inline int
decimalLength64(const uint64 v)
{
	int			t;
	static const uint64 PowersOfTen[] = {
		UINT64CONST(1), UINT64CONST(10),
		UINT64CONST(100), UINT64CONST(1000),
		UINT64CONST(10000), UINT64CONST(100000),
		UINT64CONST(1000000), UINT64CONST(10000000),
		UINT64CONST(100000000), UINT64CONST(1000000000),
		UINT64CONST(10000000000), UINT64CONST(100000000000),
		UINT64CONST(1000000000000), UINT64CONST(10000000000000),
		UINT64CONST(100000000000000), UINT64CONST(1000000000000000),
		UINT64CONST(10000000000000000), UINT64CONST(100000000000000000),
		UINT64CONST(1000000000000000000), UINT64CONST(10000000000000000000)
	};

	/*
	 * Compute base-10 logarithm by dividing the base-2 logarithm by a
	 * good-enough approximation of the base-2 logarithm of 10
	 */
	t = (pg_leftmost_one_pos64(v) + 1) * 1233 / 4096;
	return t + (v >= PowersOfTen[t]);
}

#endif /* PG_VERSION_NUM < 130000 */

/*
 * Convert a 2D array of strings into a tuplestore and return it
 * as an SRF result.
 * 
 * fcinfo is the called SQL facing function call info
 * values is the 2D array of strings to convert
 * nrow and ncol provide the array dimensions
 * dtypes is an array of data type oids for the output tuple
 * 
 * If nrow is 0 or values is NULL, return an empty tuplestore
 * to the caller (empty result set).
 */
Datum
form_srf(FunctionCallInfo fcinfo, char ***values, int nrow, int ncol, Oid *dtypes)
{
	ReturnSetInfo	   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	Tuplestorestate	   *tupstore;
	HeapTuple			tuple;
	TupleDesc			tupdesc;
	AttInMetadata	   *attinmeta;
	MemoryContext		per_query_ctx;
	MemoryContext		oldcontext;
	int					i;

	/* check to see if caller supports us returning a tuplestore */
	if (!rsinfo || !(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("materialize mode required, but it is not "
						"allowed in this context")));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* get the requested return tuple description */
	tupdesc = CreateTupleDescCopy(rsinfo->expectedDesc);

	/*
	 * Check to make sure we have a reasonable tuple descriptor
	 */
	if (tupdesc->natts != ncol)
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("query-specified return tuple and "
						"function return type are not compatible"),
				 errdetail("Number of columns mismatch")));
	}
	else
	{
		for (i = 0; i < ncol; ++i)
		{
			Oid		tdtyp = TupleDescAttr(tupdesc, i)->atttypid;

			if (tdtyp != dtypes[i])
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("query-specified return tuple and "
							"function return type are not compatible"),
					 errdetail("Expected %s, got %s", format_type_be(dtypes[i]), format_type_be(tdtyp))));
		}
	}

	/* OK to use it */
	attinmeta = TupleDescGetAttInMetadata(tupdesc);

	/* let the caller know we're sending back a tuplestore */
	rsinfo->returnMode = SFRM_Materialize;

	/* initialize our tuplestore */
	tupstore = tuplestore_begin_heap(true, false, work_mem);

	if (nrow > 0 && values != NULL)
	{
		for (i = 0; i < nrow; ++i)
		{
			char	   **rowvals = values[i];

			tuple = BuildTupleFromCStrings(attinmeta, rowvals);
			tuplestore_puttuple(tupstore, tuple);
		}
	}

	/*
	 * no longer need the tuple descriptor reference created by
	 * TupleDescGetAttInMetadata()
	 */
	ReleaseTupleDesc(tupdesc);

	tuplestore_donestoring(tupstore);
	rsinfo->setResult = tupstore;

	/*
	 * SFRM_Materialize mode expects us to return a NULL Datum. The actual
	 * tuples are in our tuplestore and passed back through rsinfo->setResult.
	 * rsinfo->setDesc is set to the tuple description that we actually used
	 * to build our tuples with, so the caller can verify we did what it was
	 * expecting.
	 */
	rsinfo->setDesc = tupdesc;
	MemoryContextSwitchTo(oldcontext);

	return (Datum) 0;
}

/*
 * Convert multiline scalar file into setof scalar resultset
 */
Datum
setof_scalar_internal(FunctionCallInfo fcinfo, char *fqpath, Oid *srf_sig)
{
	char	  **lines;
	int			nrow;
	int			ncol = 1;

	lines = read_nlsv(fqpath, &nrow);
	if (nrow > 0)
	{
		char	 ***values;
		int			i;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			values[i] = (char **) palloc(ncol * sizeof(char *));

			/* if bigint, deal with "max" */
			if (srf_sig[0] == INT8OID &&
				strcasecmp(lines[i], "max") == 0)
			{
				char		buf[MAXINT8LEN + 1];
				int			len;

#if PG_VERSION_NUM >= 140000
				len = pg_lltoa(PG_INT64_MAX, buf) + 1;
#else
				pg_lltoa(PG_INT64_MAX, buf);
				len = strlen(buf) + 1;
#endif
				values[i][0] = palloc(len);
				memcpy(values[i][0], buf, len);
			}
			else
				values[i][0] = pstrdup(lines[i]);
		}

		return form_srf(fcinfo, values, nrow, ncol, srf_sig);
	}
	else
	{
		/* return empty result set */
		return form_srf(fcinfo, NULL, 0, ncol, srf_sig);
	}
}

/* return simple, one dimensional array */
Datum
string_get_array_datum(char **values, int nvals, Oid typelem, bool *isnull)
{
	const char *value;
	int16		typlen;
	bool		typbyval;
	char		typdelim;
	Oid			typinput,
				typioparam;
	FmgrInfo	in_func;
	char		typalign;
	int			i;
	Datum	   *dvalues = NULL;
	ArrayType  *arr;

	if (nvals == 0)
	{
		*isnull = true;
		return (Datum) 0;
	}
	else
		*isnull = false;

	/*
	 * get the element type's in_func
	 */
	get_type_io_data(typelem, IOFunc_input, &typlen, &typbyval,
					 &typalign, &typdelim, &typioparam, &typinput);
	fmgr_info(typinput, &in_func);

	dvalues = (Datum *) palloc(nvals * sizeof(Datum));

	for (i = 0; i < nvals; i++)
	{
		value = values[i];

		dvalues[i] = FunctionCall1(&in_func, CStringGetDatum(value));
	}

	arr = construct_array(dvalues, nvals,
						  typelem, typlen, typbyval, typalign);

	return PointerGetDatum(arr);
}

/* qsort comparison function for int64 */
int
int64_cmp(const void *p1, const void *p2)
{
	int64	v1 = *((const int64 *) p1);
	int64	v2 = *((const int64 *) p2);

	if (v1 < v2)
		return -1;
	if (v1 > v2)
		return 1;
	return 0;
}

/*
 * Functions for obtaining the context within which we are operating
 */
/*
 * Look up GUC option NAME. If it exists, return a pointer to its record,
 * else return NULL. This is cribbed from guc.c -- unfortunately there
 * seems to be no exported functionality to get the entire record by name.
 */
struct config_generic *
find_option(const char *name)
{
	const char			  **key = &name;
	struct config_generic **res;
	struct config_generic **guc_vars;
	int                     numOpts;

	Assert(name);

	guc_vars = get_guc_variables();
	numOpts = GetNumConfigOptions();

	/*
	 * By equating const char ** with struct config_generic *, we are assuming
	 * the name field is first in config_generic.
	 */
	res = (struct config_generic **) bsearch((void *) &key,
											 (void *) guc_vars,
											 numOpts,
											 sizeof(struct config_generic *),
											 guc_var_compare);
	if (res)
		return *res;

	/* Unknown name */
	return NULL;
}

/*
 * Additional utility functions cribbed from guc.c
 */

/*
 * comparator for qsorting and bsearching guc_variables array
 */
static int
guc_var_compare(const void *a, const void *b)
{
	const struct config_generic *confa = *(struct config_generic *const *) a;
	const struct config_generic *confb = *(struct config_generic *const *) b;

	return guc_name_compare(confa->name, confb->name);
}

/*
 * the bare comparison function for GUC names
 */
static int
guc_name_compare(const char *namea, const char *nameb)
{
	/*
	 * The temptation to use strcasecmp() here must be resisted, because the
	 * array ordering has to remain stable across setlocale() calls. So, build
	 * our own with a simple ASCII-only downcasing.
	 */
	while (*namea && *nameb)
	{
		char		cha = *namea++;
		char		chb = *nameb++;

		if (cha >= 'A' && cha <= 'Z')
			cha += 'a' - 'A';
		if (chb >= 'A' && chb <= 'Z')
			chb += 'a' - 'A';
		if (cha != chb)
			return cha - chb;
	}
	if (*namea)
		return 1;				/* a is longer */
	if (*nameb)
		return -1;				/* b is longer */
	return 0;
}

char *
int64_to_string(int64 val)
{
	char		buf[MAXINT8LEN + 1];
	int			len;
	char	   *value;

#if PG_VERSION_NUM >= 140000
	len = pg_lltoa(val, buf) + 1;
#else
	pg_lltoa(val, buf);
	len = strlen(buf) + 1;
#endif
	value = palloc(len);
	memcpy(value, buf, len);

	return value;
}

/*
 * pg_ulltoa: converts an unsigned 64-bit integer to its string representation and
 * returns strlen(a). Copied and modified version of PostgreSQL's pg_lltoa().
 *
 * Caller must ensure that 'a' points to enough memory to hold the result
 * (at least MAXINT8LEN + 1 bytes, counting a trailing NUL).
 */
int
pg_ulltoa(uint64 uvalue, char *a)
{
	int		len = 0;

	len += pg_ulltoa_n(uvalue, a);
	a[len] = '\0';
	return len;
}

char *
uint64_to_string(uint64 val)
{
	char		buf[MAXINT8LEN + 1];
	int			len;
	char	   *value;

	len = pg_ulltoa(val, buf) + 1;
	value = palloc(len);
	memcpy(value, buf, len);

	return value;
}

#if PG_VERSION_NUM < 90600
/*
 * Convert a human-readable size to a size in bytes.
 * Copied from src/backend/utils/adt/dbsize.c and
 * modified to take a simple char* string and return
 * int64 instead of Datum.
 */
int64
size_bytes(char *str)
{
	char	   *strptr,
			   *endptr;
	char		saved_char;
	Numeric		num;
	int64		result;
	bool		have_digits = false;

	/* Skip leading whitespace */
	strptr = str;
	while (isspace((unsigned char) *strptr))
		strptr++;

	/* Check that we have a valid number and determine where it ends */
	endptr = strptr;

	/* Part (1): sign */
	if (*endptr == '-' || *endptr == '+')
		endptr++;

	/* Part (2): main digit string */
	if (isdigit((unsigned char) *endptr))
	{
		have_digits = true;
		do
			endptr++;
		while (isdigit((unsigned char) *endptr));
	}

	/* Part (3): optional decimal point and fractional digits */
	if (*endptr == '.')
	{
		endptr++;
		if (isdigit((unsigned char) *endptr))
		{
			have_digits = true;
			do
				endptr++;
			while (isdigit((unsigned char) *endptr));
		}
	}

	/* Complain if we don't have a valid number at this point */
	if (!have_digits)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid size: \"%s\"", str)));

	/* Part (4): optional exponent */
	if (*endptr == 'e' || *endptr == 'E')
	{
		long		exponent;
		char	   *cp;

		/*
		 * Note we might one day support EB units, so if what follows 'E'
		 * isn't a number, just treat it all as a unit to be parsed.
		 */
		exponent = strtol(endptr + 1, &cp, 10);
		(void) exponent;		/* Silence -Wunused-result warnings */
		if (cp > endptr + 1)
			endptr = cp;
	}

	/*
	 * Parse the number, saving the next character, which may be the first
	 * character of the unit string.
	 */
	saved_char = *endptr;
	*endptr = '\0';

	num = DatumGetNumeric(DirectFunctionCall3(numeric_in,
											  CStringGetDatum(strptr),
											  ObjectIdGetDatum(InvalidOid),
											  Int32GetDatum(-1)));

	*endptr = saved_char;

	/* Skip whitespace between number and unit */
	strptr = endptr;
	while (isspace((unsigned char) *strptr))
		strptr++;

	/* Handle possible unit */
	if (*strptr != '\0')
	{
		int64		multiplier = 0;

		/* Trim any trailing whitespace */
		endptr = str + strlen(str) - 1;

		while (isspace((unsigned char) *endptr))
			endptr--;

		endptr++;
		*endptr = '\0';

		/* Parse the unit case-insensitively */
		if (pg_strcasecmp(strptr, "bytes") == 0)
			multiplier = (int64) 1;
		else if (pg_strcasecmp(strptr, "kb") == 0)
			multiplier = (int64) 1024;
		else if (pg_strcasecmp(strptr, "mb") == 0)
			multiplier = ((int64) 1024) * 1024;

		else if (pg_strcasecmp(strptr, "gb") == 0)
			multiplier = ((int64) 1024) * 1024 * 1024;

		else if (pg_strcasecmp(strptr, "tb") == 0)
			multiplier = ((int64) 1024) * 1024 * 1024 * 1024;

		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid size: \"%s\"", str),
					 errdetail("Invalid size unit: \"%s\".", strptr),
					 errhint("Valid units are \"bytes\", \"kB\", \"MB\", \"GB\", and \"TB\".")));

		if (multiplier > 1)
		{
			Numeric		mul_num;

			mul_num = DatumGetNumeric(DirectFunctionCall1(int8_numeric,
														  Int64GetDatum(multiplier)));

			num = DatumGetNumeric(DirectFunctionCall2(numeric_mul,
													  NumericGetDatum(mul_num),
													  NumericGetDatum(num)));
		}
	}

	result = DatumGetInt64(DirectFunctionCall1(numeric_int8,
											   NumericGetDatum(num)));

	return result;
}
#endif

#if PG_VERSION_NUM < 130000
/*
 * Get the decimal representation, not NUL-terminated, and return the length of
 * same.  Caller must ensure that a points to at least MAXINT8LEN bytes.
 * Pulled from PostgreSQL 13 numutils.c since it does not exist before that.
 */
static int
pg_ulltoa_n(uint64 value, char *a)
{
	int			olength,
				i = 0;
	uint32		value2;

	/* Degenerate case */
	if (value == 0)
	{
		*a = '0';
		return 1;
	}

	olength = decimalLength64(value);

	/* Compute the result string. */
	while (value >= 100000000)
	{
		const uint64 q = value / 100000000;
		uint32		value2 = (uint32) (value - 100000000 * q);

		const uint32 c = value2 % 10000;
		const uint32 d = value2 / 10000;
		const uint32 c0 = (c % 100) << 1;
		const uint32 c1 = (c / 100) << 1;
		const uint32 d0 = (d % 100) << 1;
		const uint32 d1 = (d / 100) << 1;

		char	   *pos = a + olength - i;

		value = q;

		memcpy(pos - 2, DIGIT_TABLE + c0, 2);
		memcpy(pos - 4, DIGIT_TABLE + c1, 2);
		memcpy(pos - 6, DIGIT_TABLE + d0, 2);
		memcpy(pos - 8, DIGIT_TABLE + d1, 2);
		i += 8;
	}

	/* Switch to 32-bit for speed */
	value2 = (uint32) value;

	if (value2 >= 10000)
	{
		const uint32 c = value2 - 10000 * (value2 / 10000);
		const uint32 c0 = (c % 100) << 1;
		const uint32 c1 = (c / 100) << 1;

		char	   *pos = a + olength - i;

		value2 /= 10000;

		memcpy(pos - 2, DIGIT_TABLE + c0, 2);
		memcpy(pos - 4, DIGIT_TABLE + c1, 2);
		i += 4;
	}
	if (value2 >= 100)
	{
		const uint32 c = (value2 % 100) << 1;
		char	   *pos = a + olength - i;

		value2 /= 100;

		memcpy(pos - 2, DIGIT_TABLE + c, 2);
		i += 2;
	}
	if (value2 >= 10)
	{
		const uint32 c = value2 << 1;
		char	   *pos = a + olength - i;

		memcpy(pos - 2, DIGIT_TABLE + c, 2);
	}
	else
		*a = (char) ('0' + value2);

	return olength;
}

#endif /* PG_VERSION_NUM < 130000 */

/*
 * Convert number of kernel pages to size in bytes
 */
PG_FUNCTION_INFO_V1(pgnodemx_pages_to_bytes);
Datum
pgnodemx_pages_to_bytes(PG_FUNCTION_ARGS)
{
	Numeric		num  = int64_to_numeric(sysconf(_SC_PAGESIZE));

	PG_RETURN_DATUM(DirectFunctionCall2(numeric_mul,
										PG_GETARG_DATUM(0),
										NumericGetDatum(num)));
}

/*
 * Return the currently running executable full path
 */
PG_FUNCTION_INFO_V1(pgnodemx_exec_path);
Datum pgnodemx_exec_path(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(my_exec_path));
}

#define INTEGER_LEN 64
/*
 * pgnodemx version of stat a file
 */
PG_FUNCTION_INFO_V1(pgnodemx_stat_file);
Datum pgnodemx_stat_file(PG_FUNCTION_ARGS)
{
	int				nrow = 1;
	int				ncol = 5;
	char		 ***values = (char ***) palloc(nrow * sizeof(char **));
	text		   *filename_t = PG_GETARG_TEXT_PP(0);
	char		   *filename;
	struct stat		fst;
	mode_t			st_mode;        /* File type and mode */
	uid_t			st_uid;         /* User ID of owner */
	gid_t			st_gid;         /* Group ID of owner */
	char			buf[INTEGER_LEN];
	char		   *uidstr;
	char		   *gidstr;
	char		   *modestr;
	char		   *username;
	char		   *groupname;
	struct passwd  *pwd;
	struct group   *grp;

	filename = convert_and_check_filename(filename_t, true);

	/* stat the file */
	if (stat(filename, &fst) < 0)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not stat file \"%s\": %m", filename)));
	}

	st_mode = fst.st_mode;
	st_uid = fst.st_uid;
	st_gid = fst.st_gid;

	/* get uid string and username */
	snprintf(buf, INTEGER_LEN, "%" PRIuMAX, (uintmax_t) st_uid);
	uidstr = pstrdup(buf);
	pwd = getpwuid(st_uid);
	if (pwd == NULL)
		username = NULL;
	else
		username = pstrdup(pwd->pw_name);

	/* get gid string and groupname */
	snprintf(buf, INTEGER_LEN, "%" PRIuMAX, (uintmax_t) st_gid);
	gidstr = pstrdup(buf);
	grp = getgrgid(st_gid);
    if (grp == NULL)
		groupname = NULL;
	else
		groupname = pstrdup(grp->gr_name);

	/* get mode string */
	snprintf(buf, INTEGER_LEN, "%o", st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
	modestr = pstrdup(buf);

	values[0] = (char **) palloc(ncol * sizeof(char *));

	/* uid and username for file */
	values[0][0] = uidstr;
	values[0][1] = username;

	/* gid and groupname for file */
	values[0][2] = gidstr;
	values[0][3] = groupname;

	/* one mode */
	values[0][4] = modestr;

	return form_srf(fcinfo, values, nrow, ncol, num_text_num_2_text_sig);
}
