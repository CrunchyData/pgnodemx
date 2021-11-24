/*
 * cgroup.c
 *
 * Functions specific to capture and manipulation of cgroup virtual files
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
#include <linux/magic.h>
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC  0x63677270
#endif
#include <sys/vfs.h>
#include <unistd.h>

#if PG_VERSION_NUM >= 110000
#include "catalog/pg_type_d.h"
#else
#include "catalog/pg_type.h"
#endif
#include "fmgr.h"
#if PG_VERSION_NUM >= 130000
#include "lib/qunique.h"
#else	/* did not exist prior to pg13; use local copy */
#include "qunique.h"
#endif
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/guc_tables.h"
#include "utils/int8.h"
#include "utils/memutils.h"
#if PG_VERSION_NUM >= 100000
#include "utils/varlena.h"
#endif

#include "fileutils.h"
#include "genutils.h"
#include "parseutils.h"
#include "cgroup.h"

#define DEFCONTROLLER	"memory"

/* context gathering functions */
static void create_default_cgpath(char *str, int curlen);
static void init_or_reset_cgpath(void);
static StringInfo candidate_controller_path(char *controller, char *r);
static StringInfo check_and_fix_controller_path(char *controller, char *r);

/* custom GUC vars */
bool	containerized = false;
char *cgrouproot = NULL;
bool cgroup_enabled = true;

/* module globals */
char *cgmode = NULL;
kvpairs *cgpath = NULL;

/*
 * Take input filename from caller, make sure it is acceptable
 * (not absolute, no relative parent references, caller belongs
 * to correct role), and concatenates it with the path to the
 * related controller in the cgroup filesystem. The returned
 * value is a "fully qualified" path to the file of interest
 * for the purposes of cgroup virtual files.
 */
char *
get_fq_cgroup_path(FunctionCallInfo fcinfo)
{
	StringInfo	ftr = makeStringInfo();
	char	   *fname = convert_and_check_filename(PG_GETARG_TEXT_PP(0));
	char	   *p = strchr(fname, '.');
	Size		len;
	char	   *controller;

	if (!p)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: missing \".\" in filename %s", PROC_CGROUP_FILE)));

	len = (p - fname);
	controller = pnstrdup(fname, len);
	appendStringInfo(ftr, "%s/%s", get_cgpath_value(controller), fname);

	return pstrdup(ftr->data);
}

/*
 * Find out all the pids in a cgroup.
 * 
 * In cgroup v2 (at least) cgroup.procs is not sorted or guaranteed unique.
 * Remedy that. *pids is set to point to a palloc'd array containing
 * distinct pids in sorted order. The length of the array is the
 * function result. Cribbed from aclmembers.
 */
int
cgmembers(int64 **pids)
{
	int64	   *list;
	int			i;
	StringInfo	ftr = makeStringInfo();
	int			nlines;
	char	  **lines;

	appendStringInfo(ftr, "%s/%s", get_cgpath_value("cgroup"), "cgroup.procs");
	lines = read_nlsv(ftr->data, &nlines);

	if (nlines == 0)
	{
		/*
		 * This should never happen, by definition. If it does
		 * die horribly...
		 */
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: no cgroup procs found in file %s", ftr->data)));
	}

	/* Allocate the worst-case space requirement */
	list = (int64 *) palloc(nlines * sizeof(int64));

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
	*pids = list;

	/* Remove duplicates from the array, returns new size */
	return qunique(list, nlines, sizeof(int64), int64_cmp);
}

/*
 * Determine whether running inside a container.
 * 
 * Of particular interest to us is whether our cgroup vfs has been mounted
 * at /sys/fs/cgroup for us. Inside a container that is what we expect,
 * but outside of a container it will be where PROC_CGROUP_FILE tells
 * us to find it.
 */
