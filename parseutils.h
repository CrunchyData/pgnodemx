/*
 * parseutils.h
 *
 * Functions specific to parsing various common string formats
 * 
 * Joe Conway <joe@crunchydata.com>
 *
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2020-2021 Crunchy Data Solutions, Inc.
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

#ifndef PARSEUTILS_H
#define PARSEUTILS_H

typedef struct kvpairs
{
	int		nkvp;
	char  **keys;
	char  **values;
} kvpairs;

extern char **read_nlsv(char *ftr, int *nlines);
extern char *read_one_nlsv(char *ftr);
extern kvpairs *parse_nested_keyed_line(char *line);
extern char **parse_ss_line(char *line, int *ntok);
extern void strip_quotes(char *source, char quote);
extern char **parse_keqv_line(char *line);
extern int64 get_int64_from_file(char *ftr);
extern double get_double_from_file(char *ftr);
extern char *get_string_from_file(char *ftr);
extern char **parse_space_sep_val_file(char *filename, int *nvals);

#endif	/* PARSEUTILS_H */
