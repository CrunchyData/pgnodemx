/*
 * kdapi.h
 *
 * Functions specific to capture and manipulation of Kubernetes
 * Downward API files
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

#include "postgres.h"

#include "fmgr.h"
#include "lib/stringinfo.h"

#include "fileutils.h"
#include "kdapi.h"
#include "parseutils.h"

char *kdapi_path = NULL;
bool kdapi_enabled = true;

/*
 * Take input filename from caller, make sure it is acceptable
 * (not absolute, no relative parent references, caller belongs
 * to correct role), and concatenates it with the path to the
 * related controller in the cgroup filesystem. The returned
 * value is a "fully qualified" path to the file of interest
 * for the purposes of cgroup virtual files.
 */
char *
get_fq_kdapi_path(FunctionCallInfo fcinfo)
{
	StringInfo	ftr = makeStringInfo();
	char	   *fname = convert_and_check_filename(PG_GETARG_TEXT_PP(0));

	appendStringInfo(ftr, "%s/%s", kdapi_path, fname);

	return pstrdup(ftr->data);
}
