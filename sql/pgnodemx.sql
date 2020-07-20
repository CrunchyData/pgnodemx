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

SELECT cgroup_setof_bigint('cpu.max');
SELECT cgroup_setof_text('cpu.max');
SELECT cgroup_setof_bigint('cgroup.procs');

SELECT * FROM cgroup_keyed_memstat('memory.stat');

SELECT key, avg10, avg60, avg300, total FROM memory_pressure();
