/* beginnings of a cgroup v1 regression test */

\pset pager off
DROP EXTENSION IF EXISTS pgnodemx;

CREATE EXTENSION pgnodemx;
SELECT cgroup_mode();
SELECT * FROM cgroup_path();
SELECT cgroup_process_count();
SELECT current_setting('pgnodemx.containerized');

SELECT cgroup_scalar_bigint('memory.usage_in_bytes');
SELECT cgroup_scalar_float8('memory.usage_in_bytes');
SELECT cgroup_scalar_text('memory.usage_in_bytes');
SELECT cgroup_scalar_bigint('memory.limit_in_bytes');
SELECT cgroup_scalar_bigint('cpu.cfs_period_us');
SELECT cgroup_scalar_bigint('cpu.cfs_quota_us');

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

SELECT * FROM proc_meminfo();

SELECT * FROM fsinfo(current_setting('data_directory'));
SELECT pg_size_pretty(total_bytes) AS total_size,
       pg_size_pretty(available_bytes) AS available_size
FROM fsinfo(current_setting('data_directory'));

SELECT * FROM network_stats();
SELECT interface,
       rx_bytes,
       rx_packets,
       tx_bytes,
       tx_packets
FROM network_stats();
