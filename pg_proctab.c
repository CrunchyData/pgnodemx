/*
 * Copyright (C) 2008 Mark Wong
 */

#include "postgres.h"
#include <string.h>
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "access/htup_details.h"
#include "utils/tuplestore.h"
#include "storage/fd.h"
#include "utils/builtins.h"
#include <sys/vfs.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include "genutils.h"
#include "parseutils.h"
#include "pg_proctab.h"

#define FULLCOMM_LEN 1024
char *get_fullcmd(char *pid);
char *get_rss(char *rss);
char ***read_kv_file( char *filename, int *nlines );

// from pg_proctab.c
#define NUM_COLS 39
					/* pid INTEGER, comm TEXT, fullcomm TEXT, state TEXT */
Oid proctab_sig[] = {INT4OID, TEXTOID, TEXTOID, TEXTOID, 
					/* 4 ppid INTEGER, pgrp INTEGER, session INTEGER, tty_nr INTEGER */
					INT4OID, INT4OID, INT4OID, INT4OID,
					/* 8 tpgid INTEGER, flags INTEGER, minflt BIGINT, cminflt BIGINT */
					INT4OID, INT4OID, INT8OID, INT8OID,
					/* 12 majflt BIGINT, cmajflt BIGINT, utime BIGINT, stime BIGINT */
					INT8OID, INT8OID, INT8OID, INT8OID,
					/* 16 cutime BIGINT, cstime BIGINT, priority BIGINT, nice BIGINT */
					INT8OID, INT8OID, INT8OID, INT8OID,
					/* 20 num_threads BIGINT, itrealvalue BIGINT, starttime NUMERIC, vsize BIGINT */
					INT8OID, INT8OID, NUMERICOID, INT8OID,
					/* 24 rss NUMERIC, exit_signal INTEGER, processor INTEGER, rt_priority BIGINT */
					NUMERICOID, INT4OID, INT4OID, INT8OID,
					/* 28 policy BIGINT, delayacct_blkio_ticks NUMERIC, uid INTEGER, username VARCHAR */
					INT8OID, NUMERICOID, INT4OID, TEXTOID,
					/* 32 rchar BIGINT, wchar BIGINT, syscr BIGINT, syscw BIGINT */
					INT8OID, INT8OID, INT8OID, INT8OID,
					/* 36 reads BIGINT, writes BIGINT,cwrites BIGINT */
					INT8OID, INT8OID, INT8OID
					};
/*
0 11750 (postmaster) S 1  
4 11750 11750 0 -1 
8 4210944 3948 13103 0
12 0 1 2 3 
16 5 20 0 1 
20 0 8242695 296566784 6236 
24 18446744073709551615 1305 18446744073709551615 4194304 
28 12252472 140728896963568 0 0 0 
32 0 4194304 24147974 536871425
36 0 0 0 17
40 1 0 0 0
44 0 0 14352560 14546418 
41533440 140728896970319 140728896970375 140728896970375 140728896970715 
0
*/

#define GET_VALUE(value) \
		p = strchr(p, ':'); \
		++p; \
		++p; \
		q = strchr(p, '\n'); \
		len = q - p; \
		if (len >= BIGINT_LEN) \
		{ \
			elog(ERROR, "value is larger than the buffer: %d\n", __LINE__); \
			return 0; \
		} \
		strncpy(value, p, len); \
		value[len] = '\0';

#define pagetok(x)	((x) * sysconf(_SC_PAGESIZE) >> 10)

enum proctab {i_pid, i_comm, i_fullcomm, i_state, i_ppid, i_pgrp, i_session,
		i_tty_nr, i_tpgid, i_flags, i_minflt, i_cminflt, i_majflt, i_cmajflt,
		i_utime, i_stime, i_cutime, i_cstime, i_priority, i_nice,
		i_num_threads, i_itrealvalue, i_starttime, i_vsize, i_rss,
		i_exit_signal, i_processor, i_rt_priority, i_policy,
		i_delayacct_blkio_ticks, i_uid, i_username, i_rchar, i_wchar, i_syscr,
		i_syscw, i_reads, i_writes, i_cwrites};
enum cputime {i_user, i_nice_c, i_system, i_idle, i_iowait};
enum loadavg {i_load1, i_load5, i_load15, i_last_pid};
enum memusage {i_memused, i_memfree, i_memshared, i_membuffers, i_memcached,
		i_swapused, i_swapfree, i_swapcached};
