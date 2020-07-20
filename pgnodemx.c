/*
 * pgnodemx
 *
 * SQL functions that allow capture of node OS metrics from PostgreSQL
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

#include <linux/magic.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "catalog/pg_authid.h"
#include "catalog/pg_type_d.h"
#include "fmgr.h"
#include "funcapi.h"
#include "lib/qunique.h"
#include "lib/stringinfo.h"
#include "miscadmin.h"
#include "port.h"
#include "storage/fd.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/guc_tables.h"
#include "utils/int8.h"
#include "utils/memutils.h"
#include "utils/varlena.h"

PG_MODULE_MAGIC;

#define PROC_CGROUP_FILE	"/proc/self/cgroup"
#define CGROUP_V1			"legacy"
#define CGROUP_V2			"unified"
#define CGROUP_HYBRID		"hybrid"
#define is_cgroup_v1		(strcmp(cgmode, CGROUP_V1) == 0)
#define is_cgroup_v2		(strcmp(cgmode, CGROUP_V2) == 0)
#define is_cgroup_hy		(strcmp(cgmode, CGROUP_HYBRID) == 0)

/* columns in memory pressure output */
#define MEM_PRESS_NCOL		5
/* columns in flat keyed output */
#define FLAT_KEYED_NCOL		2

typedef struct kvpairs
{
	int		nkvp;
	char  **keys;
	char  **values;
} kvpairs;

/* parsing functions */
static char *convert_and_check_filename(text *arg);
static char *read_vfs(char *ftr);
static char **read_nlsv(char *ftr, int *nlines);
static char *read_one_nlsv(char *ftr);
static kvpairs *parse_nested_keyed_line(char *line);
static char **parse_flat_keyed_line(char *line);
static int64 getInt64FromFile(char *ftr);
static char **parse_space_sep_val_file(char *filename, int *nvals);
static int int64_cmp(const void *p1, const void *p2);
static int cgmembers(int64 **pids);
static Datum get_scalar_int64(FunctionCallInfo fcinfo, char *basepath);

/* context gathering functions */
static struct config_generic *find_option(const char *name);
static int guc_var_compare(const void *a, const void *b);
static int guc_name_compare(const char *namea, const char *nameb);
static void set_containerized(void);
static void set_cgmode(void);
static void create_default_cgpath(char *str, int curlen);
static void init_or_reset_cgpath(void);
static void set_cgpath(void);
static char *get_cgpath_value(char *key);

/* utility functions */
static Datum form_srf(FunctionCallInfo fcinfo, char ***values,
					  int nrow, int ncol, Oid *dtypes);

/* exported functions */
void _PG_init(void);
extern Datum pgnodemx_cgroup_mode(PG_FUNCTION_ARGS);
extern Datum pgnodemx_cgroup_path(PG_FUNCTION_ARGS);
extern Datum pgnodemx_cgroup_process_count(PG_FUNCTION_ARGS);
extern Datum pgnodemx_memory_pressure(PG_FUNCTION_ARGS);
extern Datum pgnodemx_memstat_int64(PG_FUNCTION_ARGS);
extern Datum pgnodemx_keyed_memstat_int64(PG_FUNCTION_ARGS);

/* function return signatures */
static Oid cgpath_sig[] = {TEXTOID, TEXTOID};
static Oid mem_press_sig[] = {TEXTOID, FLOAT8OID, FLOAT8OID, FLOAT8OID, FLOAT8OID};
static Oid flat_keyed_int64_sig[] = {TEXTOID, INT8OID};

/* custom GUC vars */
static bool	containerized = false;
static char *cgrouproot = NULL;

/* module globals */
static char *cgmode = NULL;
static kvpairs *cgpath = NULL;

/*
 * Entrypoint of this module.
 */
