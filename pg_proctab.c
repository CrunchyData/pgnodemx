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
void get_uid_username( char *pid, char **uid, char **username );

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

Oid cpu_time_sig[] = { INT8OID, INT8OID, INT8OID, INT8OID, INT8OID };
Oid load_avg_sig[] = { FLOAT8OID, FLOAT8OID, FLOAT8OID, INT4OID };

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
			get_uid_username( child_pids[j], &values[j][l], &values[j][l+1] );
			l = l+2;

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

void
get_uid_username( char *pid, char **uid, char **username )
{
	struct stat stat_struct;
	char tmp[256];
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

		snprintf(tmp, INTEGER_LEN, "%d", stat_struct.st_uid);
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

Datum pg_cputime(PG_FUNCTION_ARGS)
{
	char **values = NULL;
	struct statfs sb;
	char buffer[4096];
	char **lines;
	int nlines;
	char **tokens;
	int ntok;

	elog(DEBUG5, "pg_cputime: Entering stored function.");

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		/* never reached */
		return (Datum)0;
	}

	snprintf(buffer, sizeof(buffer) - 1, "%s/stat", PROCFS);
	lines = read_nlsv(buffer, &nlines);

	elog(DEBUG5, "pg_cputime: %s", lines[0]);

	tokens = parse_ss_line(lines[0], &ntok);

	values = (char **) palloc(5 * sizeof(char *));

	values[i_user] = pstrdup(tokens[1]);
	values[i_nice_c] = pstrdup(tokens[2]);
	values[i_system] = pstrdup(tokens[3]); 
	values[i_idle] = pstrdup(tokens[4]);
	values[i_iowait] = pstrdup(tokens[5]);

	elog(DEBUG5, "pg_cputime: [%d] user = %s", (int) i_user, values[i_user]);
	elog(DEBUG5, "pg_cputime: [%d] nice = %s", (int) i_nice_c, values[i_nice_c]);
	elog(DEBUG5, "pg_cputime: [%d] system = %s", (int) i_system,
			values[i_system]);
	elog(DEBUG5, "pg_cputime: [%d] idle = %s", (int) i_idle, values[i_idle]);
	elog(DEBUG5, "pg_cputime: [%d] iowait = %s", (int) i_iowait,
			values[i_iowait]);

	return form_srf(fcinfo, &values, 1, 5, cpu_time_sig);

}

Datum pg_loadavg(PG_FUNCTION_ARGS)
{
	char **values = NULL;
	struct statfs sb;
	char buffer[4096];
	char **lines;
	int nlines;
	char **tokens;
	int ntok;

	elog(DEBUG5, "pg_loadavg: Entering stored function.");

	values = (char **) palloc(4 * sizeof(char *));
	
	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		/* never reached */
		return (Datum)0;
	}

	snprintf(buffer, sizeof(buffer) - 1, "%s/loadavg", PROCFS);
	lines = read_nlsv(buffer, &nlines);

	elog(DEBUG5, "pg_loadavg: %s", buffer);

	tokens = parse_ss_line(lines[0], &ntok);

	values = (char **) palloc(4 * sizeof(char *));

	values[i_load1] = pstrdup(tokens[0]);
	values[i_load5] = pstrdup(tokens[1]);
	values[i_load15] = pstrdup(tokens[2]); 
	/* skip running/tasks */
	values[i_last_pid] = pstrdup(tokens[4]);

	elog(DEBUG5, "pg_loadavg: [%d] load1 = %s", (int) i_load1,
			values[i_load1]);
	elog(DEBUG5, "pg_loadavg: [%d] load5 = %s", (int) i_load5,
			values[i_load5]);
	elog(DEBUG5, "pg_loadavg: [%d] load15 = %s", (int) i_load15,
			values[i_load15]);
	elog(DEBUG5, "pg_loadavg: [%d] last_pid = %s", (int) i_last_pid,
			values[i_last_pid]);

	return form_srf(fcinfo, &values, 1, 4, load_avg_sig);

}