enum diskusage {i_major, i_minor, i_devname,
		i_reads_completed, i_reads_merged, i_sectors_read, i_readtime,
		i_writes_completed, i_writes_merged, i_sectors_written, i_writetime,
		i_current_io, i_iotime, i_totaliotime,
		i_discards_completed, i_discards_merged, i_sectors_discarded, i_discardtime,
		i_flushes_completed, i_flushtime};

int get_proctab(FuncCallContext *, char **);
int get_cputime(char **);
int get_loadavg(char **);
int get_memusage(char **);

Datum pg_proctab(PG_FUNCTION_ARGS);
Datum pg_cputime(PG_FUNCTION_ARGS);
Datum pg_loadavg(PG_FUNCTION_ARGS);
Datum pg_memusage(PG_FUNCTION_ARGS);
Datum pg_diskusage(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_proctab);
PG_FUNCTION_INFO_V1(pg_cputime);
PG_FUNCTION_INFO_V1(pg_loadavg);
PG_FUNCTION_INFO_V1(pg_memusage);
PG_FUNCTION_INFO_V1(pg_diskusage);

Datum pg_proctab(PG_FUNCTION_ARGS)
{
	char buffer[256];
	char **child_pids;
	int  ntok;
	int  ncol = 42;
	pid_t ppid;
	int nlines;
	char ***iostat;

	char  ***values = (char ***) palloc(0);

	elog(DEBUG5, "pg_proctab: Entering stored function.");

	/* Get pid of all client connections. */

	ppid = getppid();
	snprintf(buffer, sizeof(buffer) - 1, "/proc/%d/task/%d/children", ppid, ppid);
	child_pids = parse_space_sep_val_file(buffer, &ntok);

	if (ntok > 0) 
	{
		int j;
		int nchildren = ntok;
		/* ntok is the number of children pids we will be getting stats for */
		values = (char ***) repalloc(values, nchildren * sizeof(char **));
		
		for (j = 0; j < nchildren; ++j)
		{
			int	 ntok;
			char **toks;
			int	 k,l;
			
			/* read stats for each child pid */
			snprintf(buffer, sizeof(buffer) - 1, "/proc/%s/stat", child_pids[j]);

			toks = parse_space_sep_val_file(buffer, &ntok);

			if (ntok != 52)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: expected %d tokens, got %d in flat keyed file %s, line %d",
							   ncol, ntok, buffer, j + 1)));

			values[j] = (char **) palloc(NUM_COLS * sizeof(char *));
			
			for (k = 0, l = 0; k < ncol; ++k) 
			{
				if ( k == 1 )
				{
					/* strip () from the cmd */
					int len = strlen(toks[k]);
					char *cmd = palloc( len * sizeof (char) );
					char *dest = cmd;
					char *src = toks[k];
					while( *src != '\0')
					{
						if (*src == '(') 
						{	src++;
							continue;
						}
						else if (*src == ')')
						{	*cmd = '\0';
							break;
						}
						*cmd++ = *src++;	
					}
					values[j][l++] = dest;
				}
				else if ( k == 2 ) 
				{
					/* need to get long version of command line here */
					values[j][l++] = get_fullcmd(child_pids[j]);
					/* need to add status */
					values[j][l++] = pstrdup(toks[k]);
				}
				else if ( k == 24 )
					/* rss in pages */
					values[j][l++] = pstrdup( get_rss(toks[k]) );
				else if ( k > 24 && k <= 37 )
					/* skip these values */
					continue;
				else 	
				{
					elog(DEBUG1, "values[%d] == %s ",l, toks[k]);
					values[j][l++] = pstrdup(toks[k]);
				}
			}
			values[j][l++] = pstrdup("1234"); // uid
			values[j][l++] = pstrdup("postgres"); // username

			snprintf(buffer, sizeof(buffer) - 1, "%s/%s/io", PROCFS, child_pids[j]);
			
			iostat = read_kv_file(buffer, &nlines);

			if ( nlines != 7)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("pgnodemx: expected %d tokens, got %d in keyed file %s, pid %d",
							   7, nlines, buffer, j + 1)));

			for (int i = 0; i < 7 ; i++ )
			{
				values[j][l++] = pstrdup(iostat[i][1]);
			}

		}

		return form_srf(fcinfo, values, nchildren, NUM_COLS, proctab_sig);
	}
	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			errmsg("pgnodemx: no lines in flat keyed file: %s ", buffer)));

	/* never reached */
	return (Datum) 0;
}