void
_PG_init(void)
{
	/* Be sure we do initialization only once */
	static bool inited = false;

	if (inited)
		return;

	/* Must be loaded with shared_preload_libraries */
	if (!process_shared_preload_libraries_in_progress)
		ereport(ERROR, (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: must be loaded via shared_preload_libraries")));

	DefineCustomBoolVariable("pgnodemx.containerized",
							 "True if operating inside a container",
							 NULL, &containerized, false, PGC_POSTMASTER /* PGC_SIGHUP */,
							 0, NULL, NULL, NULL);

	DefineCustomStringVariable("pgnodemx.cgrouproot",
							   "Path to root cgroup",
							   NULL, &cgrouproot, "/sys/fs/cgroup", PGC_POSTMASTER /* PGC_SIGHUP */,
							   0, NULL, NULL, NULL);

	set_cgmode();

	/* must determine if containerized before setting cgpath */
	set_containerized();
	set_cgpath();

    inited = true;
}

/*
 * Funtions to parse the various virtual file output formats.
 * See https://www.kernel.org/doc/Documentation/cgroup-v2.txt
 * for examples of the types of output formats to be parsed.
 */

/*
 * Simplified/modified version of same named function in genfile.c.
 * Be careful not to call during _PG_init() because
 * is_member_of_role does not play nice with shared_preload_libraries.
 */
static char *
convert_and_check_filename(text *arg)
{
	char	   *filename;

	/* Limit use to members of the 'pg_monitor' role */
	if (!is_member_of_role(GetUserId(), DEFAULT_ROLE_MONITOR))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be member of pg_monitor role")));

	filename = text_to_cstring(arg);
	canonicalize_path(filename);	/* filename can change length here */

	/* Disallow absolute paths */
	if (is_absolute_path(filename))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("reference to absolute path not allowed")));

	/* Disallow references to parent directory */
	if (path_contains_parent_reference(filename))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("reference to parent directory (\"..\") not allowed")));

	return filename;
}

/*
 * read_vfs(): stripped down copy of read_binary_file() from
 * genfile.c
 */

/* Minimum amount to read at a time */
#define MIN_READ_SIZE 4096
static char *
read_vfs(char *filename)
{
	char		   *buf;
	size_t			nbytes = 0;
	FILE		   *file;
	StringInfoData	sbuf;

	if ((file = AllocateFile(filename, PG_BINARY_R)) == NULL)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\" for reading: %m",
						filename)));

	initStringInfo(&sbuf);

	while (!(feof(file) || ferror(file)))
	{
		size_t		rbytes;

		/*
		 * If not at end of file, and sbuf.len is equal to
		 * MaxAllocSize - 1, then either the file is too large, or
		 * there is nothing left to read. Attempt to read one more
		 * byte to see if the end of file has been reached. If not,
		 * the file is too large; we'd rather give the error message
		 * for that ourselves.
		 */
		if (sbuf.len == MaxAllocSize - 1)
		{
			char	rbuf[1]; 

			if (fread(rbuf, 1, 1, file) != 0 || !feof(file))
				ereport(ERROR,
						(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
						 errmsg("file length too large")));
			else
				break;
		}

		/* OK, ensure that we can read at least MIN_READ_SIZE */
		enlargeStringInfo(&sbuf, MIN_READ_SIZE);

		/*
		 * stringinfo.c likes to allocate in powers of 2, so it's likely
		 * that much more space is available than we asked for.  Use all
		 * of it, rather than making more fread calls than necessary.
		 */
		rbytes = fread(sbuf.data + sbuf.len, 1,
					   (size_t) (sbuf.maxlen - sbuf.len - 1), file);
		sbuf.len += rbytes;
		nbytes += rbytes;
	}

	/*
	 * Keep a trailing null in place, same as what
	 * appendBinaryStringInfo() would do.
	 */
	sbuf.data[sbuf.len] = '\0';

	/* Now we can commandeer the stringinfo's buffer as the result */
	buf = sbuf.data;

	if (ferror(file))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));

	FreeFile(file);

	return buf;
}

/*
 * Read lines from a "new-line separated values" virtual file. Returns
 * the lines as an array of strings (char *), and populates nlines
 * with the line count.
 */
