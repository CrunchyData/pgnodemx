/* contrib/pgnodemx/pgnodemx--1.1--1.2.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgnodemx" to load this file. \quit

DROP FUNCTION fsinfo (text);

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