/*
	returns full command line of pid otherwise NULL
*/
char *get_fullcmd(char *pid)
{
	char buffer[256];
	int fd = -1;
	int len;

	/* Get the full command line information. */
	snprintf(buffer, sizeof(buffer) - 1, "%s/%s/cmdline", PROCFS, pid);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		return NULL;
	}
	else
	{
		char *full_cmd = (char *) palloc((FULLCOMM_LEN + 1) * sizeof(char));
		len = read(fd, full_cmd, FULLCOMM_LEN);
		close(fd);
		full_cmd[len] = '\0';
		return full_cmd;
	}
	return NULL;
}

char *get_rss(char *rss)
{
	char *rss_str = palloc(256 * sizeof(char));
	snprintf(rss_str, (256 * sizeof(char)) -1, "%lld", pagetok(atoll(rss)) );
	return rss_str;
}

char ***read_kv_file( char *fname, int *nlines )
{
	char **lines = read_nlsv(fname, nlines);	
	if (nlines > 0)
	{
		char	 ***values;
		int		 nrow = *nlines;
		int		 i;

		values = (char ***) palloc(nrow * sizeof(char **));
		for (i = 0; i < nrow; ++i)
		{
			int	ntok;

			values[i] = parse_ss_line(lines[i], &ntok);
		
		}
		return values;
	}
	return NULL;
}



#ifdef XXX
int
get_proctab(FuncCallContext *funcctx, char **values)
{
	/*
 	* For details on the Linux process table, see the description of
 	* /proc/PID/stat in Documentation/filesystems/proc.txt in the Linux source
 	* code.
 	*/

	int32 *ppid;
	int32 pid;
	int length;

	int			nlines;
	char	  **lines;
	
	struct stat stat_struct;

	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;
	char ***values;

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		return 0;
	}

	/* Read the stat info for the pid. */

	ppid = (int32 *) funcctx->user_fctx;
	pid = ppid[funcctx->call_cntr];
#if (PG_VERSION_NUM >= 90600)
	elog(DEBUG5, "pg_proctab: accessing process table for pid[%lu] %d.",
				funcctx->call_cntr, pid);
#else
	elog(DEBUG5, "pg_proctab: accessing process table for pid[%d] %d.",
				funcctx->call_cntr, pid);
