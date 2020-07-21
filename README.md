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

In each case, the filename must be in the form ```<controller>.<metric>```, e.g. ```memory.stat```. For more information about cgroup v2 virtual files, See https://www.kernel.org/doc/Documentation/cgroup-v2.txt.

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

## Configuration

* Add pgnodemx to shared_preload_libraries in postgresql.conf.
```
shared_preload_libraries = 'pgnodemx'
```
* The following custom parameters may be set. The values shown are defaults. If the default values work, there is no need to add these to ```postgresql.conf```. In particular, if ```pgnodemx.containerized``` is defined in ```postgresql.conf```, that value will override pgnodemx heuristics. When not specified, pgnodemx heuristics will determine if the value should be ```on``` or ```off``` at runtime.
```
# force use of "containerized" assumptions for cgroup file paths
pgnodemx.containerized = off
# specify location of cgroup mount
pgnodemx.cgrouproot = '/sys/fs/cgroup'
```

## Installation

### Compile and Install

Clone PostgreSQL repository:

```bash
$> git clone https://github.com/postgres/postgres.git
```

Checkout REL_12_STABLE (for example) branch:

```bash
$> git checkout REL_12_STABLE
```

Make PostgreSQL:

```bash
$> ./configure
$> make install -s
```

Change to the contrib directory:

```bash
$> cd contrib
```

Clone ```pgnodemx``` extension:

```bash
$> git clone https://github.com/crunchydata/pgnodemx
```

Change to ```pgnodemx``` directory:

```bash
$> cd pgnodemx
```

Build ```pgnodemx```:

```bash
$> make
```

Install ```pgnodemx```:

```bash
$> make install
```

#### Using PGXS

If an instance of PostgreSQL is already installed, then PGXS can be utilized to build and install ```pgnodemx```.  Ensure that PostgreSQL binaries are available via the ```$PATH``` environment variable then use the following commands.

```bash
$> make USE_PGXS=1
$> make USE_PGXS=1 install
```

### Configure

The following bash commands should configure your system to utilize pgnodemx. Replace all paths as appropriate. It may be prudent to visually inspect the files afterward to ensure the changes took place.

###### Initialize PostgreSQL (if needed):

```bash
$> initdb -D /path/to/data/directory
```

###### Create Target Database (if needed):

```bash
$> createdb <database>
```

###### Install ```pgnodemx``` functions:

Edit postgresql.conf and add ```pgnodemx``` to the shared_preload_libraries line, and change custom settings as mentioned above.

Finally, restart PostgreSQL (method may vary):

```
$> service postgresql restart
```
Install the extension into your database:

```bash
psql <database>
CREATE EXTENSION pgnodemx;
```
