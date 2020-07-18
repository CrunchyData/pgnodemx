# pgnodemx

## Overview
SQL functions that allow capture of node OS metrics from PostgreSQL

## Security
Executing role must have been granted pg_read_server_files membership.

## cgroup Related Functions

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
