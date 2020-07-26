/*
 * cgroup.h
 *
 * Functions specific to capture and manipulation of cgroup virtual files
 * 
 * Joe Conway <joe@crunchydata.com>
 *
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2020 Crunchy Data Solutions, Inc.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL CRUNCHY DATA SOLUTIONS, INC. BE LIABLE TO ANY PARTY
 * FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
 * INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE CRUNCHY DATA SOLUTIONS, INC. HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE CRUNCHY DATA SOLUTIONS, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE CRUNCHY DATA SOLUTIONS, INC. HAS NO
 * OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
 * MODIFICATIONS.
 */

#ifndef CGROUP_H
#define CGROUP_H

#include "fmgr.h"
#include "parseutils.h"

#define PROC_CGROUP_FILE	"/proc/self/cgroup"
#define CGROUP_V1			"legacy"
#define CGROUP_V2			"unified"
#define CGROUP_HYBRID		"hybrid"
#define CGROUP_DISABLED		"disabled"
#define is_cgroup_v1		(strcmp(cgmode, CGROUP_V1) == 0)
#define is_cgroup_v2		(strcmp(cgmode, CGROUP_V2) == 0)
#define is_cgroup_hy		(strcmp(cgmode, CGROUP_HYBRID) == 0)

extern bool set_cgmode(void);
extern void set_containerized(void);
extern void set_cgpath(void);
extern int cgmembers(int64 **pids);
extern char *get_cgpath_value(char *key);
extern char *get_fq_cgroup_path(FunctionCallInfo fcinfo);

/* exported globals */
extern char *cgmode;
extern kvpairs *cgpath;
extern char *cgrouproot;
extern bool containerized;
extern bool cgroup_enabled;

#endif	/* CGROUP_H */
