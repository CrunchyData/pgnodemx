# pgnodemx

## Overview
SQL functions that allow capture of node OS metrics from PostgreSQL

## Security
Executing role must have been granted pg_monitor membership.

## cgroup Related Functions

### General Access Functions

cgroup virtual files fall into (at least) the following general categories, each with a generic SQL access function:

* BIGINT single line scalar values - ```cgroup_scalar_bigint(filename text)```
  * cgroup v2 examples: cgroup.freeze, cgroup.max.depth, cgroup.max.descendants, cpu.weight, cpu.weight.nice, memory.current, memory.high, memory.low, memory.max, memory.min, memory.oom.group, memory.swap.current, memory.swap.max, pids.current, pids.max
* FLOAT8 single line scalar values - ```cgroup_scalar_float8(filename text)```
  * cgroup v2 examples: cpu.uclamp.max, cpu.uclamp.min
* TEXT single line scalar values - ```cgroup_scalar_text(filename text)```
  * cgroup v2 examples: cgroup.type

* SETOF(BIGINT) space separated values or multiline scalar values - ```cgroup_setof_bigint(filename text)```
  * cgroup v2 examples: cgroup.procs, cgroup.threads, cpu.max
* SETOF(TEXT) space separated values or multiline scalar values - ```cgroup_setof_text(filename text)```
  * cgroup v2 examples: cgroup.controllers, cgroup.subtree_control

* SETOF(TEXT, BIGINT) flat keyed - ```cgroup_setof_kv(filename text)```
  * cgroup v2 examples: cgroup.events, cgroup.stat, cpu.stat, io.pressure, io.weight, memory.events, memory.events.local, memory.stat, memory.swap.events, pids.events

* SETOF(TEXT, TEXT, FLOAT8) nested keyed - ```cgroup_setof_nkv(filename text)```
  * cgroup v2 examples: memory.pressure, cpu.pressure, io.max, io.stat

In each case, the filename must be in the form ```<controller>.<metric>```, e.g. ```memory.stat```

### Get current cgroup mode
```
SELECT cgroup_mode();
```
* Returns the current cgroup mode. Possible values are "legacy", "unified", and "hybrid". These correspond to cgroup v1, cgroup v2, and mixed, respectively.

### Determine if Running Containerized
```
SELECT current_setting('pgnodemx.containerized');
```
* Returns boolean result ("on"/"off"). The extension attempts to heuristically determine whether PostgreSQL is running under a container, but this value may be explicitly set in postgresql.conf to override the heuristically determined value. The value of this setting influences the cgroup paths which are used to read the cgroup controller files.

### Get cgroup Paths
```
SELECT controller, path FROM cgroup_path();
```
* Returns the path to each supported cgroup controller.

### Get cgroup process count
```
SELECT cgroup_process_count();
```
* Returns the number of processes assigned to the cgroup
* For cgroup v1, based on the "memory" controller cgroup.procs file. For cgroup v2, based on the unified cgroup.procs file.
 
