/*
 * Functions to provide a pg_proctab compatible interface.
 * The hope is that this will allow pgnodemx to work with
 * pg_top as a remote target.
 */

CREATE OR REPLACE FUNCTION pg_cputime(
 OUT "user" BIGINT,
 OUT nice BIGINT,
 OUT system BIGINT,
 OUT idle BIGINT,
 OUT iowait BIGINT
)
RETURNS SETOF record
AS $$
 SELECT "user", nice, system, idle, iowait
 FROM proc_cputime()
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION pg_loadavg(
 OUT load1 FLOAT,
 OUT load5 FLOAT,
 OUT load15 FLOAT,
 OUT last_pid INTEGER
)
RETURNS SETOF record
AS $$
 SELECT load1, load5, load15, last_pid
 FROM proc_loadavg()
$$ LANGUAGE sql;

/*
 * Compatibility note: in the original implementation memshared
 * is always equal to zero. Here we use the value from Shmem instead.
 */
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
AS $$
 WITH m (key,val) AS
 (
   SELECT key, val
   FROM proc_meminfo()
 )
 SELECT
  ((SELECT val FROM m WHERE key = 'MemTotal') - (SELECT val FROM m WHERE key = 'MemFree')) / 1024 as memused,
  (SELECT val FROM m WHERE key = 'MemFree') / 1024 AS memfree,
  (SELECT val FROM m WHERE key = 'Shmem') / 1024 AS memshared,
  (SELECT val FROM m WHERE key = 'Buffers') / 1024 AS membuffers,
  (SELECT val FROM m WHERE key = 'Cached') / 1024 AS memcached,
  ((SELECT val FROM m WHERE key = 'SwapTotal') - (SELECT val FROM m WHERE key = 'SwapFree')) / 1024 AS swapused,
  (SELECT val FROM m WHERE key = 'SwapFree') / 1024 AS swapfree,
  (SELECT val FROM m WHERE key = 'SwapCached') / 1024 as swapcached
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION pg_proctab(
 OUT pid integer,
 OUT comm character varying,
 OUT fullcomm character varying,
 OUT state character,
 OUT ppid integer,
 OUT pgrp integer,
 OUT session integer,
 OUT tty_nr integer,
 OUT tpgid integer,
 OUT flags integer,
 OUT minflt bigint,
 OUT cminflt bigint,
 OUT majflt bigint,
 OUT cmajflt bigint,
 OUT utime bigint,
 OUT stime bigint,
 OUT cutime bigint,
 OUT cstime bigint,
 OUT priority bigint,
 OUT nice bigint,
 OUT num_threads bigint,
 OUT itrealvalue bigint,
 OUT starttime bigint,
 OUT vsize bigint,
 OUT rss bigint,
 OUT exit_signal integer,
 OUT processor integer,
 OUT rt_priority bigint,
 OUT policy bigint,
 OUT delayacct_blkio_ticks bigint,
 OUT uid integer,
 OUT username character varying,
 OUT rchar bigint,
 OUT wchar bigint,
 OUT syscr bigint,
 OUT syscw bigint,
 OUT reads bigint,
 OUT writes bigint,
 OUT cwrites bigint
)
RETURNS SETOF record
AS $$
 SELECT
  s.pid,
  comm,
  fullcomm,
  state,
  ppid,
  pgrp,
  session,
  tty_nr,
  tpgid,
  flags,
  minflt,
  cminflt,
  majflt,
  cmajflt,
  utime,
  stime,
  cutime,
  cstime,
  priority,
  nice,
  num_threads,
  itrealvalue,
  starttime,
  vsize,
  kpages_to_bytes(rss) / 1024 as rss,
  exit_signal,
  processor,
  rt_priority,
  policy,
  delayacct_blkio_ticks,
  uid,
  username,
  rchar,
  wchar,
  syscr,
  syscw,
  reads,
  writes,
  cwrites
 FROM proc_pid_stat() s
 JOIN proc_pid_cmdline() c
 ON s.pid = c.pid
 JOIN proc_pid_io() i
 ON c.pid = i.pid
$$ LANGUAGE sql;
