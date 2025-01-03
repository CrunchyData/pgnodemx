/*
 * 
 * Functions that allow capture procfs metrics from PostgreSQL
 * Dave Cramer <davecramer@gmail.com>
 * 
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2021-2025 Crunchy Data Solutions, Inc.
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

#include <ctype.h>
#include <linux/magic.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pwd.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "access/htup_details.h"
#include "utils/tuplestore.h"
#include "storage/fd.h"
#include "utils/builtins.h"

#include "fileutils.h"
#include "genutils.h"
#include "parseutils.h"
#include "procfunc.h"
#include "srfsigs.h"

Datum pgnodemx_proc_meminfo(PG_FUNCTION_ARGS);
Datum pgnodemx_fsinfo(PG_FUNCTION_ARGS);
Datum pgnodemx_network_stats(PG_FUNCTION_ARGS);
Datum pgnodemx_proctab(PG_FUNCTION_ARGS);
Datum pgnodemx_proc_cputime(PG_FUNCTION_ARGS);
Datum pgnodemx_proc_loadavg(PG_FUNCTION_ARGS);

static char *get_fullcmd(char *pid);
static void get_uid_username( char *pid, char **uid, char **username );

/* human readable to bytes */
#if PG_VERSION_NUM < 90600
#define h2b(arg1) size_bytes(arg1)
#else
#define h2b(arg1) \
  DatumGetInt64(DirectFunctionCall1(pg_size_bytes, PointerGetDatum(cstring_to_text(arg1))))
#endif

/* various /proc/ source files */
#define PROCFS "/proc"
#define diskstats		PROCFS "/diskstats"
#define mountinfo		PROCFS "/self/mountinfo"
#define meminfo			PROCFS "/meminfo"
#define procstat		PROCFS "/stat"
#define loadavg			PROCFS "/loadavg"
#define netstat			PROCFS "/self/net/dev"
#define pidiofmt		PROCFS "/%s/io"
#define pidcmdfmt		PROCFS "/%s/cmdline"
#define childpidsfmt	PROCFS "/%d/task/%d/children"
#define pidstatfmt		PROCFS "/%s/stat"

extern bool proc_enabled;

/*
 * "/proc" files: these files have all kinds of formats. For now
 * at least do not try to create generic parsing functions. Just
 * create a handful of specific access functions for the most
 * interesting (to us) files.
 */

/*
 * Check to see if procfs exists
 */
bool
check_procfs(void)
{
	struct statfs sb;

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
		return false;
	else
		return true;
}

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
 * Validate either 14,18, or 20 fields found when
 * parsing the lines. Unused fields passed as NULL.
 */
PG_FUNCTION_INFO_V1(pgnodemx_proc_diskstats);
Datum
pgnodemx_proc_diskstats(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			ncol = 20;
	char	 ***values = (char ***) palloc(0);
	char	  **lines;
	int			nlines;

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, proc_diskstats_sig);

	/* read /proc/diskstats file */
	lines = read_nlsv(diskstats, &nlines);

	/*
	 * These files have either 14,18, or 20 fields per line.
	 * Validate one of those lengths. The third column is the device name.
	 * Rest of the columns are unsigned long or unsigned int, which
	 * are mapped to postgres numeric or bigints respectively.
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
			{
				if (k < ntok)
					values[j][k] = pstrdup(toks[k]);
				else
					values[j][k] = NULL;
			}
		}
	}
	else
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: no data in file: %s ", diskstats)));

	return form_srf(fcinfo, values, nrow, ncol, proc_diskstats_sig);
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

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, _4_bigint_6_text_sig);

	/* read /proc/self/mountinfo file */
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

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, text_bigint_sig);

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

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, _2_numeric_text_9_numeric_text_sig);

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

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, text_16_bigint_sig);

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

PG_FUNCTION_INFO_V1(pgnodemx_proc_pid_io);
Datum pgnodemx_proc_pid_io(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			ncol = 8;
	char	  **child_pids;
	pid_t		ppid;
	char	 ***values = (char ***) palloc(0);
	StringInfo	fname = makeStringInfo();

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, int_7_numeric_sig);

	/* Get pid of all client connections. */
	ppid = getppid();
	appendStringInfo(fname, childpidsfmt, ppid, ppid);
	/* read /proc/<ppid>/task/<ppid>/children file */
	child_pids = parse_space_sep_val_file(fname->data, &nrow);

	if (nrow > 0)
	{
		int j;

		/* nrow is the number of child pids we will be getting stats for */
		values = (char ***) repalloc(values, nrow * sizeof(char **));

		/* iterate through child pids */
		for (j = 0; j < nrow; ++j)
		{
			int		nlines;
			char ***iostat;
			int		i;
			int		k = 0;

			values[j] = (char **) palloc(ncol * sizeof(char *));

			/* read io for current child pid */
			resetStringInfo(fname);
			appendStringInfo(fname, pidiofmt, child_pids[j]);
			/* read "/proc/<child-pid>/io file" */
			iostat = read_kv_file(fname->data, &nlines);

			if (nlines != ncol - 1)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: expected %d tokens, got %d in keyed file %s",
							   ncol - 1, nlines, fname->data)));

			/* inject the current child pid number as first column */
			values[j][k++] = pstrdup(child_pids[j]);
			for (i = 0; i < nlines ; i++ )
			{
				/*
				 * We only care about the values, not the keys
				 * because each key gets its own column in the
				 * output.
				 */
				values[j][k++] = pstrdup(iostat[i][1]);
			}
		}

		return form_srf(fcinfo, values, nrow, ncol, int_7_numeric_sig);
	}
	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in flat keyed file: %s ", fname->data)));

	/* never reached */
	return (Datum) 0;
}

