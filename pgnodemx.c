/*
 * pgnodemx
 *
 * SQL functions that allow capture of node OS metrics from PostgreSQL
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

#if PG_VERSION_NUM < 90500
#error "pgnodemx only builds with PostgreSQL 9.5 or later"
#endif

#include <dlfcn.h>
#ifdef USE_OPENSSL
#include <openssl/ssl.h>
#endif
#include <unistd.h>

#include "catalog/pg_authid.h"
#if PG_VERSION_NUM >= 110000
#include "catalog/pg_type_d.h"
#else
#include "catalog/pg_type.h"
#endif
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/guc_tables.h"
#include "utils/int8.h"

#include "cgroup.h"
#include "envutils.h"
#include "fileutils.h"
#include "genutils.h"
#include "kdapi.h"
#include "parseutils.h"

PG_MODULE_MAGIC;

/* human readable to bytes */
#if PG_VERSION_NUM < 90600
#define h2b(arg1) size_bytes(arg1)
#else
#define h2b(arg1) \
  DatumGetInt64(DirectFunctionCall1(pg_size_bytes, PointerGetDatum(cstring_to_text(arg1))))
#endif

/* various /proc/ source files */
#define diskstats	"/proc/diskstats"
#define mountinfo	"/proc/self/mountinfo"
#define meminfo		"/proc/meminfo"
#define netstat		"/proc/self/net/dev"

/* function return signatures */
Oid text_sig[] = {TEXTOID};
Oid bigint_sig[] = {INT8OID};
Oid text_text_sig[] = {TEXTOID, TEXTOID};
Oid text_bigint_sig[] = {TEXTOID, INT8OID};
Oid text_text_bigint_sig[] = {TEXTOID, TEXTOID, INT8OID};
Oid text_text_float8_sig[] = {TEXTOID, TEXTOID, FLOAT8OID};
Oid _2_numeric_text_9_numeric_text_sig[] = {NUMERICOID, NUMERICOID, TEXTOID, NUMERICOID,
										  NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID,
										  NUMERICOID, NUMERICOID, NUMERICOID, TEXTOID};
Oid _4_bigint_6_text_sig[] = {INT8OID, INT8OID, INT8OID, INT8OID,
							  TEXTOID, TEXTOID, TEXTOID, TEXTOID, TEXTOID, TEXTOID};
Oid bigint_bigint_text_11_bigint_sig[] = {INT8OID, INT8OID, TEXTOID,
										  INT8OID, INT8OID, INT8OID, INT8OID,
										  INT8OID, INT8OID, INT8OID, INT8OID,
										  INT8OID, INT8OID, INT8OID};
Oid text_16_bigint_sig[] = {TEXTOID,
							INT8OID, INT8OID, INT8OID, INT8OID,
							INT8OID, INT8OID, INT8OID, INT8OID,
							INT8OID, INT8OID, INT8OID, INT8OID,
							INT8OID, INT8OID, INT8OID, INT8OID};

