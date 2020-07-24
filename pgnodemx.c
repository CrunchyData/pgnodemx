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

#if PG_VERSION_NUM < 100000
#error "pgnodemx only builds with PostgreSQL 10 or later"
#endif

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
#include "parseutils.h"

PG_MODULE_MAGIC;

/* human readable to bytes */
#define h2b(arg1) \
  DatumGetInt64(DirectFunctionCall1(pg_size_bytes, PointerGetDatum(cstring_to_text(arg1))))
/* various /proc/ source files */
#define meminfo		"/proc/meminfo"
#define netstatfmt	"/proc/%ld/net/dev"

/* function return signatures */
Oid text_sig[] = {TEXTOID};
Oid bigint_sig[] = {INT8OID};
Oid text_text_sig[] = {TEXTOID, TEXTOID};
Oid text_bigint_sig[] = {TEXTOID, INT8OID};
Oid text_text_bigint_sig[] = {TEXTOID, TEXTOID, INT8OID};
Oid text_text_float8_sig[] = {TEXTOID, TEXTOID, FLOAT8OID};
Oid text_9_bigint_text_sig[] = {TEXTOID, INT8OID, INT8OID, INT8OID,
										 INT8OID, INT8OID, INT8OID,
										 INT8OID, INT8OID, INT8OID, TEXTOID};
Oid text_17_bigint_sig[] = {TEXTOID, INT8OID,
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
Datum pgnodemx_cgroup_setof_kv(PG_FUNCTION_ARGS);
Datum pgnodemx_cgroup_setof_nkv(PG_FUNCTION_ARGS);

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

	DefineCustomBoolVariable("pgnodemx.cgroupfs_enabled",
							 "True if cgroup virtual file system access is enabled",
							 NULL, &cgroupfs_enabled, true, PGC_POSTMASTER /* PGC_SIGHUP */,
							 0, NULL, NULL, NULL);

	DefineCustomBoolVariable("pgnodemx.containerized",
							 "True if operating inside a container",
							 NULL, &containerized, false, PGC_POSTMASTER /* PGC_SIGHUP */,
							 0, NULL, NULL, NULL);

	DefineCustomStringVariable("pgnodemx.cgrouproot",
							   "Path to root cgroup",
							   NULL, &cgrouproot, "/sys/fs/cgroup", PGC_POSTMASTER /* PGC_SIGHUP */,
							   0, NULL, NULL, NULL);

	/* don't try to set cgmode unless cgroupfs is enabled */
	if (set_cgmode())
	{
		/* must determine if containerized before setting cgpath */
		set_containerized();
		set_cgpath();
	}
	else
	{
		/*
		 * If cgmode cannot be set, either because cgroupfs_enabled is
		 * already set to false, or because of an error trying to stat
		 * cgrouproot, then we must force disable cgroup functions. 
		 */
		cgroupfs_enabled = false;
	}

    inited = true;
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_mode);
Datum
pgnodemx_cgroup_mode(PG_FUNCTION_ARGS)
{
	/*
	 * Do not check cgroupfs_enabled here; this is the one cgroup
	 * function which *should* work when cgroupfs is disabled.
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

	if (unlikely(!cgroupfs_enabled))
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

	if (unlikely(!cgroupfs_enabled))
		PG_RETURN_NULL();

	/* cgmembers returns pid count */
	PG_RETURN_INT32(cgmembers(&cgpids));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_scalar_bigint);
