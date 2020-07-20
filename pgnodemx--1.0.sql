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

CREATE FUNCTION cgroup_keyed_memstat
(
  IN filename TEXT,
  OUT key TEXT,
  OUT val BIGINT
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_keyed_memstat_int64'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

CREATE FUNCTION memory_pressure
(
  OUT key TEXT,
  OUT avg10 FLOAT8,
  OUT avg60 FLOAT8,
  OUT avg300 FLOAT8,
  OUT total FLOAT8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_memory_pressure'
LANGUAGE C STABLE STRICT PARALLEL RESTRICTED;