static char **
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
static char *
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
static kvpairs *
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
static char **
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
static int64
getInt64FromFile(char *ftr)
{
	char	   *rawstr;
	bool		success = false;
	int64		result;

	rawstr = read_one_nlsv(ftr);

	/* cgroup v2 reports literal "max" instead of largest possible value */
	if (strcmp(rawstr, "max") == 0)
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
 * Parse a "space separated values" virtual file.
 * Must be exactly one line with tokens separated by a space.
 * Returns tokens as array of strings, and number of tokens
 * found in nvals.
 */
static char **
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

/* qsort comparison function for int64 */
static int
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
 * Find out all the pids in a cgroup.
 * 
 * In cgroup v2 cgroup.procs is not sorted or guaranteed unique.
 * Remedy that. If not NULL, *pids is set to point to a palloc'd
 * array containing distinct pids in sorted order. The length of
 * the array is the function result. Cribbed from aclmembers.
 */
static int
cgmembers(int64 **pids)
{
	int64	   *list;
	int			i;
	StringInfo	ftr = makeStringInfo();
	int			nlines;
	char	  **lines;

	appendStringInfo(ftr, "%s/%s", get_cgpath_value("default"), "cgroup.procs");
	lines = read_nlsv(ftr->data, &nlines);

	if (nlines == 0)
	{
		if (pids)
			*pids = NULL;
		return 0;
	}

	/* Allocate the worst-case space requirement */
	list = palloc(nlines * sizeof(int64));

	/*
	 * Walk the string array collecting PIDs.
	 */
	for (i = 0; i < nlines; i++)
	{
		bool	success = false;
		int64	result;

		success = scanint8(lines[i], true, &result);
		if (!success)
			ereport(ERROR,
					(errcode_for_file_access(),
					errmsg("contents not an integer, file \"%s\"",
					ftr->data)));

		list[i] = result;
	}

	/* Sort the array */
	qsort(list, nlines, sizeof(int64), int64_cmp);

	/*
	 * We could repalloc the array down to minimum size, but it's hardly worth
	 * it since it's only transient memory.
	 */
	if (pids)
		*pids = list;

	/* Remove duplicates from the array, returns new size */
	return qunique(list, nlines, sizeof(int64), int64_cmp);
}

/*
 * Get scalar int64 value from a virtual file
 */
static Datum
get_scalar_int64(FunctionCallInfo fcinfo, char *basepath)
{
	StringInfo	ftr = makeStringInfo();
	char	   *fname = convert_and_check_filename(PG_GETARG_TEXT_PP(0));
	int64		result;

	appendStringInfo(ftr, "%s/%s", basepath, fname);

	result = getInt64FromFile(ftr->data);

	PG_RETURN_INT64(result);
}


/*
 * Functions for obtaining the context within which we are operating
 */

/*
 * Look up GUC option NAME. If it exists, return a pointer to its record,
 * else return NULL. This is cribbed from guc.c -- unfortunately there
 * seems to be no exported functionality to get the entire record by name.
 */
static struct config_generic *
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

/*
 * Determine whether running inside a container.
 * 
 * Of particular interest to us is whether our cgroup vfs has been mounted
 * at /sys/fs/cgroup for us. Inside a container that is what we expect,
 * but outside of a container it will be where PROC_CGROUP_FILE tells
 * us to find it.
 */
static void
set_containerized(void)
{
	/*
	 * If containerized was explicitly set in postgresql.conf, allow that
	 * value to preside.
	 */
	struct config_generic *record;

	record = find_option("pgnodemx.containerized");
	if (record->source == PGC_S_FILE)
		return;

	/*
	 * Check to see if path referenced in PROC_CGROUP_FILE exists.
	 * If it does, we are presumably not in a container, else we are.
	 * In either case, the important distinction is whether we will
	 * find the controller files in that location. If the location
	 * does not exist the files are found under cgrouproot directly.
	 */
	if (is_cgroup_v1 || is_cgroup_v2)
	{
		StringInfo		str = makeStringInfo();

		/* cgroup v1 and v2 will have differences we need to account for */
		if (is_cgroup_v1)
		{
			int		nlines;
			char  **lines = read_nlsv(PROC_CGROUP_FILE, &nlines);
			if (nlines > 0)
			{
				int		i;

				for (i = 0; i < nlines; ++i)
				{
					/* use the memory controller path to test with */
					char   *line = lines[i];
					char   *p = strchr(line, ':') + 1;

					if (strncmp(p, "memory", 6) == 0)
					{
						p = strchr(p, ':') + 1;
						appendStringInfo(str, "%s/%s", cgrouproot, p);
						break;
					}
				}
			}
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: no cgroup paths found in file %s", PROC_CGROUP_FILE)));

		}
		else if (is_cgroup_v2)
		{
			char		   *rawstr;

			/* in cgroup v2 there should only be one entry */
			rawstr = read_one_nlsv(PROC_CGROUP_FILE);
			appendStringInfo(str, "%s/%s", cgrouproot, (rawstr + 4));
		}

		if (access(str->data, F_OK) != -1)
			containerized = false;
		else
			containerized = true;

		return;
	}
	else
	{
		/* hybrid mode; means not in a container */
		containerized = false;
		return;
	}
}

/*
 * Determine whether running with cgroup v1, v2, or systemd hybrid mode
 */
static void
set_cgmode(void)
{
	/*
	 * From: https://systemd.io/CGROUP_DELEGATION/
	 * 
	 * To detect which of three modes is currently used, use statfs()
	 * on /sys/fs/cgroup/. If it reports CGROUP2_SUPER_MAGIC in its
	 * .f_type field, then you are in unified mode. If it reports
	 * TMPFS_MAGIC then you are either in legacy or hybrid mode. To
	 * distinguish these two cases, run statfs() again on
	 * /sys/fs/cgroup/unified/. If that succeeds and reports
	 * CGROUP2_SUPER_MAGIC you are in hybrid mode, otherwise not. 
	 */
	struct statfs	buf;
	int				ret;
	MemoryContext	oldcontext;

	ret = statfs(cgrouproot, &buf);
	if (ret == -1)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				errmsg("pgnodemx: statfs error on cgroup mount %s: %m", cgrouproot)));
	}

	if (buf.f_type == CGROUP2_SUPER_MAGIC)					/* cgroup v2 */
	{
		oldcontext = MemoryContextSwitchTo(TopMemoryContext);
		cgmode = pstrdup(CGROUP_V2);
		MemoryContextSwitchTo(oldcontext);
		return;
	}
	else if (buf.f_type == TMPFS_MAGIC)
	{
		StringInfo		str = makeStringInfo();

		appendStringInfo(str, "%s/%s", cgrouproot, "unified");
		ret = statfs(str->data, &buf);

		oldcontext = MemoryContextSwitchTo(TopMemoryContext);
		if (ret == 0 && buf.f_type == CGROUP2_SUPER_MAGIC)	/* hybrid mode */
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					errmsg("pgnodemx: unsupported cgroup configuration")));

		else												/* cgroup v1 */
			cgmode = pstrdup(CGROUP_V1);
		MemoryContextSwitchTo(oldcontext);
		return;
	}
	else
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: unexpected mount type on cgroup root %s", cgrouproot)));
}

