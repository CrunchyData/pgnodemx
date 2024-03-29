/* beginnings of a cgroup v1 regression test */

\pset pager off
\x auto
DROP EXTENSION IF EXISTS pgnodemx;

CREATE EXTENSION pgnodemx;
SELECT cgroup_mode();
SELECT * FROM cgroup_path();
SELECT cgroup_process_count();
SELECT current_setting('pgnodemx.containerized');
SELECT current_setting('pgnodemx.cgroup_enabled');

SELECT cgroup_scalar_bigint('memory.usage_in_bytes');
SELECT cgroup_scalar_float8('memory.usage_in_bytes');
SELECT cgroup_scalar_text('memory.usage_in_bytes');
SELECT cgroup_scalar_bigint('memory.limit_in_bytes');
SELECT cgroup_scalar_bigint('cpu.cfs_period_us');
SELECT cgroup_scalar_bigint('cpu.cfs_quota_us');
SELECT cgroup_scalar_bigint('cpuacct.usage');

-- should return NULL
SELECT cgroup_scalar_bigint(null);
-- should fail
SELECT cgroup_scalar_bigint('bar/../../etc/memory.usage_in_bytes');
-- should fail
SELECT cgroup_scalar_bigint('/memory.usage_in_bytes');
CREATE USER pgnodemx_joe;
SET SESSION AUTHORIZATION pgnodemx_joe;
-- should fail
SELECT cgroup_scalar_bigint('memory.usage_in_bytes');
RESET SESSION AUTHORIZATION;
DROP USER pgnodemx_joe;

SELECT cgroup_setof_bigint('cgroup.procs');

SELECT cgroup_array_text('cpu.shares');
SELECT cgroup_array_bigint('cpu.shares');

SELECT * FROM cgroup_setof_kv('cpuacct.stat');
SELECT * FROM cgroup_setof_kv('cpu.stat');
SELECT * FROM cgroup_setof_kv('memory.stat');

SELECT * FROM cgroup_setof_ksv('blkio.throttle.io_serviced');
SELECT * FROM cgroup_setof_ksv('blkio.throttle.io_service_bytes');

SELECT envvar_text('PGDATA');
SELECT envvar_text('HOSTNAME');
SELECT envvar_bigint('PGHA_PG_PORT');

SELECT * FROM proc_diskstats();

SELECT * FROM proc_mountinfo();

SELECT * FROM proc_meminfo();

SELECT * FROM fsinfo(current_setting('data_directory'));
SELECT pg_size_pretty(total_bytes) AS total_size,
       pg_size_pretty(available_bytes) AS available_size
FROM fsinfo(current_setting('data_directory'));

SELECT * FROM proc_network_stats();
SELECT interface,
       rx_bytes,
       rx_packets,
       tx_bytes,
       tx_packets
FROM proc_network_stats();

SELECT current_setting('pgnodemx.kdapi_enabled');
SELECT * FROM kdapi_setof_kv('labels');
SELECT * FROM kdapi_setof_kv('annotations');
SELECT replace(val,'\"','"')::jsonb FROM kdapi_setof_kv('annotations') WHERE key = 'status';

SELECT * FROM kdapi_scalar_bigint('cpu_limit');
SELECT * FROM kdapi_scalar_bigint('cpu_request');
SELECT * FROM kdapi_scalar_bigint('mem_limit');
SELECT * FROM kdapi_scalar_bigint('mem_request');

SELECT *
FROM proc_mountinfo() m
JOIN proc_diskstats() d USING (major_number, minor_number)
JOIN fsinfo(current_setting('data_directory')) f USING (major_number, minor_number);

SELECT "user", nice, system, idle, iowait
FROM proc_cputime();

SELECT load1, load5, load15, last_pid
FROM proc_loadavg();

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
  (SELECT val FROM m WHERE key = 'SwapCached') / 1024 as swapcached;

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
ON c.pid = i.pid;

SELECT exec_path(), * FROM stat_file(exec_path());
