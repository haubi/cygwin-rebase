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
#include "rebase-db.h"

#if defined(__MSYS__)
/* MSYS has no inttypes.h */
# define PRIx64 "llx"
#else
# include <inttypes.h>
#endif

const char IMG_INFO_MAGIC[4] = "rBiI";
const ULONG IMG_INFO_VERSION = 1;

int
img_info_cmp (const void *a, const void *b)
{
  ULONG64 abase = ((img_info_t *) a)->base;
  ULONG64 bbase = ((img_info_t *) b)->base;

  if (abase < bbase)
    return -1;
  if (abase > bbase)
    return 1;
  return strcmp (((img_info_t *) a)->name, ((img_info_t *) b)->name);
}

int
img_info_name_cmp (const void *a, const void *b)
{
  return strcmp (((img_info_t *) a)->name, ((img_info_t *) b)->name);
}

void
dump_rebasedb_header (FILE *f, img_info_hdr_t const *h)
{
  if (h == NULL)
    {
      fprintf (f, "Rebase DB Header is null\n");
      return;
    }

  fprintf (f,
      "Header\n"
      "  magic  : %c%c%c%c\n"
      "  machine: %s\n"
      "  version: %d\n"
      "  base   : 0x%0*" PRIx64 "\n"
      "  offset : 0x%08lx\n"
      "  downflg: %s\n"
      "  count  : %ld\n",
      h->magic[0], h->magic[1], h->magic[2], h->magic[3],
      (h->machine == IMAGE_FILE_MACHINE_I386
      ? "i386"
      : (h->machine == IMAGE_FILE_MACHINE_AMD64
        ? "x86_64"
        : "unknown")),
      h->version,
      (h->machine == IMAGE_FILE_MACHINE_I386 ? 8 : 12),
      h->base,
      h->offset,
      (h->down_flag ? "true" : "false"),
      h->count);
}

void
dump_rebasedb_entry (FILE *f,
                     img_info_hdr_t const *h,
                     img_info_t const *entry)
{
  if (h == NULL)
    {
      fprintf (f, "Rebase DB Header is null\n");
      return;
    }
  if (entry == NULL)
    {
      fprintf (f, "Rebase DB Entry is null\n");
      return;
    }
  fprintf (f,
      "%-*s base 0x%0*" PRIx64 " size 0x%08lx slot 0x%08lx %c\n",
      h->machine == IMAGE_FILE_MACHINE_I386 ? 45 : 41,
      entry->name,
      h->machine == IMAGE_FILE_MACHINE_I386 ? 8 : 12,
      entry->base,
      entry->size,
      entry->slot_size,
      entry->flag.needs_rebasing ? '*' : ' ');
}

void
dump_rebasedb (FILE *f, img_info_hdr_t const *h,
               img_info_t const *list, unsigned int sz)
{
  unsigned int i;
  if (h == NULL)
    {
      fprintf (f, "Rebase DB Header is null\n");
      return;
    }
  if (list == NULL)
    {
      fprintf (f, "Rebase DB List is null\n");
      return;
    }

  dump_rebasedb_header (stdout, h);
  for (i = 0; i < sz ; ++i)
    {
      dump_rebasedb_entry (stdout, h, &(list[i]));
    }
}

