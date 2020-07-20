/* beginnings of a regression test */

CREATE EXTENSION pgnodemx;
SELECT cgroup_mode();
SELECT * FROM cgroup_path();
SELECT cgroup_process_count();
SELECT current_setting('pgnodemx.containerized');

SELECT cgroup_memstat('memory.current');
SELECT cgroup_memstat('memory.high');
SELECT cgroup_memstat('memory.max');
SELECT cgroup_memstat('memory.swap.current');
-- should return NULL
SELECT cgroup_memstat(null);
-- should fail
SELECT cgroup_memstat('bar/../../etc/memory.max');
-- should fail
SELECT cgroup_memstat('/memory.max');
CREATE USER pgnodemx_joe;
SET SESSION AUTHORIZATION pgnodemx_joe;
-- should fail
SELECT cgroup_memstat('memory.current');
RESET SESSION AUTHORIZATION;
DROP USER pgnodemx_joe;

SELECT * FROM cgroup_keyed_memstat('memory.stat');

SELECT key, avg10, avg60, avg300, total FROM memory_pressure();
