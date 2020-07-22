/*
 * cgroup.c
 *
 * Functions specific to capture and manipulation of cgroup virtual files
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
#include <linux/magic.h>
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
/* also locally defined prior to pg13 */
#define MAXINT8LEN              25
#endif
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/guc_tables.h"
#include "utils/int8.h"
#include "utils/memutils.h"

#include "fileutils.h"
#include "genutils.h"
#include "parseutils.h"
#include "cgroup.h"

/* context gathering functions */
static void create_default_cgpath(char *str, int curlen);
static void init_or_reset_cgpath(void);

/* custom GUC vars */
bool	containerized = false;
char *cgrouproot = NULL;
bool cgroupfs_enabled = true;

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
 * In cgroup v2 cgroup.procs is not sorted or guaranteed unique.
 * Remedy that. If not NULL, *pids is set to point to a palloc'd
 * array containing distinct pids in sorted order. The length of
 * the array is the function result. Cribbed from aclmembers.
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
					/* use the memory controller path to test with */
					char   *line = lines[i];
					char   *p = strchr(line, ':') + 1;

					if (strncmp(p, "memory", 6) == 0)
					{
						p = strchr(p, ':') + 1;
						appendStringInfo(str, "%s/memory/%s", cgrouproot, p);
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
	if (!cgroupfs_enabled)
	{
		cgmode = MemoryContextStrdup(TopMemoryContext, CGROUP_DISABLED);
		return false;
	}

	ret = statfs(cgrouproot, &buf);
	if (ret == -1)
	{
		/*
		 * If we have an error trying to stat cgrouproot, there is not
		 * much else we can do besides disabling cgroupfs access.
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
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: unexpected mount type on cgroup root %s", cgrouproot)));

	/* never reached */
	return false;
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
char *
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