#endif

	/* Get the full command line information. */
	snprintf(buffer, sizeof(buffer) - 1, "%s/%d/cmdline", PROCFS, pid);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		values[i_fullcomm] = NULL;
	}
	else
	{
		values[i_fullcomm] =
				(char *) palloc((FULLCOMM_LEN + 1) * sizeof(char));
		len = read(fd, values[i_fullcomm], FULLCOMM_LEN);
		close(fd);
		values[i_fullcomm][len] = '\0';
	}
	elog(DEBUG5, "pg_proctab: %s %s", buffer, values[i_fullcomm]);

	/* Get the uid and username of the pid's owner. */
	snprintf(buffer, sizeof(buffer) - 1, "%s/%d", PROCFS, pid);
	if (stat(buffer, &stat_struct) < 0)
	{
		elog(ERROR, "'%s' not found", buffer);
		strncpy(values[i_uid], "-1", INTEGER_LEN);
		values[i_username] = NULL;
	}
	else
	{
		struct passwd *pwd;

		snprintf(values[i_uid], INTEGER_LEN, "%d", stat_struct.st_uid);
		pwd = getpwuid(stat_struct.st_uid);
		if (pwd == NULL)
			values[i_username] = NULL;
		else
		{
			values[i_username] = (char *) palloc((strlen(pwd->pw_name) +
					1) * sizeof(char));
			strcpy(values[i_username], pwd->pw_name);
		}
	}

	/* Get the process table information for the pid. */
	snprintf(buffer, sizeof(buffer) - 1, "%s/%d/stat", PROCFS, pid);
	lines = read_nlsv(buffer, &nlines);
	if (nlines > 0)
	{
		int		j;
		char	**toks;
		int 	nrow;

		nrow = nlines;
		values = (char ***) repalloc(values, 39 * sizeof(char **));
		for (j = 0; j < nrow; ++j)
		{
			int			ntok;
			int			k;

			values[j] = (char **) palloc(ncol * sizeof(char *));

			toks = parse_ss_line(lines[j], &ntok);

			for (k = 0; k < ncol; ++k)
				switch (k) {
					case i_pid:
					case i_comm:
					case i_state:
					case i_pid:
					case i_ppid:
					case i_pgrp:
					case i_session:
					case i_tty_nr:
					case i_tpgid:
					case i_flags:
					case i_minflt:
					case i_cminflt:
					case i_majflt:
					case i_cmajflt:
					case i_utime:
					case i_stime:
					case i_cutime:
					case i_cstime:
					case i_priority:
					case i_num_threads:
					case i_itrealvalue:
					case i_starttime:
					case i_vsize:
					case i_exit_signal:
					case i_processor:
					case i_rt_priority:
					case i_policy:
					case i_delayacct_blkio_ticks:
						values[j][k] = pstrdup(toks[k]);
					case i_rss:
						/*
						 Convert rss into bytes. 
						snprintf(values[i_rss], sizeof(values[i_rss]) -1, "%lld",
						pagetok(atoll(values[i_rss])));
						*/
				} 
		}
	}
	else
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pgnodemx: no data in file: %s ", buffer)));

	return form_srf(fcinfo, values, nrow, ncol, bigint_bigint_text_11_bigint_sig);

	/* exit_signal */
	GET_NEXT_VALUE(p, q, values[i_exit_signal], length,
			"exit_signal not found", ' ');

	/* processor */
	GET_NEXT_VALUE(p, q, values[i_processor], length, "processor not found",
			' ');

	/* rt_priority */
	GET_NEXT_VALUE(p, q, values[i_rt_priority], length,
			"rt_priority not found", ' ');

	/* policy */
	GET_NEXT_VALUE(p, q, values[i_policy], length, "policy not found", ' ');

	/* delayacct_blkio_ticks */
	/*
	 * It appears sometimes this is the last item in /proc/PID/stat and
	 * sometimes it's not, depending on the version of the kernel and
	 * possibly the architecture.  So first test if it is the last item
	 * before determining how to deliminate it.
	 */
	if (strchr(p, ' ') == NULL)
	{
		GET_NEXT_VALUE(p, q, values[i_delayacct_blkio_ticks], length,
				"delayacct_blkio_ticks not found", '\n');
	}
	else
	{
		GET_NEXT_VALUE(p, q, values[i_delayacct_blkio_ticks], length,
				"delayacct_blkio_ticks not found", ' ');
	}

	/* Get i/o stats per process. */

	snprintf(buffer, sizeof(buffer) - 1, "%s/%d/io", PROCFS, pid);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		/* If the i/o stats are not available, set the values to zero. */
		elog(NOTICE, "i/o stats collection for Linux not enabled");
		strncpy(values[i_rchar], "0", BIGINT_LEN);
		strncpy(values[i_wchar], "0", BIGINT_LEN);
		strncpy(values[i_syscr], "0", BIGINT_LEN);
		strncpy(values[i_syscw], "0", BIGINT_LEN);
		strncpy(values[i_reads], "0", BIGINT_LEN);
		strncpy(values[i_writes], "0", BIGINT_LEN);
		strncpy(values[i_cwrites], "0", BIGINT_LEN);
	}
	else
	{
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
		buffer[len] = '\0';
		p = buffer;
		GET_VALUE(values[i_rchar]);
		GET_VALUE(values[i_wchar]);
		GET_VALUE(values[i_syscr]);
		GET_VALUE(values[i_syscw]);
		GET_VALUE(values[i_reads]);
		GET_VALUE(values[i_writes]);
		GET_VALUE(values[i_cwrites]);
	}

	elog(DEBUG5, "pg_proctab: [%d] uid %s", (int) i_uid, values[i_uid]);
	elog(DEBUG5, "pg_proctab: [%d] username %s", (int) i_username,
			values[i_username]);
	elog(DEBUG5, "pg_proctab: [%d] pid = %s", (int) i_pid, values[i_pid]);
	elog(DEBUG5, "pg_proctab: [%d] comm = %s", (int) i_comm, values[i_comm]);
	elog(DEBUG5, "pg_proctab: [%d] fullcomm = %s", (int) i_fullcomm,
			values[i_fullcomm]);
	elog(DEBUG5, "pg_proctab: [%d] state = %s", (int) i_state,
			values[i_state]);
	elog(DEBUG5, "pg_proctab: [%d] ppid = %s", (int) i_ppid, values[i_ppid]);
	elog(DEBUG5, "pg_proctab: [%d] pgrp = %s", (int) i_pgrp, values[i_pgrp]);
	elog(DEBUG5, "pg_proctab: [%d] session = %s", (int) i_session,
			values[i_session]);
	elog(DEBUG5, "pg_proctab: [%d] tty_nr = %s", (int) i_tty_nr,
			values[i_tty_nr]);
	elog(DEBUG5, "pg_proctab: [%d] tpgid = %s", (int) i_tpgid,
			values[i_tpgid]);
	elog(DEBUG5, "pg_proctab: [%d] flags = %s", (int) i_flags,
			values[i_flags]);
	elog(DEBUG5, "pg_proctab: [%d] minflt = %s", (int) i_minflt,
			values[i_minflt]);
	elog(DEBUG5, "pg_proctab: [%d] cminflt = %s", (int) i_cminflt,
			values[i_cminflt]);
	elog(DEBUG5, "pg_proctab: [%d] majflt = %s", (int) i_majflt,
			values[i_majflt]);
	elog(DEBUG5, "pg_proctab: [%d] cmajflt = %s", (int) i_cmajflt,
			values[i_cmajflt]);
	elog(DEBUG5, "pg_proctab: [%d] utime = %s", (int) i_utime,
			values[i_utime]);
	elog(DEBUG5, "pg_proctab: [%d] stime = %s", (int) i_stime,
			values[i_stime]);
	elog(DEBUG5, "pg_proctab: [%d] cutime = %s", (int) i_cutime,
			values[i_cutime]);
	elog(DEBUG5, "pg_proctab: [%d] cstime = %s", (int) i_cstime,
			values[i_cstime]);
	elog(DEBUG5, "pg_proctab: [%d] priority = %s", (int) i_priority,
			values[i_priority]);
	elog(DEBUG5, "pg_proctab: [%d] nice = %s", (int) i_nice, values[i_nice]);
	elog(DEBUG5, "pg_proctab: [%d] num_threads = %s", (int) i_num_threads,
			values[i_num_threads]);
	elog(DEBUG5, "pg_proctab: [%d] itrealvalue = %s", (int) i_itrealvalue,
			values[i_itrealvalue]);
	elog(DEBUG5, "pg_proctab: [%d] starttime = %s", (int) i_starttime,
			values[i_starttime]);
	elog(DEBUG5, "pg_proctab: [%d] vsize = %s", (int) i_vsize,
			values[i_vsize]);
	elog(DEBUG5, "pg_proctab: [%d] rss = %s", (int) i_rss, values[i_rss]);
	elog(DEBUG5, "pg_proctab: [%d] exit_signal = %s", (int) i_exit_signal,
			values[i_exit_signal]);
	elog(DEBUG5, "pg_proctab: [%d] processor = %s", (int) i_processor,
			values[i_processor]);
	elog(DEBUG5, "pg_proctab: [%d] rt_priority = %s", (int) i_rt_priority,
			values[i_rt_priority]);
	elog(DEBUG5, "pg_proctab: [%d] policy = %s", (int) i_policy,
			values[i_policy]);
	elog(DEBUG5, "pg_proctab: [%d] delayacct_blkio_ticks = %s",
			(int) i_delayacct_blkio_ticks, values[i_delayacct_blkio_ticks]);
	elog(DEBUG5, "pg_proctab: [%d] rchar = %s", (int) i_rchar,
			values[i_rchar]);
	elog(DEBUG5, "pg_proctab: [%d] wchar = %s", (int) i_wchar,
			values[i_wchar]);
	elog(DEBUG5, "pg_proctab: [%d] syscr = %s", (int) i_syscr,
			values[i_syscr]);
	elog(DEBUG5, "pg_proctab: [%d] syscw = %s", (int) i_syscw,
			values[i_syscw]);
	elog(DEBUG5, "pg_proctab: [%d] reads = %s", (int) i_reads,
			values[i_reads]);
	elog(DEBUG5, "pg_proctab: [%d] writes = %s", (int) i_writes,
			values[i_writes]);
	elog(DEBUG5, "pg_proctab: [%d] cwrites = %s", (int) i_cwrites,
			values[i_cwrites]);

	return 1;
}