PG_FUNCTION_INFO_V1(pgnodemx_proc_pid_cmdline);
Datum pgnodemx_proc_pid_cmdline(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			ncol = 4;
	char	  **child_pids;
	pid_t		ppid;
	char	 ***values = (char ***) palloc(0);
	StringInfo	fname = makeStringInfo();

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, int_text_int_text_sig);

	/* Get pid of all client connections. */
	ppid = getppid();
	appendStringInfo(fname, childpidsfmt, ppid, ppid);
	/* read /proc/<ppid>/task/<ppid>/children file */
	child_pids = parse_space_sep_val_file(fname->data, &nrow);

	if (nrow > 0)
	{
		int j;

		/* nrow is the number of child pids we will be getting stats for */
		values = (char ***) repalloc(values, nrow * sizeof(char **));

		/* iterate through child pids */
		for (j = 0; j < nrow; ++j)
		{
			char   *uid;
			char   *username;

			values[j] = (char **) palloc(ncol * sizeof(char *));

			/* inject the current child pid number as first column */
			values[j][0] = pstrdup(child_pids[j]);

			/* full command line as second column */
			values[j][1] = get_fullcmd(child_pids[j]);

			/* get uid and username for process */
			get_uid_username(child_pids[j], &uid, &username);
			values[j][2] = pstrdup(uid);
			values[j][3] = pstrdup(username);
		}

		return form_srf(fcinfo, values, nrow, ncol, int_text_int_text_sig);
	}
	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in space separated file: %s ", fname->data)));

	/* never reached */
	return (Datum) 0;
}

PG_FUNCTION_INFO_V1(pgnodemx_proc_pid_stat);
Datum pgnodemx_proc_pid_stat(PG_FUNCTION_ARGS)
{
	int			nrow = 0;
	int			ncol = 52;
	char	  **child_pids;
	pid_t		ppid;
	char	 ***values = (char ***) palloc(0);
	StringInfo	fname = makeStringInfo();

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, proc_pid_stat_sig);

	/* Get pid of all client connections. */
	ppid = getppid();
	appendStringInfo(fname, childpidsfmt, ppid, ppid);
	/* read /proc/<ppid>/task/<ppid>/children file */
	child_pids = parse_space_sep_val_file(fname->data, &nrow);

	if (nrow > 0)
	{
		int j;

		/* nrow is the number of child pids we will be getting stats for */
		values = (char ***) repalloc(values, nrow * sizeof(char **));

		/* iterate through child pids */
		for (j = 0; j < nrow; ++j)
		{
			int		ntok;
			char  **toks;
			int		k;
			char   *rawstr;
			char   *line;
			char   *ptr1;
			char   *ptr2;
			int		ch1 = '(';
			int		ch2 = ')';

			/* read stats for current child pid */
			resetStringInfo(fname);
			appendStringInfo(fname, pidstatfmt, child_pids[j]);
			/* read "/proc/<child-pid>/stat file" */
			rawstr = get_string_from_file(fname->data);

			/*
			 * Find the end of the first two fields, and 
			 * advance line pointer to correct position in rawstr
			 * for the rest. While at it, also find the first "("
			 */
 
			/* Find start position of the command string */
			ptr1 = strchr(rawstr, ch1 );
 
			/* Find end position of the command string */
			ptr2 = strrchr(rawstr, ch2 );

			/*
			 * The rest of the line starts 2 bytes after
			 * the command ending parenthesis
			 */
			line = ptr2 + 2;

			/* parse the rest */
			toks = parse_ss_line(line, &ntok);

			/* there should be two columns less in the parsed rest-of-line */
			if ((ntok + 2) != ncol)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: expected %d tokens, got %d in space separated file %s",
							   ncol, ntok + 2, fname->data)));

			values[j] = (char **) palloc(ncol * sizeof(char *));
			
			for (k = 0; k < ncol; ++k) 
			{
				if ( k == 0 ) 
				{
					char   *p = ptr1 - 1;

					/*
					 * first column starts at 0 and goes through
					 * ptr1 - 1
					 */
					p[0] = '\0';
					values[j][k] = pstrdup(rawstr);
				}
				else if ( k == 1 ) 
				{
					char   *p = ptr1 + 1;

					/*
					 * second column (command) is everything inbetween
					 * ptr1 + 1 to ptr2 (which should be a NULL terminator)
					 */
					ptr2[0] = '\0';
					values[j][k] = pstrdup(p);
				}
				else 	
					values[j][k] = pstrdup(toks[k - 2]);
			}
		}

		return form_srf(fcinfo, values, nrow, ncol, proc_pid_stat_sig);
	}
	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in flat keyed file: %s ", fname->data)));

	/* never reached */
	return (Datum) 0;
}