void _PG_init(void);
Datum pgnodemx_cgroup_mode(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_path(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_process_count(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_scalar_bigint(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_scalar_float8(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_scalar_text(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_setof_bigint(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_setof_text(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_array_text(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_array_bigint(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_setof_kv(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_setof_ksv(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_setof_nkv(PG_FUNCTION_ARGS);
Datum pgnodemx_envvar_text(PG_FUNCTION_ARGS);
Datum pgnodemx_envvar_bigint(PG_FUNCTION_ARGS);
Datum pgnodemx_proc_meminfo(PG_FUNCTION_ARGS);
Datum pgnodemx_fsinfo(PG_FUNCTION_ARGS);
Datum pgnodemx_network_stats(PG_FUNCTION_ARGS);
Datum pgnodemx_kdapi_setof_kv(PG_FUNCTION_ARGS);
Datum pgnodemx_kdapi_scalar_bigint(PG_FUNCTION_ARGS);

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

	DefineCustomBoolVariable("pgnodemx.cgroup_enabled",
							 "True if cgroup virtual file system access is enabled",
							 NULL, &cgroup_enabled, true, PGC_POSTMASTER,
							 0, NULL, NULL, NULL);

	DefineCustomBoolVariable("pgnodemx.containerized",
							 "True if operating inside a container",
							 NULL, &containerized, false, PGC_POSTMASTER,
							 0, NULL, NULL, NULL);

	DefineCustomStringVariable("pgnodemx.cgrouproot",
							   "Path to root cgroup",
							   NULL, &cgrouproot, "/sys/fs/cgroup", PGC_POSTMASTER,
							   0, NULL, NULL, NULL);

	DefineCustomBoolVariable("pgnodemx.kdapi_enabled",
							 "True if Kubernetes Downward API file system access is enabled",
							 NULL, &kdapi_enabled, true, PGC_POSTMASTER,
							 0, NULL, NULL, NULL);

	DefineCustomStringVariable("pgnodemx.kdapi_path",
							   "Path to Kubernetes Downward API files",
							   NULL, &kdapi_path, "/etc/podinfo", PGC_POSTMASTER,
							   0, NULL, NULL, NULL);

	/* don't try to set cgmode unless cgroup is enabled */
	if (set_cgmode())
	{
		/* must determine if containerized before setting cgpath */
		set_containerized();
		set_cgpath();
	}
	else
	{
		/*
		 * If cgmode cannot be set, either because cgroup_enabled is
		 * already set to false, or because of an error trying to stat
		 * cgrouproot, then we must force disable cgroup functions. 
		 */
		cgroup_enabled = false;
	}

	/* force kdapi disabled if path does not exist */
	if (access(kdapi_path, F_OK) != 0)
	{
		/*
		 * If kdapi_path does not exist, there is not
		 * much else we can do besides disabling kdapi access.
		 */
		ereport(WARNING,
				(errcode_for_file_access(),
				errmsg("pgnodemx: Kubernetes Downward API path %s does not exist: %m", kdapi_path),
				errdetail("disabling Kubernetes Downward API file system access")));

		kdapi_enabled = false;
	}

    inited = true;
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_mode);
Datum
pgnodemx_cgroup_mode(PG_FUNCTION_ARGS)
{
	/*
	 * Do not check cgroup_enabled here; this is the one cgroup
	 * function which *should* work when cgroup is disabled.
	 */

	PG_RETURN_TEXT_P(cstring_to_text(cgmode));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_path);
Datum
pgnodemx_cgroup_path(PG_FUNCTION_ARGS)
{
	char ***values;
	int		nrow;
	int		ncol = 2;
	int		i;

	if (!cgroup_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, text_text_sig);

	nrow = cgpath->nkvp;
	if (nrow < 1)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: no lines in cgpath")));

	/* never reached */

	values = (char ***) palloc(nrow * sizeof(char **));
	for (i = 0; i < nrow; ++i)
	{
		values[i] = (char **) palloc(ncol * sizeof(char *));

		values[i][0] = pstrdup(cgpath->keys[i]);
		values[i][1] = pstrdup(cgpath->values[i]);
	}

	return form_srf(fcinfo, values, nrow, ncol, text_text_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_process_count);
Datum
pgnodemx_cgroup_process_count(PG_FUNCTION_ARGS)
{
	int64	   *cgpids;

	if (!cgroup_enabled)
		PG_RETURN_NULL();

	/* cgmembers returns pid count */
	PG_RETURN_INT32(cgmembers(&cgpids));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_scalar_bigint);
Datum
pgnodemx_cgroup_scalar_bigint(PG_FUNCTION_ARGS)
{
	char   *fqpath;

	if (!cgroup_enabled)
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	PG_RETURN_INT64(get_int64_from_file(fqpath));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_scalar_float8);
Datum
pgnodemx_cgroup_scalar_float8(PG_FUNCTION_ARGS)
{
	char   *fqpath;

	if (!cgroup_enabled)
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	PG_RETURN_FLOAT8(get_double_from_file(fqpath));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_scalar_text);
Datum
pgnodemx_cgroup_scalar_text(PG_FUNCTION_ARGS)
{
	char   *fqpath;

	if (!cgroup_enabled)
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	PG_RETURN_TEXT_P(cstring_to_text(get_string_from_file(fqpath)));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_setof_bigint);
Datum
pgnodemx_cgroup_setof_bigint(PG_FUNCTION_ARGS)
{
	char	   *fqpath;

	if (!cgroup_enabled)
		return form_srf(fcinfo, NULL, 0, 1, bigint_sig);

	fqpath = get_fq_cgroup_path(fcinfo);
	return setof_scalar_internal(fcinfo, fqpath, bigint_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_setof_text);
Datum
pgnodemx_cgroup_setof_text(PG_FUNCTION_ARGS)
{
	char	   *fqpath;

	if (!cgroup_enabled)
		return form_srf(fcinfo, NULL, 0, 1, text_sig);

	fqpath = get_fq_cgroup_path(fcinfo);
	return setof_scalar_internal(fcinfo, fqpath, text_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_array_text);
Datum
pgnodemx_cgroup_array_text(PG_FUNCTION_ARGS)
{
	char   *fqpath;
	char  **values;
	int		nvals;
	bool	isnull = false;
	Datum	dvalue;

	if (!cgroup_enabled)
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	values = parse_space_sep_val_file(fqpath, &nvals);
	dvalue = string_get_array_datum(values, nvals, TEXTOID, &isnull);
	if (!isnull)
		return dvalue;
	else
		PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_array_bigint);
Datum
pgnodemx_cgroup_array_bigint(PG_FUNCTION_ARGS)
{
	char   *fqpath;
	char  **values;
	int		nvals;
	bool	isnull = false;
	Datum	dvalue;
	int		i;

	if (!cgroup_enabled)
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	values = parse_space_sep_val_file(fqpath, &nvals);

	/* deal with "max" */
	for (i = 0; i < nvals; ++i)
	{
		if (strcasecmp(values[i], "max") == 0)
			values[i] = int64_to_string(PG_INT64_MAX);
	}

	dvalue = string_get_array_datum(values, nvals, INT8OID, &isnull);
	if (!isnull)
		return dvalue;
	else
		PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_setof_kv);
Datum
pgnodemx_cgroup_setof_kv(PG_FUNCTION_ARGS)
{
	char	   *fqpath;
	int			nlines;
	char	  **lines;
	int			ncol = 2;

	if (!cgroup_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, text_bigint_sig);

	fqpath = get_fq_cgroup_path(fcinfo);
	lines = read_nlsv(fqpath, &nlines);
	if (nlines > 0)
	{
		char	 ***values;
		int			nrow = nlines;
		int			i;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			int	ntok;

			values[i] = parse_ss_line(lines[i], &ntok);
			if (ntok != ncol)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: expected %d tokens, got %d in flat keyed file %s, line %d",
							   ncol, ntok, fqpath, i + 1)));
		}

		return form_srf(fcinfo, values, nrow, ncol, text_bigint_sig);
	}

	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in flat keyed file: %s ", fqpath)));

	/* never reached */
	return (Datum) 0;
}

/*
 * Some virtual files have multiple rows of three columns:
 * (key text, subkey text, value bigint). They essentially
 * look like nkv, except they only have one subkey and value
 * and no "=" between them. The columns are space separated.
 * They also may have a "grand sum" line that only has two
 * columns because it represents a sum of all the other lines.
 * Two examples of this are blkio.throttle.io_serviced and
 * blkio.throttle.io_service_bytes.
 */
PG_FUNCTION_INFO_V1(pgnodemx_cgroup_setof_ksv);
Datum
pgnodemx_cgroup_setof_ksv(PG_FUNCTION_ARGS)
{
	char	   *fqpath;
	int			nlines;
	char	  **lines;
	int			ncol = 3;

	if (!cgroup_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, text_text_bigint_sig);

	fqpath = get_fq_cgroup_path(fcinfo);
	lines = read_nlsv(fqpath, &nlines);

	if (nlines > 0)
	{
		char	 ***values;
		int			nrow = nlines;
		int			i;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			int	ntok;

			values[i] = parse_ss_line(lines[i], &ntok);
			if (ntok > ncol || ntok < ncol - 1)
			{
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: expected %d tokens, got %d in flat keyed file %s, line %d",
							   ncol, ntok, fqpath, i + 1)));
			}
			else if (ntok == 2)
			{
				/* for the two column case, expand and shift the values right */
				values[i] = (char **) repalloc(values[i], ncol * sizeof(char *));
				values[i][2] = values[i][1];
				values[i][1] = values[i][0];
				values[i][0] = pstrdup("all");
			}
		}

		return form_srf(fcinfo, values, nrow, ncol, text_text_bigint_sig);
	}

	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in flat keyed file: %s ", fqpath)));

	/* never reached */
	return (Datum) 0;
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_setof_nkv);
Datum
pgnodemx_cgroup_setof_nkv(PG_FUNCTION_ARGS)
{
	char	   *fqpath;
	int			nlines;
	char	  **lines;
	int			ncol = 3;

	if (!cgroup_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, text_text_float8_sig);

	fqpath = get_fq_cgroup_path(fcinfo);
	lines = read_nlsv(fqpath, &nlines);
	if (nlines > 0)
	{
		char			 ***values;
		int					nrow;
		kvpairs			   *nkl;
		int					nkvp;
		int					i;

		/*
		 * We expect that each line in a "nested keyed" file has the
		 * same number of column. Therefore use the first line of the
		 * parsed file to determine how many columns we have. The entire
		 * line has a key, and each column will consist of a subkey and
		 * value. We will build "number of column" rows from this one line.
		 * Each row in the output will look like (key, subkey, value).
		 */
		nkl = parse_nested_keyed_line(pstrdup(lines[0]));
		nkvp = nkl->nkvp;

		/* each line expands to nkvp - 1 rows in the output */
		nrow = nlines * (nkvp - 1);
		values = (char ***) palloc(nrow * sizeof(char **));

		for (i = 0; i < nlines; ++i)
		{
			int		j;

			nkl = parse_nested_keyed_line(lines[i]);
			if (nkl->nkvp != nkvp)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: not nested keyed file: %s ", fqpath)));

			for (j = 1; j < nkvp; ++j)
			{
				values[(i * (nkvp - 1)) + j - 1] = (char **) palloc(ncol * sizeof(char *));

				values[(i * (nkvp - 1)) + j - 1][0] = pstrdup(nkl->values[0]);
				values[(i * (nkvp - 1)) + j - 1][1] = pstrdup(nkl->keys[j]);
				values[(i * (nkvp - 1)) + j - 1][2] = pstrdup(nkl->values[j]);
			}
		}

		return form_srf(fcinfo, values, nrow, ncol, text_text_float8_sig);
	}

	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in nested keyed file: %s ", fqpath)));

	/* never reached */
	return (Datum) 0;
}

PG_FUNCTION_INFO_V1(pgnodemx_envvar_text);
Datum
pgnodemx_envvar_text(PG_FUNCTION_ARGS)
{
	char *varname = text_to_cstring(PG_GETARG_TEXT_PP(0));

	/* Limit use to members of special role */
	pgnodemx_check_role();

	PG_RETURN_TEXT_P(cstring_to_text(get_string_from_env(varname)));
}

PG_FUNCTION_INFO_V1(pgnodemx_envvar_bigint);
Datum
pgnodemx_envvar_bigint(PG_FUNCTION_ARGS)
{
	bool	success = false;
	int64	result;
	char   *varname = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char   *value = get_string_from_env(varname);

	/* Limit use to members of special role */
	pgnodemx_check_role();

	success = scanint8(value, true, &result);
	if (!success)
		ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("contents not an integer: env variable \"%s\"",
				varname)));

	PG_RETURN_INT64(result);
}

/*
 * "/proc" files: these files have all kinds of formats. For now
 * at least do not try to create generic parsing functions. Just
 * create a handful of specific access functions for the most
 * interesting (to us) files.
 */

/*
 * /proc/diskstats file:
 * 
 *  1 - major number
 *  2 - minor mumber
 *  3 - device name
 *  4 - reads completed successfully
 *  5 - reads merged
 *  6 - sectors read
 *  7 - time spent reading (ms)
 *  8 - writes completed
 *  9 - writes merged
 * 10 - sectors written
 * 11 - time spent writing (ms)
 * 12 - I/Os currently in progress
 * 13 - time spent doing I/Os (ms)
 * 14 - weighted time spent doing I/Os (ms)
 * 
 * Kernel 4.18+ appends four more fields for discard
 * tracking putting the total at 18:
 * 
 * 15 - discards completed successfully
 * 16 - discards merged
 * 17 - sectors discarded
 * 18 - time spent discarding
 * 
 * Kernel 5.5+ appends two more fields for flush requests:
 * 
 * 19 - flush requests completed successfully
 * 20 - time spent flushing
 * 
 * For now, validate either 14,18, or 20 fields found when
 * parsing the lines, but only return the first 14. If there
 * is demand for the other fields at some point, possibly
 * add them then.
 */
PG_FUNCTION_INFO_V1(pgnodemx_proc_diskstats);
Datum
pgnodemx_proc_diskstats(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			ncol = 14;
	char	 ***values = (char ***) palloc(0);
	char	  **lines;
	int			nlines;

	/* read /proc/self/net/dev file */
	lines = read_nlsv(diskstats, &nlines);

	/*
	 * These files have either 14,18, or 20 fields per line.
	 * We will validate one of those lengths, but only use 14 of the
	 * space separated columns. The third column is the device name.
	 * Rest of the columns are bigints.
	 */
	if (nlines > 0)
	{
		int			j;
		char	  **toks;

		nrow = nlines;
		values = (char ***) repalloc(values, nrow * sizeof(char **));
		for (j = 0; j < nrow; ++j)
		{
			int			ntok;
			int			k;

			values[j] = (char **) palloc(ncol * sizeof(char *));

			toks = parse_ss_line(lines[j], &ntok);
			if (ntok != 14 && ntok != 18  && ntok != 20)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: unexpected number of tokens, %d, in file %s, line %d",
							   ntok, diskstats, j + 1)));

			for (k = 0; k < ncol; ++k)
				values[j][k] = pstrdup(toks[k]);
		}
	}
	else
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: no data in file: %s ", diskstats)));

	return form_srf(fcinfo, values, nrow, ncol, bigint_bigint_text_11_bigint_sig);
}

/*
 * 3.5	/proc/<pid>/mountinfo - Information about mounts
 * --------------------------------------------------------
 * 
 * This file contains lines of the form:
 * 
 * 36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw,errors=continue
 * (1)(2)(3)   (4)   (5)      (6)      (7)   (8) (9)   (10)         (11)
 * 
 * (1) mount ID:  unique identifier of the mount (may be reused after umount)
 * (2) parent ID:  ID of parent (or of self for the top of the mount tree)
 * (3) major:minor:  value of st_dev for files on filesystem
 * (4) root:  root of the mount within the filesystem
 * (5) mount point:  mount point relative to the process's root
 * (6) mount options:  per mount options
 * (7) optional fields:  zero or more fields of the form "tag[:value]"
 * (8) separator:  marks the end of the optional fields
 * (9) filesystem type:  name of filesystem of the form "type[.subtype]"
 * (10) mount source:  filesystem specific information or "none"
 * (11) super options:  per super block options
 * 
 * Parsers should ignore all unrecognised optional fields.  Currently the
 * possible optional fields are:
 * 
 * shared:X  mount is shared in peer group X
 * master:X  mount is slave to peer group X
 * propagate_from:X  mount is slave and receives propagation from peer group X (*)
 * unbindable  mount is unbindable
 * --------------------------------------------------------
 * 
 * Map fields 1 - 6, skip 7 (one or more) and 8, map 9 - 11 to a virtual
 * table with 10 columns (split major:minor into two columns)
 */
PG_FUNCTION_INFO_V1(pgnodemx_proc_mountinfo);
Datum
pgnodemx_proc_mountinfo(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			ncol = 10;
	char	 ***values = (char ***) palloc(0);
	char	  **lines;
	int			nlines;

	/* read /proc/self/net/dev file */
	lines = read_nlsv(mountinfo, &nlines);

	/*
	 * These files are complicated - see above.
	 */
	if (nlines > 0)
	{
		int			j;
		char	  **toks;

		nrow = nlines;
		values = (char ***) repalloc(values, nrow * sizeof(char **));
		for (j = 0; j < nrow; ++j)
		{
			int			ntok;
			int			k;
			int			c = 0;
			bool		sep_found = false;

			values[j] = (char **) palloc(ncol * sizeof(char *));

			toks = parse_ss_line(lines[j], &ntok);
			/* there shoould be at least 10 tokens */
			if (ntok < 10)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: unexpected number of tokens, %d, in file %s, line %d",
							   ntok, mountinfo, j + 1)));

			/* iterate all found columns and keep the ones we want */
			for (k = 0; k < ntok; ++k)
			{
				/* grab the first 6 columns */
				if (k < 6)
				{
					if (k != 2)
					{
						values[j][c] = pstrdup(toks[k]);
						++c;
					}
					else
					{
						/* split major:minor into two columns */
						char   *p = strchr(toks[k], ':');
						Size	len;

						if (!p)
							ereport(ERROR,
									(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
									errmsg("pgnodemx: missing \":\" in file %s, line %d",
										   mountinfo, j + 1)));

						len = (p - toks[k]);
						values[j][c] = pnstrdup(toks[k], len);
						++c;

						values[j][c] = pstrdup(p + 1);
						++c;
					}
				}
				else if (strcmp(toks[k], "-") == 0) /* skip until the separator */
					sep_found = true;
				else if (sep_found) /* all good, grab the remaining columns */
				{
					values[j][c] = pstrdup(toks[k]);
					++c;
				}
			}

			/* make sure we found ncol columns */
			if (c != ncol)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: malformed line in file %s, line %d",
							   mountinfo, j + 1)));
		}
	}
	else
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: no data in file: %s ", mountinfo)));

	return form_srf(fcinfo, values, nrow, ncol, _4_bigint_6_text_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_proc_meminfo);