#endif

Datum pg_cputime(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;

	elog(DEBUG5, "pg_cputime: Entering stored function.");

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Build a tuple descriptor for our result type */
		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("function returning record called in context "
							"that cannot accept type record")));

		/*
		 * generate attribute metadata needed later to produce tuples from raw
		 * C strings
		 */
		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		funcctx->max_calls = 1;

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	call_cntr = funcctx->call_cntr;
	max_calls = funcctx->max_calls;
	attinmeta = funcctx->attinmeta;

	if (call_cntr < max_calls) /* do when there is more left to send */
	{
		HeapTuple tuple;
		Datum result;

		char **values = NULL;

		values = (char **) palloc(5 * sizeof(char *));
		values[i_user] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_nice_c] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_system] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_idle] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_iowait] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));

		if (get_cputime(values) == 0)
			SRF_RETURN_DONE(funcctx);

		/* build a tuple */
		tuple = BuildTupleFromCStrings(attinmeta, values);

		/* make the tuple into a datum */
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}
	else /* do when there is no more left */
	{
		SRF_RETURN_DONE(funcctx);
	}
}

int
get_cputime(char **values)
{
	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;

	int length;

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		return 0;
	}

	snprintf(buffer, sizeof(buffer) - 1, "%s/stat", PROCFS);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		return 0;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	buffer[len] = '\0';
	elog(DEBUG5, "pg_cputime: %s", buffer);

	p = buffer;

	SKIP_TOKEN(p);			/* skip cpu */

	/* user */
	GET_NEXT_VALUE(p, q, values[i_user], length, "user not found", ' ');

	/* nice */
	GET_NEXT_VALUE(p, q, values[i_nice_c], length, "nice not found", ' ');

	/* system */
	GET_NEXT_VALUE(p, q, values[i_system], length, "system not found", ' ');

	/* idle */
	GET_NEXT_VALUE(p, q, values[i_idle], length, "idle not found", ' ');

	/* iowait */
	GET_NEXT_VALUE(p, q, values[i_iowait], length, "iowait not found", ' ');

	elog(DEBUG5, "pg_cputime: [%d] user = %s", (int) i_user, values[i_user]);
	elog(DEBUG5, "pg_cputime: [%d] nice = %s", (int) i_nice_c, values[i_nice_c]);
	elog(DEBUG5, "pg_cputime: [%d] system = %s", (int) i_system,
			values[i_system]);
	elog(DEBUG5, "pg_cputime: [%d] idle = %s", (int) i_idle, values[i_idle]);
	elog(DEBUG5, "pg_cputime: [%d] iowait = %s", (int) i_iowait,
			values[i_iowait]);

	return 1;
}