/*
 * Expand cgpath by one element and populate with a default
 * path. str is the path to use for the default and curlen
 * is the pre-expanded number of kv pairs.
 */
static void
create_default_cgpath(char *str, int curlen)
{
	/* add room */
	cgpath->nkvp = curlen + 1;
	cgpath->keys = (char **) repalloc(cgpath->keys, cgpath->nkvp * sizeof(char *));
	cgpath->values = (char **) repalloc(cgpath->values, cgpath->nkvp * sizeof(char *));

	/* create the default record */
	cgpath->keys[cgpath->nkvp - 1] = MemoryContextStrdup(TopMemoryContext, "default");
	cgpath->values[cgpath->nkvp - 1] = MemoryContextStrdup(TopMemoryContext, str);
}

static void
init_or_reset_cgpath(void)
{
	if (cgpath == NULL)
	{
		/* initialize in TopMemoryContext */
		cgpath = (kvpairs *) MemoryContextAlloc(TopMemoryContext, sizeof(kvpairs));
		cgpath->nkvp = 0;
		cgpath->keys = (char **) MemoryContextAlloc(TopMemoryContext, 0);
		cgpath->values = (char **) MemoryContextAlloc(TopMemoryContext, 0);
	}
	else
	{
		int		i;

		/* deep clear any existing info */
		for (i = 0; i < cgpath->nkvp; ++i)
		{
			if (cgpath->keys[i])
				pfree(cgpath->keys[i]);
			if (cgpath->values[i])
				pfree(cgpath->values[i]);
		}

		if (cgpath->keys)
			cgpath->keys = (char **) repalloc(cgpath->keys, 0);
		if (cgpath->values)
			cgpath->values = (char **) repalloc(cgpath->values, 0);
		cgpath->nkvp = 0;
	}
}