Datum
pgnodemx_proc_meminfo(PG_FUNCTION_ARGS)
{
	int			nlines;
	char	  **lines;
	int			ncol = 2;

	lines = read_nlsv(meminfo, &nlines);
	if (nlines > 0)
	{
		char	 ***values;
		int			nrow = nlines;
		int			i;
		char	  **fkl;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			size_t		len;
			StringInfo	hbytes = makeStringInfo();
			int64		nbytes;
			int			ntok;

			values[i] = (char **) palloc(ncol * sizeof(char *));

			/*
			 * These lines look like "<key>:_some_spaces_<val>_<unit>
			 * We usually get back 3 tokens but sometimes 2 (no unit).
			 * In either case we only have two output columns.
			 */
			fkl = parse_ss_line(lines[i], &ntok);
			if (ntok < 2 || ntok > 3)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: unexpected number of tokens, %d, in file %s, line %d",
							   ntok, meminfo, i + 1)));

			/* token 1 will end with an extraneous colon - strip that */
			len = strlen(fkl[0]) - 1;
			fkl[0][len] = '\0';
			values[i][0] = pstrdup(fkl[0]);

			/* reconstruct tok 2 and 3 and then convert to bytes */
			if (ntok == 3)
			{
				appendStringInfo(hbytes, "%s %s", fkl[1], fkl[2]);
				nbytes = h2b(hbytes->data);
				values[i][1] = int64_to_string(nbytes);
			}
			else
				values[i][1] = fkl[1];
		}

		return form_srf(fcinfo, values, nrow, ncol, text_bigint_sig);
	}

	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in file: %s ", meminfo)));

	/* never reached */
	return (Datum) 0;
}