Datum pg_loadavg(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;

	elog(DEBUG5, "pg_loadavg: Entering stored function.");

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Build a tuple descriptor for our result type */
		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("function returning record called in context "
							"that cannot accept type record")));

		/*
		 * generate attribute metadata needed later to produce tuples from raw
		 * C strings
		 */
		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		funcctx->max_calls = 1;

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	call_cntr = funcctx->call_cntr;
	max_calls = funcctx->max_calls;
	attinmeta = funcctx->attinmeta;

	if (call_cntr < max_calls) /* do when there is more left to send */
	{
		HeapTuple tuple;
		Datum result;

		char **values = NULL;

		values = (char **) palloc(4 * sizeof(char *));
		values[i_load1] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_load5] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_load15] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_last_pid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));

		if (get_loadavg(values) == 0)
			SRF_RETURN_DONE(funcctx);

		/* build a tuple */
		tuple = BuildTupleFromCStrings(attinmeta, values);

		/* make the tuple into a datum */
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}
	else /* do when there is no more left */
	{
		SRF_RETURN_DONE(funcctx);
	}
}

int
get_loadavg(char **values)
{
	int length;

	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		return 0;
	}

	snprintf(buffer, sizeof(buffer) - 1, "%s/loadavg", PROCFS);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		return 0;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	buffer[len] = '\0';
	elog(DEBUG5, "pg_loadavg: %s", buffer);

	p = buffer;

	/* load1 */
	GET_NEXT_VALUE(p, q, values[i_load1], length, "load1 not found", ' ');

	/* load5 */
	GET_NEXT_VALUE(p, q, values[i_load5], length, "load5 not found", ' ');

	/* load15 */
	GET_NEXT_VALUE(p, q, values[i_load15], length, "load15 not found", ' ');

	SKIP_TOKEN(p);			/* skip running/tasks */

	/* last_pid */
	/*
	 * It appears sometimes this is the last item in /proc/PID/stat and
	 * sometimes it's not, depending on the version of the kernel and
	 * possibly the architecture.  So first test if it is the last item
	 * before determining how to deliminate it.
	 */
	if (strchr(p, ' ') == NULL)
	{
		GET_NEXT_VALUE(p, q, values[i_last_pid], length,
				"last_pid not found", '\n');
	}
	else
	{
		GET_NEXT_VALUE(p, q, values[i_last_pid], length,
				"last_pid not found", ' ');
	}

	elog(DEBUG5, "pg_loadavg: [%d] load1 = %s", (int) i_load1,
			values[i_load1]);
	elog(DEBUG5, "pg_loadavg: [%d] load5 = %s", (int) i_load5,
			values[i_load5]);
	elog(DEBUG5, "pg_loadavg: [%d] load15 = %s", (int) i_load15,
			values[i_load15]);
	elog(DEBUG5, "pg_loadavg: [%d] last_pid = %s", (int) i_last_pid,
			values[i_last_pid]);

	return 1;
}

