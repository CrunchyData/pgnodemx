/* beginnings of a regression test */

CREATE EXTENSION pgnodemx;
SELECT current_setting('pgnodemx.containerized');
SELECT key, avg10, avg60, avg300, total FROM memory_pressure();
SELECT * FROM cgroup_path();
SELECT * FROM memory_pressure();
SELECT cgroup_memstat('memory.current');
SELECT cgroup_memstat('memory.high');
SELECT cgroup_memstat('memory.max');
SELECT cgroup_memstat('memory.swap.current');
SELECT cgroup_memstat(null);
SELECT cgroup_process_count();
SELECT current_setting('pgnodemx.containerized');
SELECT * FROM cgroup_keyed_memstat('memory.stat');