static void
set_cgpath(void)
{
	char		   *ftr = PROC_CGROUP_FILE;

	init_or_reset_cgpath();

	/* obtain a list of cgroup controllers */
	if (is_cgroup_v1)
	{
		/*
		 * In cgroup v1 the active controllers for the
		 * cgroup are listed in PROC_CGROUP_FILE. We will
		 * need to read these whether "containerized" or not,
		 * in order to get a complete list of controllers
		 * available.
		 */
		int				nlines;
		char		  **lines;
		StringInfo		str = makeStringInfo();
		int				i;
		char		   *defpath;

		lines = read_nlsv(PROC_CGROUP_FILE, &nlines);
		if (nlines == 0)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					errmsg("pgnodemx: no cgroup paths found in file %s", PROC_CGROUP_FILE)));

		cgpath->nkvp = nlines;
		cgpath->keys = (char **) repalloc(cgpath->keys, cgpath->nkvp * sizeof(char *));
		cgpath->values = (char **) repalloc(cgpath->values, cgpath->nkvp * sizeof(char *));

		for (i = 0; i < nlines; ++i)
		{
			/*
			 * The lines in PROC_CGROUP_FILE look like:
			 *       #:<controller>:/<relative_path>
			 * e.g.  2:memory:/foo/bar
			 * Sometimes the <controller> part is further divided
			 * into key-value, e.g. "name=systemd" in which case
			 * "systemd" actually corresponds to the directory name.
			 */
			char   *line = lines[i];
			char   *p = strchr(line, ':');
			char   *r = strchr(p, ':');
			char   *q;
			Size	len;
			char   *controller;

			if (p)
				p += 1;
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: malformed cgroup path found in file %s", PROC_CGROUP_FILE)));

			if (r)
				r += 1;
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: malformed cgroup path found in file %s", PROC_CGROUP_FILE)));

			len = ((r - p) - 1);

			controller = pnstrdup(p, len);
			q = strchr(controller, '=');
			if (q)
				controller = q + 1;

			resetStringInfo(str);

			if (!containerized)
			{
				/*
				 * not containerized: controller files are in path contained
				 * in PROC_CGROUP_FILE concatenated to "<cgrouproot>/<controller>/"
				 */
				appendStringInfo(str, "%s/%s/%s", cgrouproot, controller, r);
			}
			else
			{
				/*
				 * containerized: controller files are in path contained
				 * in "<cgrouproot>/<controller>/" directly
				 */
				appendStringInfo(str, "%s/%s", cgrouproot, controller);
			}

			cgpath->keys[i] = MemoryContextStrdup(TopMemoryContext, controller);
			cgpath->values[i] = MemoryContextStrdup(TopMemoryContext, str->data);
			if (strcmp(controller, "memory") == 0)
				defpath = cgpath->values[i];
		}

		create_default_cgpath(defpath, nlines);
	}
	else if (is_cgroup_v2)
	{
		/*
		 * In v2 the active controllers for the
		 * cgroup are listed in cgroup.controllers
		 */
		StringInfo		fname = makeStringInfo();
		StringInfo		str = makeStringInfo();
		int				nvals;
		char		  **controllers;
		char		   *rawstr;
		char		   *defpath;
		int				i;

		/* read PROC_CGROUP_FILE, which for v2 has one line */
		rawstr = read_one_nlsv(ftr);

		if (!containerized)
		{
			/*
			 * not containerized: controller files are in path contained
			 * in PROC_CGROUP_FILE
			 *
			 * cgroup v2 PROC_CGROUP_FILE has one line
			 * that always starts "0::/", so skip that
			 * in order to get the relative path to the
			 * unified set of cgroup controllers
			 */
			appendStringInfo(str, "%s/%s", cgrouproot, (rawstr + 4));
			defpath = str->data;
		}
		else
		{
			/* containerized: controller files in cgrouproot directly */
			defpath = cgrouproot;
		}

		/*
		 * In cgroup v2 all the controllers are in the
		 * same cgroup dir, but we need to determine which
		 * controllers are present in the current cgroup.
		 * It is simpler to just repeat the same path for
		 * each controller in order to maintain consistency
		 * with the cgroup v1 case.
		 */
		appendStringInfo(fname, "%s/%s", defpath, "cgroup.controllers");
		controllers = parse_space_sep_val_file(fname->data, &nvals);

		cgpath->nkvp = nvals;
		cgpath->keys = (char **) repalloc(cgpath->keys, cgpath->nkvp * sizeof(char *));
		cgpath->values = (char **) repalloc(cgpath->values, cgpath->nkvp * sizeof(char *));

		for (i = 0; i < cgpath->nkvp; ++i)
		{
			cgpath->keys[i] = MemoryContextStrdup(TopMemoryContext, controllers[i]);
			cgpath->values[i] = MemoryContextStrdup(TopMemoryContext, defpath);
		}

		create_default_cgpath(defpath, nvals);
	}
	else /* unsupported */
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: unsupported cgroup configuration")));
	}
}