Datum pg_memusage(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;


	elog(DEBUG5, "pg_memusage: Entering stored function.");

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Build a tuple descriptor for our result type */
		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("function returning record called in context "
							"that cannot accept type record")));

		/*
		 * generate attribute metadata needed later to produce tuples from raw
		 * C strings
		 */
		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		funcctx->max_calls = 1;

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	call_cntr = funcctx->call_cntr;
	max_calls = funcctx->max_calls;
	attinmeta = funcctx->attinmeta;

	if (call_cntr < max_calls) /* do when there is more left to send */
	{
		HeapTuple tuple;
		Datum result;

		char **values = NULL;

		values = (char **) palloc(8 * sizeof(char *));
		values[i_memused] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_memfree] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_memshared] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_membuffers] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_memcached] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_swapused] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_swapfree] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_swapcached] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));

		if (get_memusage(values) == 0)
			SRF_RETURN_DONE(funcctx);

		/* build a tuple */
		tuple = BuildTupleFromCStrings(attinmeta, values);

		/* make the tuple into a datum */
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}
	else /* do when there is no more left */
	{
		SRF_RETURN_DONE(funcctx);
	}
}

int
get_memusage(char **values)
{
	int length;
	unsigned long memfree = 0;
	unsigned long memtotal = 0;
	unsigned long swapfree = 0;
	unsigned long swaptotal = 0;

	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		return 0;
	}

	snprintf(buffer, sizeof(buffer) - 1, "%s/meminfo", PROCFS);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		return 0;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	buffer[len] = '\0';
	elog(DEBUG5, "pg_memusage: %s", buffer);

	values[i_memshared][0] = '0';
	values[i_memshared][1] = '\0';

	p = buffer;
	while (*p != '\0') {
		if (strncmp(p, "Buffers:", 8) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_membuffers], length,
					"Buffers not found", ' ');
		}
		else if (strncmp(p, "Cached:", 7) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_memcached], length,
					"Cached not found", ' ');
		}
		else if (strncmp(p, "MemFree:", 8) == 0)
		{
			SKIP_TOKEN(p);
			memfree = strtoul(p, &p, 10);
			snprintf(values[i_memused], BIGINT_LEN, "%lu", memtotal - memfree);
			snprintf(values[i_memfree], BIGINT_LEN, "%lu", memfree);
		}
		else if (strncmp(p, "MemShared:", 10) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_memshared], length,
					"MemShared not found", ' ');
		}
		else if (strncmp(p, "MemTotal:", 9) == 0)
		{
			SKIP_TOKEN(p);
			memtotal = strtoul(p, &p, 10);
			elog(DEBUG5, "pg_memusage: MemTotal = %lu", memtotal);
		}
		else if (strncmp(p, "SwapFree:", 9) == 0)
		{
			SKIP_TOKEN(p);
			swapfree = strtoul(p, &p, 10);
			snprintf(values[i_swapused], BIGINT_LEN, "%lu",
					swaptotal - swapfree);
			snprintf(values[i_swapfree], BIGINT_LEN, "%lu", swapfree);
		}
		else if (strncmp(p, "SwapCached:", 11) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_swapcached], length,
					"SwapCached not found", ' ');
		}
		else if (strncmp(p, "SwapTotal:", 10) == 0)
		{
			SKIP_TOKEN(p);
			swaptotal = strtoul(p, &p, 10);
			elog(DEBUG5, "pg_memusage: SwapTotal = %lu", swaptotal);
		}
		p = strchr(p, '\n');
		++p;
	}

	elog(DEBUG5, "pg_memusage: [%d] Buffers = %s", (int) i_membuffers,
			values[i_membuffers]);
	elog(DEBUG5, "pg_memusage: [%d] Cached = %s", (int) i_memcached,
			values[i_memcached]);
	elog(DEBUG5, "pg_memusage: [%d] MemFree = %s", (int) i_memfree,
			values[i_memfree]);
	elog(DEBUG5, "pg_memusage: [%d] MemUsed = %s", (int) i_memused,
			values[i_memused]);
	elog(DEBUG5, "pg_memusage: [%d] MemShared = %s", (int) i_memshared,
			values[i_memshared]);
	elog(DEBUG5, "pg_memusage: [%d] SwapCached = %s", (int) i_swapcached,
			values[i_swapcached]);
	elog(DEBUG5, "pg_memusage: [%d] SwapFree = %s", (int) i_swapfree,
			values[i_swapfree]);
	elog(DEBUG5, "pg_memusage: [%d] SwapUsed = %s", (int) i_swapused,
			values[i_swapused]);

	return 1;
}

