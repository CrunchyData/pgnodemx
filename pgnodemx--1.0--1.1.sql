/* contrib/pgnodemx/pgnodemx--1.0--1.1.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgnodemx UPDATE TO '1.1'" to load this file. \quit

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