PG_FUNCTION_INFO_V1(pgnodemx_fsinfo);
Datum
pgnodemx_fsinfo(PG_FUNCTION_ARGS)
{
	int		nrow;
	int		ncol;
	char ***values;
	char   *pname = text_to_cstring(PG_GETARG_TEXT_PP(0));

	values = get_statfs_path(pname, &nrow, &ncol);
	return form_srf(fcinfo, values, nrow, ncol, _2_numeric_text_9_numeric_text_sig);
}

#define HDR_LINES	2
PG_FUNCTION_INFO_V1(pgnodemx_network_stats);
Datum
pgnodemx_network_stats(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			ncol = 17;
	char	 ***values = (char ***) palloc(0);
	char	  **lines;
	int			nlines;

	/* read /proc/self/net/dev file */
	lines = read_nlsv(netstat, &nlines);

	/*
	 * These files have two rows we want to skip at the top.
	 * Lines of interest are 17 space separated columns.
	 * First column is the interface name. It has a trailing colon.
	 * Rest of the columns are bigints.
	 */
	if (nlines > HDR_LINES)
	{
		int			j;
		char	  **toks;

		nrow += (nlines - HDR_LINES);
		values = (char ***) repalloc(values, nrow * sizeof(char **));
		for (j = HDR_LINES; j < nlines; ++j)
		{

			size_t		len;
			int			ntok;
			int			k;

			values[j - HDR_LINES] = (char **) palloc(ncol * sizeof(char *));

			toks = parse_ss_line(lines[j], &ntok);
			if (ntok != ncol)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: unexpected number of tokens, %d, in file %s, line %d",
							   ntok, netstat, j + 1)));

			/* token 1 will end with an extraneous colon - strip that */
			len = strlen(toks[0]) - 1;
			toks[0][len] = '\0';
			values[j - HDR_LINES][0] = pstrdup(toks[0]);

			/* second through seventeenth columns are rx and tx stats */
			for (k = 1; k < ncol; ++k)
				values[j - HDR_LINES][k] = pstrdup(toks[k]);
		}
	}
	else
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: no data in file: %s ", netstat)));

	return form_srf(fcinfo, values, nrow, ncol, text_16_bigint_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_kdapi_setof_kv);