void
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
					/* use the DEFCONTROLLER controller path to test with */
					char   *line = lines[i];
					char   *p = strchr(line, ':');

					/* advance past the colon */
					if (p)
						p += 1;

					if (strncmp(p, DEFCONTROLLER, 6) == 0)
					{
						p = strchr(p, ':');
						/* advance past the colon and "/" */
						if (p)
							p += 2;

						appendStringInfo(str, "%s/%s/%s", cgrouproot, DEFCONTROLLER, p);
						break;
					}
				}
			}
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: no cgroup paths found in file %s", PROC_CGROUP_FILE)));

			if (access(str->data, F_OK) != -1)
				containerized = false;
			else
				containerized = true;
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
bool
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

	/*
	 * If requested, directly set cgmode to disabled before
	 * doing anything else.
	 */
	if (!cgroup_enabled)
	{
		cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_DISABLED);
		return false;
	}

	ret = statfs(cgrouproot, &buf);
	if (ret == -1)
	{
		/*
		 * If we have an error trying to stat cgrouproot, there is not
		 * much else we can do besides disabling cgroup access.
		 */
		ereport(WARNING,
				(errcode_for_file_access(),
				errmsg("pgnodemx: statfs error on cgroup mount %s: %m", cgrouproot),
				errdetail("disabling cgroup virtual file system access")));
		cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_DISABLED);
		return false;
	}

	if (buf.f_type == CGROUP2_SUPER_MAGIC)					/* cgroup v2 */
	{
		char   *ftr = PROC_CGROUP_FILE;
		int		nlines;

		/*
		 * From what I have read, this should not ever happen.
		 * However it was reported from the field, so apparently
		 * it *can* happen.
		 * 
		 * In any case, it seems to indicate hybrid mode is in effect.
		 */
		read_nlsv(ftr, &nlines);
		if (nlines != 1)
		{
			cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_HYBRID);
			return false;
		}

		cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_V2);
		return true;
	}
	else if (buf.f_type == TMPFS_MAGIC)
	{
		StringInfo		str = makeStringInfo();

		appendStringInfo(str, "%s/%s", cgrouproot, "unified");
		ret = statfs(str->data, &buf);

		if (ret == 0 && buf.f_type == CGROUP2_SUPER_MAGIC)	/* hybrid mode */
		{
			cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_HYBRID);
			return false;
		}
		else												/* cgroup v1 */
		{
			cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_V1);
			return true;
		}
	}
	else
	{
		/*
		 * If cgrouproot is not actually a cgroup mount, there is not
		 * much else we can do besides disabling cgroup access.
		 */
		ereport(WARNING,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: unexpected mount type on cgroup root %s", cgrouproot),
				errdetail("disabling cgroup virtual file system access")));
		cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_DISABLED);
		return false;
	}
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
	cgpath->keys[cgpath->nkvp - 1] = MemoryContextStrdup(TopMemoryContext, "cgroup");
	if (str != NULL)
		cgpath->values[cgpath->nkvp - 1] = MemoryContextStrdup(TopMemoryContext, str);
	else
		cgpath->values[cgpath->nkvp - 1] = MemoryContextStrdup(TopMemoryContext,
														"Default_Controller_Not_Found");
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

/*
 * Take an array of int, and copy it.
 */
static int *
intarr_copy(int *oldarr, size_t oldsize)
{
	int	   *newarr;
	int		sizebytes = oldsize * sizeof(int);

	Assert(oldarr != NULL);
	Assert(sizebytes != 0);

	newarr = (int *) palloc(sizebytes);
	memcpy(newarr, oldarr, sizebytes);

	return newarr;
}

static void
swap (int *arr, int a, int b)
{
	int		t = arr[a];

	arr[a] = arr[b];
	arr[b] = t;
}

/*
 * Generate permutations of origarr indexes, and return them as an array
 * of integer array. Each array represents the indexes for a different
 * permutation. Use's "heap's algorithm".
 * See https://en.wikipedia.org/wiki/Heap%27s_algorithm
 */
static void
heap_permute(int *origarr, size_t origarrsize,
			 size_t level,
			 int **arrofpermarr, int *nrow)
{
	int		i;

    if (level == 1)
	{
		/*
		 * We have recursed to the end of the original array of indexes,
		 * so attach our permutation to the array of arrays and return it.
		 */
		arrofpermarr[*nrow] = intarr_copy(origarr, origarrsize);
		++(*nrow);
	}
	else
	{
        /*
		 * Generate permutations with levelth unaltered
		 * Initially level == length(origarr) == origarrsize
		 */
		heap_permute(origarr, origarrsize, level - 1, arrofpermarr, nrow);

		for (i = 0; i < level - 1; ++i)
		{
			/* Swap choice dependent on parity of level (even or odd) */
			if (level % 2 == 0)
			{
				/*
				 * If level is even, swap ith and
				 * (level-1)th i.e (last) element
				 */
                swap(origarr, i, level-1);
			}
            else
			{
				/*
				 * If level is odd, swap 0th i.e (first) and (level-1)th
				 * i.e (last) element
				 */
                swap(origarr, 0, level-1);
			}

			heap_permute(origarr, origarrsize, level - 1, arrofpermarr, nrow);
		}
	}
}

