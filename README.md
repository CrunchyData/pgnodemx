# pgnodemx

## Overview
SQL functions that allow capture of node OS metrics from PostgreSQL

## Security
Executing role must have been granted pg_read_server_files membership.

## cgroup Related Functions

cgroup virtual files fall into (at least) the following general categories, each with a generic SQL access function:

* BIGINT single line scalar values - ```cgroup_scalar_bigint(filename text)```
** cgroup v2 examples: cgroup.freeze, cgroup.max.depth, cgroup.max.descendants, cpu.weight, cpu.weight.nice, memory.current, memory.high, memory.low, memory.max, memory.min, memory.oom.group, memory.swap.current, memory.swap.max, pids.current, pids.max
* FLOAT8 single line scalar values - ```cgroup_scalar_float8(filename text)```
** cgroup v2 examples: cpu.uclamp.max, cpu.uclamp.min
* TEXT single line scalar values - ```cgroup_scalar_text(filename text)```
** cgroup v2 examples: cgroup.type

* SETOF(BIGINT) space separated values or multiline scalar values - ```cgroup_setof_bigint(filename text)```
** cgroup v2 examples: cgroup.procs, cgroup.threads, cpu.max
* SETOF(TEXT) space separated values or multiline scalar values - ```cgroup_setof_text(filename text)```
** cgroup v2 examples: cgroup.controllers, cgroup.subtree_control

* SETOF(TEXT, BIGINT) flat keyed - ```cgroup_setof_kv(filename text)```
** cgroup v2 examples: cgroup.events, cgroup.stat, cpu.stat, io.pressure, io.weight, memory.events, memory.events.local, memory.stat, memory.swap.events, pids.events

In each case, the filename must be in the form ```<controller>.<metric>```, e.g. ```memory.stat```





Additionally, the following categories of cgroup virtual file formats exist. These are each served with a specific SQL access function:

FIXME: convert to key-subkey-value triplets and generic access function

* SETOF(TEXT, BIGINT, BIGINT, BIGINT, BIGINT) nested keyed
** cpu.pressure - cgroup_cpu_pressure()
** io.max - cgroup_io_pressure()
** memory.pressure - cgroup_memory_pressure()
* SETOF(TEXT, BIGINT, BIGINT, BIGINT, BIGINT, BIGINT, BIGINT) nested keyed
** io.stat - cgroup_io_stat()




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
* Based on the memory controller for cgroup v1













### Get Scalar BIGINT Memory Metrics by Filename
```
SELECT cgroup_memstat(filename TEXT);
```
* Returns the BIGINT value obtained by reading the provided file.
* ```filename``` is the name of a cgroup memory controller virtual file.
* This function only works for virtual files which contain a single row, single column, BIGINT data type value. The only exception to that is the special value "max", which will be translated into the maxumum BIGINT value.
* Examples: memory.current, memory.high, memory.max, memory.swap.current, memory.limit_in_bytes, memory.usage_in_bytes;

### Get Flat Keyed Memory BIGINT Metrics by Filename
```
SELECT * FROM cgroup_keyed_memstat(filename TEXT);
```
* Returns the key names and BIGINT values obtained by reading the provided file.
* ```filename``` is the name of a cgroup memory controller virtual file.
* This function only works for virtual files which contain multiple rows of two columns separated by a space. The first column must be the value name (key) and the second column is a BIGINT data type value. The only exception to that is the special value "max", which will be translated into the maxumum BIGINT value.
* Examples: memory.stat, memory.events, memory.events.local

### Get Memory Pressure output
```
SELECT key, avg10, avg60, avg300, total FROM memory_pressure();
```
* Returns parsed output from memory.pressure
* Only supported in unified/v2 mode
* See https://www.kernel.org/doc/html/latest/accounting/psi.html#psi for details