Datum
pgnodemx_kdapi_setof_kv(PG_FUNCTION_ARGS)
{
	char	   *fqpath;
	int			nlines;
	char	  **lines;
	int			ncol = 2;

	if (!kdapi_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, text_text_sig);

	fqpath = get_fq_kdapi_path(fcinfo);
	lines = read_nlsv(fqpath, &nlines);
	if (nlines > 0)
	{
		char	 ***values;
		int			nrow = nlines;
		int			i;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			/*
			 * parse_keqv_line always returns two tokens
			 * or throws an error if it cannot.
			 */
			values[i] = parse_keqv_line(lines[i]);
		}

		return form_srf(fcinfo, values, nrow, ncol, text_text_sig);
	}

	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in Kubernetes Downward API file: %s ", fqpath)));

	/* never reached */
	return (Datum) 0;
}

PG_FUNCTION_INFO_V1(pgnodemx_kdapi_scalar_bigint);
Datum
pgnodemx_kdapi_scalar_bigint(PG_FUNCTION_ARGS)
{
	char   *fqpath;

	if (!kdapi_enabled)
		PG_RETURN_NULL();

	fqpath = get_fq_kdapi_path(fcinfo);

	PG_RETURN_INT64(get_int64_from_file(fqpath));
}

PG_FUNCTION_INFO_V1(pgnodemx_fips_mode);
Datum
pgnodemx_fips_mode(PG_FUNCTION_ARGS)
{
	/* Limit use to members of special role */
	pgnodemx_check_role();

#ifdef USE_OPENSSL
	if (FIPS_mode())
		PG_RETURN_BOOL(true);
	else
		PG_RETURN_BOOL(false);
#else
	PG_RETURN_BOOL(false);
#endif
}