Datum pg_diskusage(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	TupleDesc tupleDesc;
	Tuplestorestate *tupleStore;

	Datum values[20];
	bool nulls[20];

	char device_name[4096];
	struct statfs sb;
	FILE *fd;
	int ret;

	int major = 0;
	int minor = 0;

	int64 reads_completed = 0;
	int64 reads_merged = 0;
	int64 sectors_read = 0;
	int64 readtime = 0;

	int64 writes_completed = 0;
	int64 writes_merged = 0;
	int64 sectors_written = 0;
	int64 writetime = 0;

	int64 current_io = 0;
	int64 iotime = 0;
	int64 totaliotime = 0;

	int64 discards_completed = 0;
	int64 discards_merged = 0;
	int64 sectors_discarded = 0;
	int64 discardtime = 0;

	int64 flushes_completed = 0;
	int64 flushtime = 0;

	elog(DEBUG5, "pg_diskusage: Entering stored function.");

	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg
				 ("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not "
						"allowed in this context")));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/*
	 * Build a tuple descriptor for our result type
	 */
	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	tupleStore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupleStore;
	rsinfo->setDesc = tupleDesc;

	MemoryContextSwitchTo(oldcontext);

	memset(nulls, 0, sizeof(nulls));
	memset(values, 0, sizeof(values));

	if (statfs("/proc", &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on /proc\n");
		return (Datum) 0;
	}
	if ((fd = AllocateFile("/proc/diskstats", PG_BINARY_R)) == NULL)
	{
		elog(ERROR, "File not found: '/proc/diskstats'");
		return (Datum) 0;
	}

#define HS "%*[ \t]"

	while ((ret =
			fscanf(fd, (
					   "%d" HS "%d" HS "%s"
					   HS "%lu" HS "%lu" HS "%lu" HS "%lu"
					   HS "%lu" HS "%lu" HS "%lu" HS "%lu"
					   HS "%lu" HS "%lu" HS "%lu"
					   HS "%lu" HS "%lu" HS "%lu" HS "%lu"
					   HS "%lu" HS "%lu"
					   ),
				   &major, &minor, device_name,
				   &reads_completed, &reads_merged, &sectors_read, &readtime,
				   &writes_completed, &writes_merged, &sectors_written, &writetime,
				   &current_io, &iotime, &totaliotime,
				   &discards_completed, &discards_merged, &sectors_discarded, &discardtime,
				   &flushes_completed, &flushtime
				)) > 0)
	{
		/*
		 * Consume additional data on the line, so it isn't
		 * interpreted as part of the next (newline-delimited)
		 * diskstats record.
		 */
		ret = fscanf(fd, "%*[^\n]");

		values[i_major] = Int32GetDatum(major);
		values[i_minor] = Int32GetDatum(minor);
		values[i_devname] = CStringGetTextDatum(device_name);

		values[i_reads_completed] = Int64GetDatumFast(reads_completed);
		values[i_reads_merged] = Int64GetDatumFast(reads_merged);
		values[i_sectors_read] = Int64GetDatumFast(sectors_read);
		values[i_readtime] = Int64GetDatumFast(readtime);

		values[i_writes_completed] = Int64GetDatumFast(writes_completed);
		values[i_writes_merged] = Int64GetDatumFast(writes_merged);
		values[i_sectors_written] = Int64GetDatumFast(sectors_written);
		values[i_writetime] = Int64GetDatumFast(writetime);

		values[i_current_io] = Int64GetDatumFast(current_io);
		values[i_iotime] = Int64GetDatumFast(iotime);
		values[i_totaliotime] = Int64GetDatumFast(totaliotime);

		values[i_discards_completed] = discards_completed;
		values[i_discards_merged] = discards_merged;
		values[i_sectors_discarded] = sectors_discarded;
		values[i_discardtime] = discardtime;

		values[i_flushes_completed] = flushes_completed;
		values[i_flushtime] = flushtime;

		tuplestore_putvalues(tupleStore, tupleDesc, values, nulls);
	}
	FreeFile(fd);

	tuplestore_donestoring(tupleStore);

	return (Datum) 0;
}
