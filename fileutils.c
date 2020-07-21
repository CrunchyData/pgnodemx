/*
 * fileutils.c
 *
 * Functions specific to accessing the file system
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

#include "catalog/pg_authid.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "fileutils.h"

/*
 * Simplified/modified version of same named function in genfile.c.
 * Be careful not to call during _PG_init() because
 * is_member_of_role does not play nice with shared_preload_libraries.
 */
char *
convert_and_check_filename(text *arg)
{
	char	   *filename;

	/* Limit use to members of the 'pg_monitor' role */
	if (!is_member_of_role(GetUserId(), DEFAULT_ROLE_MONITOR))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be member of pg_monitor role")));

	filename = text_to_cstring(arg);
	canonicalize_path(filename);	/* filename can change length here */

	/* Disallow absolute paths */
	if (is_absolute_path(filename))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("reference to absolute path not allowed")));

	/* Disallow references to parent directory */
	if (path_contains_parent_reference(filename))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("reference to parent directory (\"..\") not allowed")));

	return filename;
}

/*
 * read_vfs(): stripped down copy of read_binary_file() from
 * genfile.c
 */

/* Minimum amount to read at a time */
#define MIN_READ_SIZE 4096
char *
read_vfs(char *filename)
{
	char		   *buf;
	size_t			nbytes = 0;
	FILE		   *file;
	StringInfoData	sbuf;

	if ((file = AllocateFile(filename, PG_BINARY_R)) == NULL)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\" for reading: %m",
						filename)));

	initStringInfo(&sbuf);

	while (!(feof(file) || ferror(file)))
	{
		size_t		rbytes;

		/*
		 * If not at end of file, and sbuf.len is equal to
		 * MaxAllocSize - 1, then either the file is too large, or
		 * there is nothing left to read. Attempt to read one more
		 * byte to see if the end of file has been reached. If not,
		 * the file is too large; we'd rather give the error message
		 * for that ourselves.
		 */
		if (sbuf.len == MaxAllocSize - 1)
		{
			char	rbuf[1]; 

			if (fread(rbuf, 1, 1, file) != 0 || !feof(file))
				ereport(ERROR,
						(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
						 errmsg("file length too large")));
			else
				break;
		}

		/* OK, ensure that we can read at least MIN_READ_SIZE */
		enlargeStringInfo(&sbuf, MIN_READ_SIZE);

		/*
		 * stringinfo.c likes to allocate in powers of 2, so it's likely
		 * that much more space is available than we asked for.  Use all
		 * of it, rather than making more fread calls than necessary.
		 */
		rbytes = fread(sbuf.data + sbuf.len, 1,
					   (size_t) (sbuf.maxlen - sbuf.len - 1), file);
		sbuf.len += rbytes;
		nbytes += rbytes;
	}

	/*
	 * Keep a trailing null in place, same as what
	 * appendBinaryStringInfo() would do.
	 */
	sbuf.data[sbuf.len] = '\0';

	/* Now we can commandeer the stringinfo's buffer as the result */
	buf = sbuf.data;

	if (ferror(file))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));

	FreeFile(file);

	return buf;
}