/*
 * Accept a string list (comma delimited list of items)
 * and return an array of strings representing all of the
 * different permutation of the original string list.
 */
#define MAX_PERM_ARRLEN		10
static char ***
get_list_permutations(char *controller, int ncol, int *nrow)
{
	char	   *rawstring = pstrdup(controller);
	List	   *origlist = NIL;
	ListCell   *l;
	int		   *origarr = NULL;
	char	  **origarr_str = NULL;
	size_t		origarrsize = 0;
	int		  **arrofpermarr = NULL;
	int			i;
	char	 ***values;
	int			cntr;
	int			fact = 1;
	StringInfo	str = makeStringInfo();
 
	/*
	 * If the controller name includes one or more ",", we need
	 * to check all orderings to see which is the actual path.
	 * 
	 * Parse the list into individual tokens
	 */
	if (!SplitIdentifierString(rawstring, ',', &origlist))
	{
		elog(WARNING, "failed to parse controller string: %s", controller);
		return NULL;
	}

	origarrsize = list_length(origlist);
	if (origarrsize > MAX_PERM_ARRLEN)
	{
		elog(WARNING, "too many elements in controller string: %s", controller);
		return NULL;
	}

	origarr_str = (char **) palloc(origarrsize * sizeof(char *));
	i = 0;
	foreach(l, origlist)
	{
		origarr_str[i] = pstrdup((char *) lfirst(l));
		++i;
	}

	origarr = (int *) palloc(origarrsize * sizeof(int));
	for (i = 0; i < origarrsize; ++i)
		origarr[i] = i;

	/* precalculate how many permutations we should get back */
	for (cntr = 1; cntr <= origarrsize; cntr++)
		fact = fact * cntr;

	/* make space for the permutation arrays */
	arrofpermarr = (int **) palloc(fact * sizeof(int *));

	/* get list of permutation indexes */
	heap_permute(origarr, origarrsize, origarrsize, arrofpermarr, nrow);
	if (*nrow != fact)
		elog(WARNING, "expected %d permutations, got %d", fact, *nrow);

	/* make space for the return tuples */
	values = (char ***) palloc((*nrow) * sizeof(char **));

	/* map the original list back to the permuted indexes */
	for (i = 0; i < (*nrow); ++i)
	{
		int		   *pidx = arrofpermarr[i];
		int			j;

		resetStringInfo(str);
		for(j = 0; j < origarrsize; ++j)
		{
			char   *tok = origarr_str[pidx[j]];

			if (j == 0)
				appendStringInfo(str, "%s", tok);
			else
				appendStringInfo(str, ",%s", tok);
		}

		values[i] = (char **) palloc(ncol * sizeof(char *));
		values[i][0] = pstrdup(str->data);
		pfree(arrofpermarr[i]);
	}

	pfree(arrofpermarr);
	return values;
}

/*
 * Create candidate path based on controller string taking into account
 * whether we are "containerized" or not.
 */
static StringInfo
candidate_controller_path(char *controller, char *r)
{
	StringInfo		str = makeStringInfo();

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

	return str;
}

/*
 * Attempt to determine and return a valid path for a cgroup controller.
 * 
 * If no directories are found, return "Controller_Not_Found" as the
 * path. If we were to raise an ERROR it would prevent Postgres from starting
 * since this extension is preloaded, which seems less friendly than causing
 * later queries to generate errors. For example:
 * 
 *   could not open file "Controller_Not_Found/cpuacct.usage"
 *   for reading: No such file or directory
 * 
 * At least would clue us in that something went wrong without causing an
 * outage of postgres itself.
 */
