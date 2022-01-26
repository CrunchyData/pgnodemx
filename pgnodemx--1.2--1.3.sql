/* contrib/pgnodemx/pgnodemx--1.1--1.2.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgnodemx" to load this file. \quit

CREATE OR REPLACE FUNCTION proc_pid_io(
		OUT pid INTEGER,
		OUT rchar NUMERIC,
		OUT wchar NUMERIC,
		OUT syscr NUMERIC,
		OUT syscw NUMERIC,
		OUT reads NUMERIC,
		OUT writes NUMERIC,
		OUT cwrites NUMERIC)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_pid_io'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION proc_pid_cmdline(
		OUT pid INTEGER,
		OUT fullcomm TEXT,
		OUT uid INTEGER,
		OUT username TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_pid_cmdline'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION proc_pid_stat(
		OUT pid INTEGER,
		OUT comm TEXT,
		OUT state TEXT,
		OUT ppid INTEGER,
		OUT pgrp INTEGER,
		OUT session INTEGER,
		OUT tty_nr INTEGER,
		OUT tpgid INTEGER,
		OUT flags BIGINT,
		OUT minflt NUMERIC,
		OUT cminflt NUMERIC,
		OUT majflt NUMERIC,
		OUT cmajflt NUMERIC,
		OUT utime NUMERIC,
		OUT stime NUMERIC,
		OUT cutime BIGINT,
		OUT cstime BIGINT,
		OUT priority BIGINT,
		OUT nice BIGINT,
		OUT num_threads BIGINT,
		OUT itrealvalue BIGINT,
		OUT starttime NUMERIC,
		OUT vsize NUMERIC,
		OUT rss BIGINT,
		OUT rsslim NUMERIC,
		OUT startcode NUMERIC,
		OUT endcode NUMERIC,
		OUT startstack NUMERIC,
		OUT kstkesp NUMERIC,
		OUT kstkeip NUMERIC,
		OUT signal NUMERIC,
		OUT blocked NUMERIC,
		OUT sigignore NUMERIC,
		OUT sigcatch NUMERIC,
		OUT wchan NUMERIC,
		OUT nswap NUMERIC,
		OUT cnswap NUMERIC,
		OUT exit_signal INTEGER,
		OUT processor INTEGER,
		OUT rt_priority BIGINT,
		OUT policy BIGINT,
		OUT delayacct_blkio_ticks NUMERIC,
		OUT guest_time NUMERIC,
		OUT cguest_time BIGINT,
		OUT start_data NUMERIC,
		OUT end_data NUMERIC,
		OUT start_brk NUMERIC,
		OUT arg_start NUMERIC,
		OUT arg_end NUMERIC,
		OUT env_start NUMERIC,
		OUT env_end NUMERIC,
		OUT exit_code INTEGER)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_pid_stat'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION kpages_to_bytes(NUMERIC)
RETURNS NUMERIC
AS 'MODULE_PATHNAME', 'pgnodemx_pages_to_bytes'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION proc_cputime(
		OUT "user" BIGINT,
		OUT nice BIGINT,
		OUT system BIGINT,
		OUT idle BIGINT,
		OUT iowait BIGINT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_cputime'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION proc_loadavg(
		OUT load1 FLOAT,
		OUT load5 FLOAT,
		OUT load15 FLOAT,
		OUT last_pid INTEGER)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_loadavg'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION exec_path()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_exec_path'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION stat_file(
		IN filename TEXT,
		OUT uid NUMERIC,
		OUT username TEXT,
		OUT gid NUMERIC,
		OUT groupname TEXT,
		OUT filemode TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_stat_file'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION openssl_version()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_openssl_version'
LANGUAGE C IMMUTABLE STRICT;

DROP FUNCTION proc_diskstats();
CREATE FUNCTION proc_diskstats
(
  OUT major_number BIGINT,
  OUT minor_number BIGINT,
  OUT device_name TEXT,
  OUT reads_completed_successfully NUMERIC,
  OUT reads_merged NUMERIC,
  OUT sectors_read NUMERIC,
  OUT time_spent_reading_ms BIGINT,
  OUT writes_completed NUMERIC,
  OUT writes_merged NUMERIC,
  OUT sectors_written NUMERIC,
  OUT time_spent_writing_ms BIGINT,
  OUT ios_currently_in_progress BIGINT,
  OUT time_spent_doing_ios_ms BIGINT,
  OUT weighted_time_spent_doing_ios_ms BIGINT,
  OUT discards_completed_successfully NUMERIC,
  OUT discards_merged NUMERIC,
  OUT sectors_discarded NUMERIC,
  OUT time_spent_discarding BIGINT,
  OUT flush_requests_completed_successfully NUMERIC,
  OUT time_spent_flushing BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_diskstats'
LANGUAGE C STABLE STRICT;
