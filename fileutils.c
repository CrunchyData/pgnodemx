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

#include <linux/magic.h>
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC  0x63677270
#endif
#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342
#endif
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/sysmacros.h>

#include "catalog/pg_authid.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#if PG_VERSION_NUM < 100000
#include "access/htup_details.h"
#include "utils/syscache.h"
#endif

#include "fileutils.h"
#include "genutils.h"

uint64	magic_ids[] =
	{ADFS_SUPER_MAGIC,AFFS_SUPER_MAGIC,AFS_SUPER_MAGIC,
	 ANON_INODE_FS_MAGIC,AUTOFS_SUPER_MAGIC,BDEVFS_MAGIC,
	 BINFMTFS_MAGIC,BPF_FS_MAGIC,
	 BTRFS_SUPER_MAGIC,BTRFS_TEST_MAGIC,CGROUP_SUPER_MAGIC,
	 CGROUP2_SUPER_MAGIC,CODA_SUPER_MAGIC,
	 CRAMFS_MAGIC,DEBUGFS_MAGIC,
	 DEVPTS_SUPER_MAGIC,ECRYPTFS_SUPER_MAGIC,
	 EFIVARFS_MAGIC,EFS_SUPER_MAGIC,
	 EXT2_SUPER_MAGIC,EXT3_SUPER_MAGIC,
	 EXT4_SUPER_MAGIC,F2FS_SUPER_MAGIC,
	 FUTEXFS_SUPER_MAGIC,HOSTFS_SUPER_MAGIC,
	 HPFS_SUPER_MAGIC,HUGETLBFS_MAGIC,ISOFS_SUPER_MAGIC,
	 JFFS2_SUPER_MAGIC,MINIX_SUPER_MAGIC,
	 MINIX_SUPER_MAGIC2,MINIX2_SUPER_MAGIC,MINIX2_SUPER_MAGIC2,
	 MINIX3_SUPER_MAGIC,MSDOS_SUPER_MAGIC,
	 MTD_INODE_FS_MAGIC,NCP_SUPER_MAGIC,NFS_SUPER_MAGIC,
	 NILFS_SUPER_MAGIC,
	 OPENPROM_SUPER_MAGIC,
	 OVERLAYFS_SUPER_MAGIC,PIPEFS_MAGIC,PROC_SUPER_MAGIC,
	 PSTOREFS_MAGIC,QNX4_SUPER_MAGIC,QNX6_SUPER_MAGIC,
	 RAMFS_MAGIC,REISERFS_SUPER_MAGIC,
	 SECURITYFS_MAGIC,SELINUX_MAGIC,SMACK_MAGIC,SMB_SUPER_MAGIC,
	 SOCKFS_MAGIC,SQUASHFS_MAGIC,SYSFS_MAGIC,
	 TMPFS_MAGIC,
	 USBDEVICE_SUPER_MAGIC,V9FS_MAGIC,
	 XENFS_SUPER_MAGIC,XFS_SUPER_MAGIC,0};

/* keep in sync with magic_ids above */
char   *magic_names[] =
	{"adfs","affs","afs",
	 "anon_inode_fs","autofs","bdevfs",
	 "binfmtfs","bpf_fs",
	 "btrfs","btrfs_test","cgroup",
	 "cgroup2","coda",
	 "cramfs","debugfs",
	 "devpts","ecryptfs",
	 "efivarfs","efs",
	 "ext2","ext3",
	 "ext4","f2fs",
	 "futexfs","hostfs",
	 "hpfs","hugetlbfs","isofs",
	 "jffs2","minix",
	 "minix12","minix2","minix22",
	 "minix3","msdos",
	 "mtd_inode_fs","ncp","nfs",
	 "nilfs",
	 "openprom",
	 "overlayfs","pipefs","proc",
	 "pstorefs","qnx4","qnx6",
	 "ramfs","reiserfs",
	 "securityfs","selinux","smack","smb",
	 "sockfs","squashfs","sysfs",
	 "tmpfs",
	 "usbdevice","v9fs",
	 "xenfs","xfs",NULL};

/* possible mount flags */
uint64	mflags[] =
	{ST_MANDLOCK,ST_NOATIME,ST_NODEV,ST_NODIRATIME,ST_NOEXEC,
	 ST_NOSUID,ST_RDONLY,ST_RELATIME,ST_SYNCHRONOUS,0};
/* keep in sync with mflags above */
char   *mflagnames[] =
	{"mandlock","noatime","nodev","nodiratime","noexec",
	 "nosuid","rdonly","relatime","synchronous",NULL};

static char *magic_get_name(uint64 magic_id);
static char *mount_flags_to_string(uint64 flags);

