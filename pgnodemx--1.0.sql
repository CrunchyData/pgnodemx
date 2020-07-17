/* contrib/pgnodemx/pgnodemx--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgnodemx" to load this file. \quit

CREATE FUNCTION cgroup_mode()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_mode'
LANGUAGE C STABLE PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_path
(
  OUT controller text,
  OUT path text
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_cgroup_path'
LANGUAGE C STABLE PARALLEL RESTRICTED;

CREATE FUNCTION memory_pressure
(
  OUT key text,
  OUT avg10 float8,
  OUT avg60 float8,
  OUT avg300 float8,
  OUT total float8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pgnodemx_memory_pressure'
LANGUAGE C STABLE PARALLEL RESTRICTED;

CREATE FUNCTION cgroup_memstat(text)
RETURNS BIGINT
AS 'MODULE_PATHNAME', 'pgnodemx_memstat_int64'
LANGUAGE C STABLE PARALLEL RESTRICTED;


