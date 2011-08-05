/*
 * Copyright (c) 2011 Corinna Vinschen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * See the COPYING file for full license information.
 */
#ifndef REBASE_DB_H
#define REBASE_DB_H

#include <windows.h>
#include <stdio.h>
#if defined(__MSYS__)
/* MSYS has no inttypes.h */
# define PRIu64 "llu"
# define PRIx64 "llx"
#else
# include <inttypes.h>
#endif

#define roundup(x,y)	((((x) + ((y) - 1)) / (y)) * (y))
#define roundup2(x,y)	(((x) + (y) - 1) & ~((y) - 1))

#ifdef __cplusplus
extern "C" {
#endif

extern const char IMG_INFO_MAGIC[4];
extern const ULONG IMG_INFO_VERSION;

#pragma pack (push, 4)

typedef struct _img_info_hdr
{
  CHAR    magic[4];	/* Always IMG_INFO_MAGIC.                            */
  WORD    machine;	/* IMAGE_FILE_MACHINE_I386/IMAGE_FILE_MACHINE_AMD64  */
  WORD    version;	/* Database version, always set to IMG_INFO_VERSION. */
  ULONG64 base;		/* Base address (-b) used to generate database.      */
  ULONG   offset;	/* Offset (-o) used to generate database.            */
  BOOL    down_flag;	/* Always TRUE right now.                            */
  ULONG   count;	/* Number of img_info_t entries following header.    */
} img_info_hdr_t;

typedef struct _img_info
{
  union {
    PCHAR   name;	/* Absolute path to DLL.  The strings are stored     */
    ULONG64 _filler;	/* right after the img_info_t table, in the same     */
  };			/* order as the img_info_t entries.                  */
  ULONG   name_size;	/* Length of name string including trailing NUL.     */
  ULONG64 base;		/* Base address the DLL has been rebased to.         */
  ULONG   size;		/* Size of the DLL at rebased time.                  */
  ULONG   slot_size;	/* Size of the DLL rounded to allocation granularity.*/
  struct {		/* Flags                                             */
    unsigned needs_rebasing : 1; /* Set to 0 in the database.  Used only     */
				 /* while rebasing.                          */
  } flag;
} img_info_t;

#pragma pack (pop)

int img_info_cmp (const void *a, const void *b);
int img_info_name_cmp (const void *a, const void *b);

void dump_rebasedb_header (FILE *f, img_info_hdr_t const *h);
void dump_rebasedb_entry  (FILE *f, img_info_hdr_t const *h,
                           img_info_t const *entry);
void dump_rebasedb (FILE *f, img_info_hdr_t const *h,
                    img_info_t const *list, unsigned int sz);

#ifdef __cplusplus
}
#endif

#endif