Datum
pgnodemx_cgroup_scalar_bigint(PG_FUNCTION_ARGS)
{
	char   *fqpath;

	if (unlikely(!cgroupfs_enabled))
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	PG_RETURN_INT64(get_int64_from_file(fqpath));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_scalar_float8);
Datum
pgnodemx_cgroup_scalar_float8(PG_FUNCTION_ARGS)
{
	char   *fqpath;

	if (unlikely(!cgroupfs_enabled))
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	PG_RETURN_FLOAT8(get_double_from_file(fqpath));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_scalar_text);
Datum
pgnodemx_cgroup_scalar_text(PG_FUNCTION_ARGS)
{
	char   *fqpath;

	if (unlikely(!cgroupfs_enabled))
		PG_RETURN_NULL();

	fqpath = get_fq_cgroup_path(fcinfo);

	PG_RETURN_TEXT_P(cstring_to_text(get_string_from_file(fqpath)));
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_setof_bigint);
Datum
pgnodemx_cgroup_setof_bigint(PG_FUNCTION_ARGS)
{
	char	   *fqpath;

	if (unlikely(!cgroupfs_enabled))
		return form_srf(fcinfo, NULL, 0, 1, bigint_sig);

	fqpath = get_fq_cgroup_path(fcinfo);
	return setof_scalar_internal(fcinfo, fqpath, bigint_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_cgroup_setof_text);
Datum
pgnodemx_cgroup_setof_text(PG_FUNCTION_ARGS)
{
	char	   *fqpath;

	if (unlikely(!cgroupfs_enabled))
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
	bool	isnull;
	Datum	dvalue;

	if (unlikely(!cgroupfs_enabled))
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
	bool	isnull;
	Datum	dvalue;
	int		i;

	if (unlikely(!cgroupfs_enabled))
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

	if (unlikely(!cgroupfs_enabled))
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

	if (unlikely(!cgroupfs_enabled))
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
				values[i] = repalloc(values[i], ncol * sizeof(char **));
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

	if (unlikely(!cgroupfs_enabled))
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

	/* Limit use to members of the 'pg_monitor' role */
	if (!is_member_of_role(GetUserId(), DEFAULT_ROLE_MONITOR))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be member of pg_monitor role")));

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

	/* Limit use to members of the 'pg_monitor' role */
	if (!is_member_of_role(GetUserId(), DEFAULT_ROLE_MONITOR))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be member of pg_monitor role")));

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

/*
 * "/proc" files: these files have all kinds of formats. For now
 * at least do not try to create generic parsing functions. Just
 * create a handful of specific access functions for the most
 * interesting (to us) files.
 */
PG_FUNCTION_INFO_V1(pgnodemx_fsinfo);
Datum
pgnodemx_fsinfo(PG_FUNCTION_ARGS)
{
	int		nrow;
	int		ncol;
	char ***values;
	char   *pname = text_to_cstring(PG_GETARG_TEXT_PP(0));

	values = get_statfs_path(pname, &nrow, &ncol);
	return form_srf(fcinfo, values, nrow, ncol, text_9_bigint_text_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_network_stats);
Datum
pgnodemx_network_stats(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			crow = 0;
	int			ncol = 18;
	int			npids;
	int64	   *cgpids;
	int			i;
	char	 ***values = (char ***) palloc(0);

	/* get list of all cgroup pids and pid count */
	npids = cgmembers(&cgpids);
	for (i = 0; i < npids; ++i)
	{
		StringInfo	ftr = makeStringInfo();
		int			nlines;
		char	  **lines;

		/* read each /proc/<pid>/net/dev file */
		appendStringInfo(ftr, netstatfmt, cgpids[i]);
		lines = read_nlsv(ftr->data, &nlines);

		/*
		 * These files have two rows we want to skip at the top.
		 * Lines of interest are 17 space separated columns.
		 * First column is the interface name. It has a trailing colon.
		 * Rest of the columns are bigints.
		 * We want to insert the pid as column two in the output.
		 */
		if (nlines > 2)
		{
			int			j;
			char	  **toks;

			nrow += (nlines - 2);
			values = (char ***) repalloc(values, nrow * sizeof(char **));
			for (j = 2; j < nlines; ++j)
			{

				size_t		len;
				int			ntok;
				int			k;

				values[crow] = (char **) palloc(ncol * sizeof(char *));

				toks = parse_ss_line(lines[j], &ntok);
				if (ntok != ncol - 1)
					ereport(ERROR,
							(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
							errmsg("pgnodemx: unexpected number of tokens, %d, in file %s, line %d",
								   ntok, ftr->data, j + 1)));

				/* token 1 will end with an extraneous colon - strip that */
				len = strlen(toks[0]) - 1;
				toks[0][len] = '\0';
				values[crow][0] = pstrdup(toks[0]);

				/* second column is our pid */
				values[crow][1] = pstrdup(int64_to_string(cgpids[i]));

				/* third through eighteenth columns are rx and tx stats */
				for (k = 1; k < ncol - 1; ++k)
					values[crow][k + 1] = pstrdup(toks[k]);

				++crow;
			}
		}
		else
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					errmsg("pgnodemx: no data in file: %s ", ftr->data)));
	}

	if (crow == 0)
		return form_srf(fcinfo, NULL, 0, ncol, text_17_bigint_sig);

	return form_srf(fcinfo, values, nrow, ncol, text_17_bigint_sig);
}