/*
 * Returns full command line of a postgres pid
 * 
 * Note that this would not work with pids in general
 * because the /proc/pid/cmdline file typically separates
 * the command line options using NULL bytes ('\0'). But
 * for postgres they are rewritten as full strings for
 * clear ps output.
 */
static char *
get_fullcmd(char *pid)
{
	StringInfo	fname = makeStringInfo();

	/* calculate filename of interest */
	appendStringInfo(fname, pidcmdfmt, pid);

	/* read /proc/<ppid>/cmdline file */
	return get_string_from_file(fname->data);
}

#define INTEGER_LEN 64
static void
get_uid_username( char *pid, char **uid, char **username )
{
	struct stat stat_struct;
	char tmp[INTEGER_LEN];
	/* Get the uid and username of the pid's owner. */
	snprintf(tmp, sizeof(tmp) - 1, "%s/%s", PROCFS, pid);
	if (stat(tmp, &stat_struct) < 0)
	{
		elog(ERROR, "'%s' not found", tmp);
		*uid = pstrdup("-1");
		*username = NULL;
	}
	else
	{
		struct passwd *pwd;

		snprintf(tmp, INTEGER_LEN, "%" PRIuMAX, (uintmax_t) stat_struct.st_uid);
		*uid = pstrdup(tmp);
		pwd = getpwuid(stat_struct.st_uid);
		if (pwd == NULL)
			*username = NULL;
		else
		{
			*username = pstrdup(pwd->pw_name);
		}
	}
}

PG_FUNCTION_INFO_V1(pgnodemx_proc_cputime);
Datum pgnodemx_proc_cputime(PG_FUNCTION_ARGS)
{
	int			nrow = 1;
	int			ncol = 5;
	char	 ***values = (char ***) palloc(0);
	char	  **lines;
	int			nlines;
	char	  **tokens;
	int			ntok;

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, _5_bigint_sig);

	lines = read_nlsv(procstat, &nlines);
	/* currently only interested in the first part of the first line */
	if (nlines < 1)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: got too few lines in file %s", procstat)));

	tokens = parse_ss_line(lines[0], &ntok);
	if (ntok < (ncol + 1))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: got too few values in file %s", procstat)));

	values = (char ***) repalloc(values, nrow * sizeof(char **));
	values[0] = (char **) palloc(ncol * sizeof(char *));
	values[0][0] = pstrdup(tokens[1]);
	values[0][1] = pstrdup(tokens[2]);
	values[0][2] = pstrdup(tokens[3]); 
	values[0][3] = pstrdup(tokens[4]);
	values[0][4] = pstrdup(tokens[5]);

	return form_srf(fcinfo, values, nrow, ncol, _5_bigint_sig);
}

PG_FUNCTION_INFO_V1(pgnodemx_proc_loadavg);
Datum pgnodemx_proc_loadavg(PG_FUNCTION_ARGS)
{
	int			nrow = 1;
	int			ncol = 4;
	char	 ***values = (char ***) palloc(0);
	char	   *rawstr;
	char	  **tokens;
	int			ntok;

	if (!proc_enabled)
		return form_srf(fcinfo, NULL, 0, ncol, load_avg_sig);

	rawstr = read_one_nlsv(loadavg);
	tokens = parse_ss_line(rawstr, &ntok);
	if (ntok < (ncol + 1))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: got too few values in file %s", loadavg)));

	values = (char ***) repalloc(values, nrow * sizeof(char **));
	values[0] = (char **) palloc(ncol * sizeof(char *));
	values[0][0] = pstrdup(tokens[0]);
	values[0][1] = pstrdup(tokens[1]);
	values[0][2] = pstrdup(tokens[2]); 
	/* skip running/tasks */
	values[0][3] = pstrdup(tokens[4]);

	return form_srf(fcinfo, values, nrow, ncol, load_avg_sig);
}
