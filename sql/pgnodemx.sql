/* beginnings of a regression test */

CREATE EXTENSION pgnodemx;
SELECT cgroup_mode();
SELECT * FROM cgroup_path();
SELECT cgroup_process_count();
SELECT current_setting('pgnodemx.containerized');

SELECT cgroup_scalar_bigint('memory.current');
SELECT cgroup_scalar_float8('memory.current');
SELECT cgroup_scalar_text('memory.current');
SELECT cgroup_scalar_text('cgroup.type');
SELECT cgroup_scalar_bigint('memory.high');
SELECT cgroup_scalar_bigint('memory.max');
SELECT cgroup_scalar_bigint('memory.swap.current');
-- should return NULL
SELECT cgroup_scalar_bigint(null);
-- should fail
SELECT cgroup_scalar_bigint('bar/../../etc/memory.max');
-- should fail
SELECT cgroup_scalar_bigint('/memory.max');
CREATE USER pgnodemx_joe;
SET SESSION AUTHORIZATION pgnodemx_joe;
-- should fail
SELECT cgroup_scalar_bigint('memory.current');
RESET SESSION AUTHORIZATION;
DROP USER pgnodemx_joe;

SELECT cgroup_setof_bigint('cgroup.procs');

SELECT cgroup_array_text('cpu.max');
SELECT cgroup_array_bigint('cpu.max');
SELECT cgroup_array_text('cgroup.controllers');

SELECT * FROM cgroup_setof_kv('memory.stat');
SELECT * FROM cgroup_setof_kv('cgroup.events');
SELECT * FROM cgroup_setof_kv('cgroup.stat');
SELECT * FROM cgroup_setof_kv('cpu.stat');
SELECT * FROM cgroup_setof_kv('io.weight');
SELECT * FROM cgroup_setof_kv('memory.events');
SELECT * FROM cgroup_setof_kv('memory.events.local');
SELECT * FROM cgroup_setof_kv('memory.swap.events');
SELECT * FROM cgroup_setof_kv('pids.events');

SELECT * FROM cgroup_setof_nkv('memory.pressure');
SELECT * FROM cgroup_setof_nkv('io.stat');
SELECT * FROM cgroup_setof_nkv('io.pressure');
SELECT * FROM cgroup_setof_nkv('cpu.pressure');

SELECT envvar_text('PGDATA');
SELECT envvar_bigint('PGPORT');

SELECT * FROM proc_meminfo();
