/*
 * 
 * SQL functions that allow capture of node OS metrics from PostgreSQL
 * Dave Cramer <davecramer@gmail.com>
 * 
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2021 Crunchy Data Solutions, Inc.
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

#ifndef _PROCFUNC_H_
#define _PROCFUNC_H_


#define BIGINT_LEN 20
#define FLOAT_LEN 20
#define INTEGER_LEN 10

#include <ctype.h>
#include <linux/magic.h>

extern char *get_fullcmd(char *pid);
extern char *get_rss(char *rss);
extern char ***read_kv_file( char *filename, int *nlines );
extern void get_uid_username( char *pid, char **uid, char **username );
extern int get_proctab(FuncCallContext *, char **);
extern int get_cputime(char **);
extern int get_loadavg(char **);
extern int get_memusage(char **);


#define PROCFS "/proc"

#endif /* _PROCFUNC_H_ */
