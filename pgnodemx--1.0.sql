/* contrib/pgnodemx/pgnodemx--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgnodemx" to load this file. \quit

CREATE FUNCTION cgroup_mode()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_mode'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_path
(
  OUT controller TEXT,
  OUT path TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_path'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_process_count()
RETURNS INT4
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_process_count'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_scalar_bigint(TEXT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_scalar_bigint'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_scalar_float8(TEXT)
RETURNS FLOAT8
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_scalar_float8'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_scalar_text(TEXT)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_scalar_text'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_setof_bigint(TEXT)
RETURNS SETOF BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_bigint'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_setof_text(TEXT)
RETURNS SETOF TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_text'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_array_text(TEXT)
RETURNS TEXT[]
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_array_text'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_array_bigint(TEXT)
RETURNS BIGINT[]
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_array_bigint'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_setof_kv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT val BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_kv'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_setof_ksv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT subkey TEXT,
  OUT val BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_ksv'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_setof_nkv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT subkey TEXT,
  OUT avg10 FLOAT8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_setof_nkv'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION envvar_text(TEXT)
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_envvar_text'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION envvar_bigint(TEXT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_envvar_bigint'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION proc_meminfo
(
  OUT key TEXT,
  OUT val BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_proc_meminfo'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION fsinfo
(
  IN pathname TEXT,
  OUT type TEXT,
  OUT block_size BIGINT,
  OUT blocks BIGINT,
  OUT total_bytes BIGINT,
  OUT free_blocks BIGINT,
  OUT free_bytes BIGINT,
  OUT available_blocks BIGINT,
  OUT available_bytes BIGINT,
  OUT total_file_nodes BIGINT,
  OUT free_file_nodes BIGINT,
  OUT mount_flags TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_fsinfo'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION network_stats
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
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION kdapi_scalar_bigint(TEXT)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_kdapi_scalar_bigint'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION kdapi_setof_kv
(
  IN filename TEXT,
  OUT key TEXT,
  OUT val TEXT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_kdapi_setof_kv'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