/*
 * Look up the cgroup path by controller name
 * Since this should never be a long list, just
 * do brute force lookup.
 */
static char *
get_cgpath_value(char *key)
{
	int		i;

	for (i = 0; i < cgpath->nkvp; ++i)
	{
		if (strcmp(cgpath->keys[i], key) == 0)
			return pstrdup(cgpath->values[i]);
	}

	/* bad request if not found */
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("failed to find controller %s", key)));

	/* unreached */
	return "unknown";
}

static Datum
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

	for (i = 0; i < nrow; ++i)
	{
		char	   **rowvals = values[i];

		tuple = BuildTupleFromCStrings(attinmeta, rowvals);
		tuplestore_puttuple(tupstore, tuple);
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

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_mode);
Datum
pgnodemx_cgroup_mode(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(cgmode));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_path);
Datum
pgnodemx_cgroup_path(PG_FUNCTION_ARGS)
{
	char ***values;
	int		nrow = cgpath->nkvp;
	int		ncol = 2;
	int		i;

	values = (char ***) palloc(nrow * sizeof(char **));
	for (i = 0; i < nrow; ++i)
	{
		values[i] = (char **) palloc(ncol * sizeof(char *));

		values[i][0] = pstrdup(cgpath->keys[i]);
		values[i][1] = pstrdup(cgpath->values[i]);
	}

	return form_srf(fcinfo, values, nrow, ncol, cgpath_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_process_count);
Datum
pgnodemx_cgroup_process_count(PG_FUNCTION_ARGS)
{
	/* cgmembers returns pid count */
	PG_RETURN_INT32(cgmembers(NULL));
}

PG_FUNCTION_INFO_V1(pgnodemx_memory_pressure);
Datum
pgnodemx_memory_pressure(PG_FUNCTION_ARGS)
{
	StringInfo	ftr = makeStringInfo();
	int		nlines;
	char  **lines;

	appendStringInfo(ftr, "%s/%s", get_cgpath_value("memory"), "memory.pressure");
	if (is_cgroup_v1)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: file %s not supported under cgroup v1", ftr->data)));

	lines = read_nlsv(ftr->data, &nlines);
	if (nlines > 0)
	{
		char			 ***values;
		int					nrow = nlines;
		int					ncol;
		int					i;
		kvpairs			   *nkl;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			int		j;

			nkl = parse_nested_keyed_line(lines[i]);
			ncol = nkl->nkvp;
			if (ncol != MEM_PRESS_NCOL)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: unexpected format read from %s", ftr->data)));

			values[i] = (char **) palloc(ncol * sizeof(char *));
			for (j = 0; j < ncol; ++j)
				values[i][j] = pstrdup(nkl->values[j]);
		}

		return form_srf(fcinfo, values, nrow, ncol, mem_press_sig);
	}

	return (Datum) 0;
}

PG_FUNCTION_INFO_V1(pgnodemx_memstat_int64);
Datum
pgnodemx_memstat_int64(PG_FUNCTION_ARGS)
{
	return get_scalar_int64(fcinfo, get_cgpath_value("memory"));
}

PG_FUNCTION_INFO_V1(pgnodemx_keyed_memstat_int64);
Datum
pgnodemx_keyed_memstat_int64(PG_FUNCTION_ARGS)
{
	StringInfo	ftr = makeStringInfo();
	char	   *fname = convert_and_check_filename(PG_GETARG_TEXT_PP(0));
	int			nlines;
	char	  **lines;

	appendStringInfo(ftr, "%s/%s", get_cgpath_value("memory"), fname);

	lines = read_nlsv(ftr->data, &nlines);
	if (nlines > 0)
	{
		char	 ***values;
		int			nrow = nlines;
		int			ncol = 2;
		int			i;
		char	  **fkl;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			fkl = parse_flat_keyed_line(lines[i]);

			values[i] = (char **) palloc(ncol * sizeof(char *));
			values[i][0] = pstrdup(fkl[0]);
			values[i][1] = pstrdup(fkl[1]);
		}

		return form_srf(fcinfo, values, nrow, ncol, flat_keyed_int64_sig);
	}

	return (Datum) 0;
}

