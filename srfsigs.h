/*
 * 
 * Return signatures for SRFs
 * 
 * This code is released under the PostgreSQL license.
 *
 * Copyright 2021-2022 Crunchy Data Solutions, Inc.
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

#ifndef _SRFSIGS_H_
#define _SRFSIGS_H_

/* function return signatures */
extern Oid text_sig[];
extern Oid bigint_sig[];
extern Oid text_text_sig[];
extern Oid text_bigint_sig[];
extern Oid text_text_bigint_sig[];
extern Oid text_text_float8_sig[];
extern Oid _2_numeric_text_9_numeric_text_sig[];
extern Oid _4_bigint_6_text_sig[];
extern Oid bigint_bigint_text_11_bigint_sig[];
extern Oid text_16_bigint_sig[];
extern Oid _5_bigint_sig[];
extern Oid int_7_numeric_sig[];
extern Oid int_text_int_text_sig[];
extern Oid load_avg_sig[];
extern Oid proc_pid_stat_sig[];

#endif /* _SRFSIGS_H_ */