PG_FUNCTION_INFO_V1(pgnodemx_symbol_filename);
Datum
pgnodemx_symbol_filename(PG_FUNCTION_ARGS)
{
	const char *sym_name = text_to_cstring(PG_GETARG_TEXT_PP(0));
	const void *sym_addr;
	Dl_info		info;  
	int			rc;
	char	   *msg;

	/* Limit use to members of special role */
	pgnodemx_check_role();

	/* according to the man page, clear any residual error message first */
	msg = dlerror();

	/* grab the symbol address using the default path */
	sym_addr = dlsym(RTLD_DEFAULT, sym_name);

	/* any error means we could not find the symbol by this name */
	msg = dlerror();
	if (msg)
		PG_RETURN_NULL();

	/* grab the source library */
	rc = dladdr(sym_addr, &info);
	if (!rc)
		PG_RETURN_NULL();
	else
	{
		char   *tmppath = realpath(info.dli_fname, NULL);
		char   *rpath;

		if (tmppath == NULL)
			PG_RETURN_NULL();

		rpath = pstrdup(tmppath);
		free(tmppath);

		PG_RETURN_TEXT_P(cstring_to_text(rpath));
	}
}

PG_FUNCTION_INFO_V1(pgnodemx_version);
Datum
pgnodemx_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(GIT_HASH));
}
