/* contrib/pgnodemx/pgnodemx--1.2.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgnodemx" to load this file. \quit

CREATE FUNCTION cgroup_mode()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_mode'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_path
(
  OUT controller TEXT,
  OUT path TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_path'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_process_count()
RETURNS INT4
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_process_count'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_scalar_bigint(TEXT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_scalar_bigint'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_scalar_float8(TEXT)
RETURNS FLOAT8
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_scalar_float8'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_scalar_text(TEXT)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_scalar_text'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_setof_bigint(TEXT)
RETURNS SETOF BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_bigint'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_setof_text(TEXT)
RETURNS SETOF TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_text'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_array_text(TEXT)
RETURNS TEXT[]
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_array_text'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_array_bigint(TEXT)
RETURNS BIGINT[]
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_array_bigint'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_setof_kv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT val BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_kv'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_setof_ksv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT subkey TEXT,
  OUT val BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_ksv'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION cgroup_setof_nkv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT subkey TEXT,
  OUT val FLOAT8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_nkv'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION envvar_text(TEXT)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_envvar_text'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION envvar_bigint(TEXT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_envvar_bigint'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION proc_diskstats
(
  OUT major_number BIGINT,
  OUT minor_number BIGINT,
  OUT device_name TEXT,
  OUT reads_completed_successfully BIGINT,
  OUT reads_merged BIGINT,
  OUT sectors_read BIGINT,
  OUT time_spent_reading_ms BIGINT,
  OUT writes_completed BIGINT,
  OUT writes_merged BIGINT,
  OUT sectors_written BIGINT,
  OUT time_spent_writing_ms BIGINT,
  OUT ios_currently_in_progress BIGINT,
  OUT time_spent_doing_ios_ms BIGINT,
  OUT weighted_time_spent_doing_ios_ms BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_diskstats'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION proc_mountinfo
(
  OUT mount_id BIGINT,
  OUT parent_id BIGINT,
  OUT major_number BIGINT,
  OUT minor_number BIGINT,
  OUT root TEXT,
  OUT mount_point TEXT,
  OUT mount_options TEXT,
  OUT fs_type TEXT,
  OUT mount_source TEXT,
  OUT super_options TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_mountinfo'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION proc_meminfo
(
  OUT key TEXT,
  OUT val BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_meminfo'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION proc_network_stats
(
  OUT interface TEXT,
  OUT rx_bytes BIGINT,
  OUT rx_packets BIGINT,
  OUT rx_errs BIGINT,
  OUT rx_drop BIGINT,
  OUT rx_fifo BIGINT,
  OUT rx_frame BIGINT,
  OUT rx_compressed BIGINT,
  OUT rx_multicast BIGINT,
  OUT tx_bytes BIGINT,
  OUT tx_packets BIGINT,
  OUT tx_errs BIGINT,
  OUT tx_drop BIGINT,
  OUT tx_fifo BIGINT,
  OUT tx_frame BIGINT,
  OUT tx_compressed BIGINT,
  OUT tx_multicast BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_network_stats'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION fsinfo
(
  IN pathname TEXT,
  OUT major_number NUMERIC,
  OUT minor_number NUMERIC,
  OUT type TEXT,
  OUT block_size NUMERIC,
  OUT blocks NUMERIC,
  OUT total_bytes NUMERIC,
  OUT free_blocks NUMERIC,
  OUT free_bytes NUMERIC,
  OUT available_blocks NUMERIC,
  OUT available_bytes NUMERIC,
  OUT total_file_nodes NUMERIC,
  OUT free_file_nodes NUMERIC,
  OUT mount_flags TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_fsinfo'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION kdapi_scalar_bigint(TEXT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_kdapi_scalar_bigint'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION kdapi_setof_kv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT val TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_kdapi_setof_kv'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION fips_mode()
RETURNS BOOL
AS 'MODULE_PATHNAME', 'pgnodemx_fips_mode'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION symbol_filename(TEXT)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_symbol_filename'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION pgnodemx_version()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_version'
LANGUAGE C STABLE STRICT;

CREATE OR REPLACE FUNCTION pg_proctab(
		OUT pid INTEGER,
		OUT comm TEXT,
		OUT fullcomm TEXT,
		OUT state TEXT,
		OUT ppid INTEGER,
		OUT pgrp INTEGER,
		OUT session INTEGER,
		OUT tty_nr INTEGER,
		OUT tpgid INTEGER,
		OUT flags INTEGER,
		OUT minflt BIGINT,
		OUT cminflt BIGINT,
		OUT majflt BIGINT,
		OUT cmajflt BIGINT,
		OUT utime BIGINT,
		OUT stime BIGINT,
		OUT cutime BIGINT,
		OUT cstime BIGINT,
		OUT priority BIGINT,
		OUT nice BIGINT,
		OUT num_threads BIGINT,
		OUT itrealvalue BIGINT,
		OUT starttime NUMERIC,
		OUT vsize BIGINT,
		OUT rss NUMERIC,
		OUT exit_signal INTEGER,
		OUT processor INTEGER,
		OUT rt_priority BIGINT,
		OUT policy BIGINT,
		OUT delayacct_blkio_ticks NUMERIC,
		OUT uid INTEGER,
		OUT username TEXT,
		OUT rchar BIGINT,
		OUT wchar BIGINT,
		OUT syscr BIGINT,
		OUT syscw BIGINT,
		OUT reads BIGINT,
		OUT writes BIGINT,
		OUT cwrites BIGINT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_proctab'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pg_cputime(
		OUT "user" BIGINT,
		OUT nice BIGINT,
		OUT system BIGINT,
		OUT idle BIGINT,
		OUT iowait BIGINT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_cputime'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pg_loadavg(
		OUT load1 FLOAT,
		OUT load5 FLOAT,
		OUT load15 FLOAT,
		OUT last_pid INTEGER)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_loadavg'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pg_memusage(
		OUT memused BIGINT,
		OUT memfree BIGINT,
		OUT memshared BIGINT,
		OUT membuffers BIGINT,
		OUT memcached BIGINT,
		OUT swapused BIGINT,
		OUT swapfree BIGINT,
		OUT swapcached BIGINT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_memusage'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION pg_diskusage (
        OUT major smallint,
        OUT minor smallint,
        OUT devname text,
        OUT reads_completed bigint,
        OUT reads_merged bigint,
        OUT sectors_read bigint,
        OUT readtime bigint,
        OUT writes_completed bigint,
        OUT writes_merged bigint,
        OUT sectors_written bigint,
        OUT writetime bigint,
        OUT current_io bigint,
        OUT iotime bigint,
        OUT totaliotime bigint)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_diskusage'
LANGUAGE C IMMUTABLE STRICT;