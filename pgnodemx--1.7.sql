/* pgnodemx--1.7.sql */

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

CREATE FUNCTION proc_pid_io(
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

CREATE FUNCTION proc_pid_cmdline(
  OUT pid INTEGER,
  OUT fullcomm TEXT,
  OUT uid INTEGER,
  OUT username TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_pid_cmdline'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION proc_pid_stat(
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

CREATE FUNCTION kpages_to_bytes(NUMERIC)
RETURNS NUMERIC
AS 'MODULE_PATHNAME', 'pgnodemx_pages_to_bytes'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION proc_cputime(
  OUT "user" BIGINT,
  OUT nice BIGINT,
  OUT system BIGINT,
  OUT idle BIGINT,
  OUT iowait BIGINT)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_cputime'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION proc_loadavg(
  OUT load1 FLOAT,
  OUT load5 FLOAT,
  OUT load15 FLOAT,
  OUT last_pid INTEGER)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_loadavg'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION exec_path()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_exec_path'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION stat_file(
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

CREATE FUNCTION openssl_version()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_openssl_version'
LANGUAGE C IMMUTABLE STRICT;