#if PG_VERSION_NUM < 100000
/*
 * Get role oid from role name, returns NULL for nonexistent role name
 * if noerr is true.
 */
static Oid
GetRoleIdFromName(char *rolename, bool noerr)
{
	HeapTuple	tuple;
	Oid			result;

	tuple = SearchSysCache1(AUTHNAME, PointerGetDatum(rolename));
	if (!HeapTupleIsValid(tuple))
	{
		if (!noerr)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("role \"%s\" does not exist", rolename)));
		result = InvalidOid;
	}
	else
	{
		result = HeapTupleGetOid(tuple);
		ReleaseSysCache(tuple);
	}
	return result;
}
#endif

void
pgnodemx_check_role(void)
{
#if PG_VERSION_NUM < 100000
#define PGNODEMX_MONITOR_ROLE	"pgmonitor"
	Oid			checkoid = GetRoleIdFromName(PGNODEMX_MONITOR_ROLE, true);

	if (checkoid == InvalidOid)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("role %s does not exist", PGNODEMX_MONITOR_ROLE)));
#else
#define PGNODEMX_MONITOR_ROLE	"pg_monitor"
	Oid			checkoid = DEFAULT_ROLE_MONITOR;
#endif

	/* Limit use to members of the specified role */
	if (!is_member_of_role(GetUserId(), checkoid))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be member of %s role", PGNODEMX_MONITOR_ROLE)));
}

/*
 * Simplified/modified version of same named function in genfile.c.
 * Be careful not to call during _PG_init() because
 * is_member_of_role does not play nice with shared_preload_libraries.
 */
char *
convert_and_check_filename(text *arg)
{
	char	   *filename;

	/* Limit use to members of special role */
	pgnodemx_check_role();

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

/*
 * Convert statfs and stat structs for given path (at least the
 * interesting bits) to a string matrix of key/value pairs, suitable
 * for use in constructing a tuplestore for returning to the client.
 */
char ***
get_statfs_path(char *pname, int *nrow, int *ncol)
{
	struct statfs	buf;
	struct stat		fs;
	int				ret;
	int				i;
	char		 ***values;

	*nrow = 1;
	*ncol = 13;

	ret = stat(pname, &fs);
	if (ret == -1)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				errmsg("pgnodemx: stat error on path %s: %m", pname)));
	}

	ret = statfs(pname, &buf);
	if (ret == -1)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				errmsg("pgnodemx: statfs error on path %s: %m", pname)));
	}

	values = (char ***) palloc((*nrow) * sizeof(char **));
	for (i = 0; i < (*nrow); ++i)
		values[i] = (char **) palloc((*ncol) * sizeof(char *));

	values[0][0] = uint64_to_string((uint64) major(fs.st_dev));
	values[0][1] = uint64_to_string((uint64) minor(fs.st_dev));
	values[0][2] = magic_get_name((uint64) buf.f_type);
	values[0][3] = uint64_to_string((uint64) buf.f_bsize);
	values[0][4] = uint64_to_string((uint64) buf.f_blocks);
	values[0][5] = uint64_to_string((uint64) (buf.f_blocks * buf.f_bsize));
	values[0][6] = uint64_to_string((uint64) buf.f_bfree);
	values[0][7] = uint64_to_string((uint64) (buf.f_bfree * buf.f_bsize));
	values[0][8] = uint64_to_string((uint64) buf.f_bavail);
	values[0][9] = uint64_to_string((uint64) (buf.f_bavail * buf.f_bsize));
	values[0][10] = uint64_to_string((uint64) buf.f_files);
	values[0][11] = uint64_to_string((uint64) buf.f_ffree);
	values[0][12] = mount_flags_to_string((uint64) buf.f_flags);

	return values;
}

static char *
magic_get_name(uint64 magic_id)
{
	int		i = 0;

	while (magic_names[i] != NULL)
	{
		if (magic_ids[i] == magic_id)
			return pstrdup(magic_names[i]);
		++i;
	}

	return pstrdup("unknown");
}

static char *
mount_flags_to_string(uint64 flags)
{
	StringInfo	mflag_str = makeStringInfo();
	int			i = 0;
	bool		found = false;

	while (mflagnames[i] != NULL)
	{
		if ((flags & mflags[i]) == mflags[i])
		{
			if (found)
				appendStringInfo(mflag_str, ",%s", mflagnames[i]);
			else
				appendStringInfo(mflag_str, "%s", mflagnames[i]);
			found = true;
		}
		++i;
	}

	if (!found)
		appendStringInfo(mflag_str, "%s", "none");

	return mflag_str->data;
}
