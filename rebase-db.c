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