static StringInfo
check_and_fix_controller_path(char *controller, char *r)
{
	StringInfo		str = candidate_controller_path(controller, r);

	if (strchr(controller, ',') == NULL)
	{
		/*
		 * The controller name does not include "," and is therefore
		 * a single controller.
		 */

		/*
		 * Should not happen (I think), but if the directory does
		 * not exist, mark it as such for debugging purposes.
		 * But avoid throwing an error, which would prevent Postgres
		 * from starting up entirely.
		 */
		if (access(str->data, F_OK) != 0)
		{
			resetStringInfo(str);
			appendStringInfoString(str, "Controller_Not_Found");
		}

		return str;
	}
	else
	{
		/*
		 * The controller name includes "," and is therefore a list
		 * of controllers. It turns out that the list ordering in
		 * /proc/self/cgroup might not match the list ordering used
		 * for the cgroupfs in some circumstances. But first check the
		 * proposed path based on /proc/self/cgroup to see if it
		 * actually exists. If so, return that.
		 */
		if (access(str->data, F_OK) == 0)
			return str;
		else
		{
			/* if not, try the alternative orderings */
			char	 ***values;
			int			nrow = 0;
			int			ncol = 1;
			int			i;

			values = get_list_permutations(controller, ncol, &nrow);
			for (i = 0; i < nrow; ++i)
			{
				char   *pcontroller = values[i][0];

				resetStringInfo(str);
				str = candidate_controller_path(pcontroller, r);
				if (access(str->data, F_OK) == 0)
					return str;
			}

			/* none of the candidates were valid */
			resetStringInfo(str);
			appendStringInfoString(str, "Controller_Not_Found");

			return str;
		}
	}
}

/*
CREATE FUNCTION permute_list(TEXT)
RETURNS SETOF TEXT
AS '$libdir/pgnodemx', 'pgnodemx_permute_list'
LANGUAGE C STABLE STRICT;
*/
/* function return signatures */
Oid cg_text_sig[] = {TEXTOID};
/* debug function */
PG_FUNCTION_INFO_V1(pgnodemx_permute_list);
Datum
pgnodemx_permute_list(PG_FUNCTION_ARGS)
{
	char	   *controller = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char	 ***values;
	int			nrow = 0;
	int			ncol = 1;

	values = get_list_permutations(controller, ncol, &nrow);
	return form_srf(fcinfo, values, nrow, ncol, cg_text_sig);
}

void
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
		StringInfo		str;
		int				i;
		char		   *defpath = NULL;

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
			 * 
			 * Sometimes the <controller> part is further divided
			 * into a list of controllers, e.g. "cpu,cpuacct" in which case
			 * the directory name might be based on either ordering of
			 * "cpu" and "cpuacct". In this case more work is required to
			 * discover the actual path in use.
			 */
			char   *line = lines[i];
			char   *p = strchr(line, ':');
			char   *r;
			char   *q;
			Size	len;
			char   *controller;

			if (p)
				p += 1;
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: malformed cgroup path found in file %s", PROC_CGROUP_FILE)));

			r = strchr(p, ':');
			/* advance past the ":" and also the "/" */
			if (r)
				r += 2;
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: malformed cgroup path found in file %s", PROC_CGROUP_FILE)));

			len = ((r - p) - 2);

			controller = pnstrdup(p, len);
			q = strchr(controller, '=');
			if (q)
				controller = q + 1;

			/* get valid path to controller */
			str = check_and_fix_controller_path(controller, r);

			cgpath->keys[i] = MemoryContextStrdup(TopMemoryContext, controller);
			cgpath->values[i] = MemoryContextStrdup(TopMemoryContext, str->data);
			if (strcasecmp(controller, DEFCONTROLLER) == 0)
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
		char		   *defpath = NULL;
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
char *
get_cgpath_value(char *key)
{
	int		i;

	for (i = 0; i < cgpath->nkvp; ++i)
	{
		char   *p;
		char   *controller = cgpath->keys[i];
		char   *path = cgpath->values[i];

		/*
		 * If controller name cgpath->keys[i] includes ",",
		 * split into multiple subkeys and check each one.
		 */
		p = strchr(controller, ',');
		if (!p)
		{
			/* no subkeys, just do it */
			if (strcmp(controller, key) == 0)
				return pstrdup(path);
		}
		else
		{
			/*
			 * Multiple subkeys. Check each one, but first get a
			 * copy we can mutate.
			 */
			char   *buf = pstrdup(controller);
			char   *token;
			char   *lstate;

			for (token = strtok_r(buf, ",", &lstate); token; token = strtok_r(NULL, ",", &lstate))
			{
				if (strcmp(token, key) == 0)
					return pstrdup(path);
			}
		}
	}

	/* bad request if not found */
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("failed to find controller %s", key)));

	/* unreached */
	return "unknown";
}
