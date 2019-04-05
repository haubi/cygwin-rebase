/*
 * Copyright (c) 2001, 2002, 2003, 2004, 2008, 2011, 2012, 2013 Jason Tishler
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
 *
 * Written by Jason Tishler <jason@tishler.net>
 *
 * $Id$
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#if defined(__CYGWIN__) || defined(__MSYS__)
#include <sys/cygwin.h>
#endif
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include "imagehelper.h"
#include "rebase-db.h"

BOOL save_image_info ();
BOOL load_image_info ();
BOOL merge_image_info ();
BOOL collect_image_info (const char *pathname);
void print_image_info ();
BOOL rebase (const char *pathname, ULONG64 *new_image_base, BOOL down_flag);
void parse_args (int argc, char *argv[]);
unsigned long long string_to_ulonglong (const char *string);
void usage ();
void help ();
BOOL is_rebaseable (const char *pathname);
FILE *file_list_fopen (const char *file_list);
char *file_list_fgets (char *buf, int size, FILE *file);
int file_list_fclose (FILE *file);
void version ();

#if defined(__MSYS__)
/* MSYS has no strtoull */
unsigned long long strtoull(const char *, char **, int);
#endif

#ifdef __x86_64__
WORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
WORD machine = IMAGE_FILE_MACHINE_I386;
#endif
ULONG64 image_base = 0;
ULONG64 low_addr;
BOOL down_flag = FALSE;
BOOL image_info_flag = FALSE;
BOOL image_storage_flag = FALSE;
BOOL image_oblivious_flag = FALSE;
BOOL force_rebase_flag = FALSE;
ULONG offset = 0;
int args_index = 0;
BOOL verbose = FALSE;
BOOL quiet = FALSE;
const char *file_list = 0;
const char *stdin_file_list = "-";
const char *destdir = 0;
size_t destdir_len = 0;

const char *progname;

ULONG ALLOCATION_SLOT;	/* Allocation granularity. */

img_info_t *img_info_list = NULL;
unsigned int img_info_size = 0;
unsigned int img_info_rebase_start = 0;
unsigned int img_info_max_size = 0;

#if !defined (__CYGWIN__) && !defined (__MSYS__)
#undef SYSCONFDIR
#define SYSCONFDIR "/../etc"
#endif
#define IMG_INFO_FILE_I386 SYSCONFDIR "/rebase.db.i386"
#define IMG_INFO_FILE_AMD64 SYSCONFDIR "/rebase.db.x86_64"
#ifdef __x86_64__
#define IMG_INFO_FILE IMG_INFO_FILE_AMD64
#else
#define IMG_INFO_FILE IMG_INFO_FILE_I386
#endif
char *DB_FILE = IMG_INFO_FILE;
char TMP_FILE[] = SYSCONFDIR "/rebase.db.XXXXXX";
char *db_file = NULL;
char *tmp_file = NULL;

#if defined(__CYGWIN__) || defined(__MSYS__)
ULONG64 cygwin_dll_image_base = 0;
ULONG cygwin_dll_image_size = 0;
#endif
#if defined(__MSYS__)
# define CYGWIN_DLL "/usr/bin/msys-1.0.dll"
#elif defined (__CYGWIN__)
# define CYGWIN_DLL "/usr/bin/cygwin1.dll"
#endif

#define LONG_PATH_MAX 32768

char * realname (img_info_t *img)
{
  /* realname eventually contains DESTDIR */
  return img->name + img->name_size;
}

BOOL has_destdir(const char * pathname)
{
  if (!destdir)
    return FALSE;
  /* must start with DESTDIR */
  if (strncmp (pathname, destdir, destdir_len))
    return FALSE;
  /* followed by a path separator */
  if (pathname[destdir_len] != '/')
    return FALSE;
  return TRUE;
}

int
check_base_address_sanity (ULONG64 addr, BOOL at_start)
{
#if defined(__CYGWIN__) || defined(__MSYS__)
  /* Sanity checks for Cygwin:
   *
   * - No DLLs below 0x38000000 on 32 bit, W10 1703+ rebase those on
   *   runtime anyway
   * - No DLLs below 0x2:00000000, ever, on 64 bit.
   */
  if (addr <= low_addr)
    {
      if (at_start)
	fprintf (stderr, "%s: Invalid Baseaddress 0x%" PRIx64 ", must be > 0x%" PRIx64 "\n",
		 progname, (uint64_t) addr, (uint64_t) low_addr);
      else
	fprintf (stderr, "%s: Too many DLLs for available address space: %s\n",
		 progname, strerror (ENOMEM));
      return -1;
    }
#endif
  return 0;
}

void
gen_progname (const char *arg0)
{
  char *p;

  p = strrchr (arg0, '/');
  if (!p)
    p = strrchr (arg0, '\\');
  progname = p ? p + 1 : arg0;
  if ((p = strrchr (progname, '.')) && !strcmp (p, ".exe"))
    *p = 0;
}

int
main (int argc, char *argv[])
{
  int i = 0;
  SYSTEM_INFO si;
  BOOL status;

  setlocale (LC_ALL, "");
  gen_progname (argv[0]);
  parse_args (argc, argv);
  GetSystemInfo (&si);
  ALLOCATION_SLOT = si.dwAllocationGranularity;

  /* If database support has been requested, load database. */
  if (image_storage_flag)
    {
      if (load_image_info () < 0)
	return 2;
      img_info_rebase_start = img_info_size;
    }

#if defined(__MSYS__)
  if (machine == IMAGE_FILE_MACHINE_I386)
    {
      GetImageInfos64 ("/bin/msys-1.0.dll", NULL,
	               &cygwin_dll_image_base, &cygwin_dll_image_size);
    }
#elif defined(__CYGWIN__)
  if (machine == IMAGE_FILE_MACHINE_I386)
    {
      /* Fetch the Cygwin DLLs data to make sure that DLLs aren't rebased
	 into the memory area taken by the Cygwin DLL. */
      GetImageInfos64 ("/bin/cygwin1.dll", NULL,
		       &cygwin_dll_image_base, &cygwin_dll_image_size);
      /* Take the up to four shared memory areas preceeding the DLL into
      	 account. */
      cygwin_dll_image_base -= 4 * ALLOCATION_SLOT;
      /* Add a slack of 8 * 64K at the end of the Cygwin DLL.  This leave a
	 bit of room to install newer, bigger Cygwin DLLs, as well as room to
	 install non-optimized DLLs for debugging purposes.  Otherwise the
	 slightest change might break fork again :-P */
      cygwin_dll_image_size += 4 * ALLOCATION_SLOT + 8 * ALLOCATION_SLOT;
    }
  else
    {
      /* On x86_64 Cygwin, we want to keep free the whole 2 Gigs area in which
	 the Cygwin DLL resides, no matter what. */
      cygwin_dll_image_base = 0x180000000L;
      cygwin_dll_image_size = 0x080000000L;
    }
#endif /* __CYGWIN__ */

  /* Collect file list, if specified. */
  if (file_list)
    {
      char filename[MAX_PATH + 2];
      FILE *file = file_list_fopen (file_list);
      if (!file)
	return 2;

      status = TRUE;
      while (file_list_fgets (filename, MAX_PATH + 2, file))
	{
	  if (strlen (filename) > 0)
	    {
	      status = collect_image_info (filename);
	      if (!status)
		break;
	    }
	}

      file_list_fclose (file);
      if (!status)
	return 2;
    }

  /* Collect command line arguments. */
  for (i = args_index; i < argc; i++)
    {
      const char *filename = argv[i];
      if (strlen (filename) > 0)
	{
	  status = collect_image_info (filename);
	  if (!status)
	    return 2;
	}
    }

  /* Nothing to do? */
  if (img_info_size == 0)
    return 0;

  /* Check what we have to do and do it. */
  if (image_info_flag)
    {
      /* Print. */
      print_image_info ();
    }
  else if (!image_storage_flag)
    {
      /* Rebase. */
      ULONG64 new_image_base = image_base;
      for (i = 0; i < img_info_size; ++i)
	{
	  status = rebase (realname (&img_info_list[i]),
			   &new_image_base, down_flag);
	  if (!status)
	    return 2;
	}
    }
  else
    {
      /* Rebase with database support. */
      BOOL header;

      if (merge_image_info () < 0)
	return 2;
      status = TRUE;
      for (i = 0; i < img_info_size; ++i)
	if (img_info_list[i].flag.needs_rebasing)
	  {
	    ULONG64 new_image_base = img_info_list[i].base;
	    status = rebase (realname (&img_info_list[i]),
			     &new_image_base, FALSE);
	    if (status)
	      img_info_list[i].flag.needs_rebasing = 0;
	  }
      for (header = FALSE, i = 0; i < img_info_size; ++i)
	if (img_info_list[i].flag.cannot_rebase == 1)
	  {
	    if (!header)
	      {
		fputs ("\nThe following DLLs couldn't be rebased "
		       "because they were in use:\n", stderr);
		header = TRUE;
	      }
	    fprintf (stderr, "  %s\n", realname (&img_info_list[i]));
	  }
      for (header = FALSE, i = 0; i < img_info_size; ++i)
	if (img_info_list[i].flag.needs_rebasing)
	  {
	    if (!header)
	      {
		fputs ("\nThe following DLLs couldn't be rebased "
		       "due to errors:\n", stderr);
		header = TRUE;
	      }
	    fprintf (stderr, "  %s\n", realname (&img_info_list[i]));
	  }
      if (save_image_info () < 0)
	return 2;
    }

  return 0;
}

#if !defined(__CYGWIN__) && !defined(__MSYS__)
int
mkstemp (char *name)
{
  return _open (mktemp (name),
      O_RDWR | O_BINARY | O_CREAT | O_EXCL | O_TRUNC | _O_SHORT_LIVED,
      _S_IREAD|_S_IWRITE);
}
#endif

int
save_image_info ()
{
  int i, fd;
  int ret = 0;
  img_info_hdr_t hdr;

  /* Do not re-write the database if --oblivious is active */
  if (image_oblivious_flag)
    return 0;
  /* Drop cannot_rebase flag and remove all DLLs for which rebasing failed
     from the list before storing it in the database file. */
  for (i = 0; i < img_info_size; ++i)
    {
      img_info_list[i].flag.cannot_rebase = 0;
      if (img_info_list[i].flag.needs_rebasing)
	img_info_list[i--] = img_info_list[--img_info_size];
    }
  /* Create a temporary file to write to. */
  fd = mkstemp (tmp_file);
  if (fd < 0)
    {
      fprintf (stderr, "%s: failed to create temporary rebase database: %s\n",
	       progname, strerror (errno));
      return -1;
    }
  qsort (img_info_list, img_info_size, sizeof (img_info_t), img_info_name_cmp);
  /* First write the number of entries. */
  memcpy (hdr.magic, IMG_INFO_MAGIC, 4);
  hdr.machine = machine;
  hdr.version = IMG_INFO_VERSION;
  hdr.base = image_base;
  hdr.offset = offset;
  hdr.down_flag = down_flag;
  hdr.count = img_info_size;
  if (write (fd, &hdr, sizeof (hdr)) < 0)
    {
      fprintf (stderr, "%s: failed to write rebase database: %s\n",
	       progname, strerror (errno));
      ret = -1;
    }
  /* Write the list. */
  else if (write (fd, img_info_list, img_info_size * sizeof (img_info_t)) < 0)
    {
      fprintf (stderr, "%s: failed to write rebase database: %s\n",
	       progname, strerror (errno));
      ret = -1;
    }
  else
    {
      int i;

      /* Write all strings, without the optional DESTDIR part. */
      for (i = 0; i < img_info_size; ++i)
	if (write (fd, img_info_list[i].name,
		   strlen (img_info_list[i].name) + 1) < 0)
	  {
	    fprintf (stderr, "%s: failed to write rebase database: %s\n",
		     progname, strerror (errno));
	    ret = -1;
	    break;
	  }
    }
#if defined(__CYGWIN__) && !defined(__MSYS__)
  /* fchmod is broken on msys */
  fchmod (fd, 0660);
#else
  chmod (tmp_file, 0660);
#endif
  close (fd);
  if (ret < 0)
    unlink (tmp_file);
  else
    {
      if (unlink (db_file) < 0 && errno != ENOENT)
	{
	  fprintf (stderr,
		   "%s: failed to remove old rebase database file \"%s\":\n"
		   "%s\n"
		   "The new rebase database is stored in \"%s\".\n"
		   "Manually remove \"%s\" and rename \"%s\" to \"%s\",\n"
		   "otherwise the new rebase database will be unusable.\n",
		   progname, db_file,
		   strerror (errno),
		   tmp_file,
		   db_file, tmp_file, db_file);
	  ret = -1;
	}
      else if (rename (tmp_file, db_file) < 0)
	{
	  fprintf (stderr,
		   "%s: failed to rename \"%s\" to \"%s\":\n"
		   "%s\n"
		   "Manually rename \"%s\" to \"%s\",\n"
		   "otherwise the new rebase database will be unusable.\n",
		   progname, tmp_file, db_file,
		   strerror (errno),
		   tmp_file, db_file);
	  ret = -1;
	}
    }
  return ret;
}

int
load_image_info ()
{
  int fd;
  ssize_t read_ret;
  int ret = 0;
  int i;
  img_info_hdr_t hdr;

  fd = open (db_file, O_RDONLY | O_BINARY);
  if (fd < 0)
    {
      /* It's no error if the file doesn't exist.  However, in this case
	 the -b option is mandatory. */
      if (errno == ENOENT && image_base)
        return 0;
      fprintf (stderr, "%s: failed to open rebase database \"%s\":\n%s\n",
	       progname, db_file, strerror (errno));
      return -1;
    }
  /* First read the header. */
  if ((read_ret = read (fd, &hdr, sizeof hdr)) != sizeof hdr)
    {
      if (read_ret < 0)
	fprintf (stderr, "%s: failed to read rebase database \"%s\":\n%s\n",
		 progname, db_file, strerror (errno));
      else
	fprintf (stderr, "%s: premature end of rebase database \"%s\".\n",
		 progname, db_file);
      close (fd);
      return -1;
    }
  /* Check the header. */
  if (memcmp (hdr.magic, IMG_INFO_MAGIC, 4) != 0)
    {
      fprintf (stderr, "%s: \"%s\" is not a valid rebase database.\n",
	       progname, db_file);
      close (fd);
      return -1;
    }
  if (hdr.machine != machine)
    {
      if (hdr.machine == IMAGE_FILE_MACHINE_I386)
	fprintf (stderr,
"%s: \"%s\" is a database file for 32 bit DLLs but\n"
"I'm started to handle 64 bit DLLs.  If you want to handle 32 bit DLLs,\n"
"use the -4 option.\n", progname, db_file);
      else if (hdr.machine == IMAGE_FILE_MACHINE_AMD64)
	fprintf (stderr,
"%s: \"%s\" is a database file for 64 bit DLLs but\n"
"I'm started to handle 32 bit DLLs.  If you want to handle 64 bit DLLs,\n"
"use the -8 option.\n", progname, db_file);
      else
	fprintf (stderr, "%s: \"%s\" is a database file for a machine type\n"
			 "I don't know about.", progname, db_file);
      close (fd);
      return -1;
    }
  if (hdr.version != IMG_INFO_VERSION)
    {
      fprintf (stderr, "%s: \"%s\" is a version %u rebase database.\n"
		       "I can only handle versions up to %u.\n",
	       progname, db_file, hdr.version, (uint32_t) IMG_INFO_VERSION);
      close (fd);
      return -1;
    }
  /* If no new image base has been specified, use the one from the header. */
  if (image_base == 0)
    {
      image_base = hdr.base;
      down_flag = hdr.down_flag;
    }
  if (offset == 0)
    offset = hdr.offset;
  /* Don't enforce rebasing if address and offset are unchanged or taken from
     the file anyway. */
  if (image_base == hdr.base && offset == hdr.offset)
    force_rebase_flag = FALSE;
  img_info_size = hdr.count;
  /* Allocate memory for the image list. */
  if (ret == 0)
    {
      img_info_max_size = roundup (img_info_size, 100);
      img_info_list = (img_info_t *) calloc (img_info_max_size,
					     sizeof (img_info_t));
      if (!img_info_list)
	{
	  fprintf (stderr, "%s: Out of memory.\n", progname);
	  ret = -1;
	}
    }
  /* Now read the list. */
  if (ret == 0
      && (read_ret = read (fd, img_info_list,
			   img_info_size * sizeof (img_info_t)))
	 != img_info_size * sizeof (img_info_t))
    {
      if (read_ret < 0)
	fprintf (stderr, "%s: failed to read rebase database \"%s\":\n%s\n",
		 progname, db_file, strerror (errno));
      else
	fprintf (stderr, "%s: premature end of rebase database \"%s\".\n",
		 progname, db_file);
      ret = -1;
    }
  /* Make sure all pointers are NULL. */
  if (ret == 0)
    for (i = 0; i < img_info_size; ++i)
      {
	img_info_list[i].name = NULL;
	/* Ensure that existing database entries are not touched when
	 *  --oblivious is active, even if they are out-of sync with
	 *  reality.  This also applies to when using DESTDIR. */
	if (image_oblivious_flag || destdir)
	  img_info_list[i].flag.cannot_rebase = 2;
      }
  /* Eventually read the strings. */
  if (ret == 0)
    {
      for (i = 0; i < img_info_size; ++i)
	{
	  /* provide realname even if identical to name */
	  img_info_list[i].name = (char *)
				  malloc (img_info_list[i].name_size * 2);
	  if (!img_info_list[i].name)
	    {
	      fprintf (stderr, "%s: Out of memory.\n", progname);
	      ret = -1;
	      break;
	    }
	  if ((read_ret = read (fd, img_info_list[i].name,
				img_info_list[i].name_size))
	      != img_info_list[i].name_size)
	    {
	      if (read_ret < 0)
		fprintf (stderr, "%s: failed to read rebase database \"%s\": "
			 "%s\n", progname, db_file, strerror (errno));
	      else
		fprintf (stderr,
			 "%s: premature end of rebase database \"%s\".\n",
			 progname, db_file);
	      ret = -1;
	      break;
	    }
	  strcpy (realname (&img_info_list[i]), img_info_list[i].name);
	}
    }
  close (fd);
  /* On failure, free all allocated memory and set list pointer to NULL. */
  if (ret < 0)
    {
      for (i = 0; i < img_info_size && img_info_list[i].name; ++i)
	free (img_info_list[i].name);
      free (img_info_list);
      img_info_list = NULL;
      img_info_size = 0;
      img_info_max_size = 0;
    }
  return ret;
}

static BOOL
set_cannot_rebase (img_info_t *img)
{
  /* While --oblivious or DESTDIR is active,
   * cannot_rebase is set to 2 on loading the database entries */
  if (img->flag.cannot_rebase <= 1 )
    {
      int fd = open (realname (img), O_WRONLY);
      if (fd < 0)
	img->flag.cannot_rebase = 1;
      else
	close (fd);
    }
  return img->flag.cannot_rebase;
}

int
merge_image_info ()
{
  int i, end;
  img_info_t *match;
  ULONG64 floating_image_base;

  /* Sort new files from command line by name. */
  qsort (img_info_list + img_info_rebase_start,
	 img_info_size - img_info_rebase_start, sizeof (img_info_t),
	 img_info_name_cmp);
  /* Iterate through new files and eliminate duplicates. */
  for (i = img_info_rebase_start; i + 1 < img_info_size; ++i)
    if ((img_info_list[i].name_size == img_info_list[i + 1].name_size
	 && !strcmp (img_info_list[i].name, img_info_list[i + 1].name))
#if defined(__CYGWIN__) || defined(__MSYS__)
	|| !strcmp (img_info_list[i].name, CYGWIN_DLL)
#endif
       )
      {
	free (img_info_list[i].name);
	memmove (img_info_list + i, img_info_list + i + 1,
		 (img_info_size - i - 1) * sizeof (img_info_t));
	--img_info_size;
	--i;
      }
  /* Iterate through new files and see if they are already available in
     existing database. */
  if (img_info_rebase_start)
    {
      for (i = img_info_rebase_start; i < img_info_size; ++i)
	{
	  /* First test if we can open the DLL for writing.  If not, it's
	     probably blocked by another process. */
	  set_cannot_rebase (&img_info_list[i]);
	  match = bsearch (&img_info_list[i], img_info_list,
			   img_info_rebase_start, sizeof (img_info_t),
			   img_info_name_cmp);
	  if (match)
	    {
	      /* We found a match.  Now test if the "new" file is actually
		 the old file, or if it at least fits into the memory slot
		 of the old file.  If so, screw the new file into the old slot.
		 Otherwise set base to 0 to indicate that this DLL needs a new
		 base address. */
	      if (img_info_list[i].flag.cannot_rebase)
		match->base = img_info_list[i].base;
	      else if (match->base != img_info_list[i].base
		       || match->slot_size < img_info_list[i].slot_size)
		{
		  /* Reuse the old address if possible. */
		  if (match->slot_size < img_info_list[i].slot_size)
		    {
		      match->base = 0;
		      if (verbose)
		        fprintf (stderr, "rebasing %s because it won't fit in it's old slot size\n",
				 realname (&img_info_list[i]));
		    }
		  else if (verbose)
		    fprintf (stderr, "rebasing %s because it's not located at it's old slot\n",
			     realname (&img_info_list[i]));

		  match->flag.needs_rebasing = 1;
		}
	      /* Unconditionally overwrite old with new size. */
	      match->size = img_info_list[i].size;
	      match->slot_size = img_info_list[i].slot_size;
	      /* With an --oblivious active, the files should not
	       * already be in the database.  Warn since the file will
	       * not be touched. */
	      if (image_oblivious_flag)
		fprintf (stderr, "%s: oblivious file \"%s\" already "
			 "found in rebase database "
			 "(file and database kept unchanged).\n",
			 progname, realname (&img_info_list[i]));
	      /* Use new file name, as it may carry DESTDIR. */
	      free (match->name);
	      match->name = img_info_list[i].name;
	      match->name_size = img_info_list[i].name_size;
	      /* Remove new entry from array. */
	      img_info_list[i--] = img_info_list[--img_info_size];
	    }
	  else if (!img_info_list[i].flag.cannot_rebase)
	    {
	      /* Not in database yet.  Set base to 0 to choose a new one. */
	      img_info_list[i].base = 0;
	      if (verbose)
		fprintf (stderr, "rebasing %s because not in database yet\n",
			 realname (&img_info_list[i]));
	    }
	}
    }
  if (!img_info_rebase_start || force_rebase_flag)
    {
      /* No database yet or enforcing a new base address.  Set base of all
	 DLLs to 0, if possible. */
      for (i = 0; i < img_info_size; ++i)
	{
	  /* Test DLLs already in database for writability. */
	  if (i < img_info_rebase_start)
	    set_cannot_rebase (&img_info_list[i]);
	  if (!img_info_list[i].flag.cannot_rebase)
	    {
	      img_info_list[i].base = 0;
	      if (verbose)
		fprintf (stderr, "rebasing %s because forced or database missing\n",
			 realname (&img_info_list[i]));
	    }
	}
      img_info_rebase_start = 0;
    }

  /* Now sort the old part of the list by base address. */
  if (img_info_rebase_start)
    qsort (img_info_list, img_info_rebase_start, sizeof (img_info_t),
	   img_info_cmp);
  /* Perform several tests on the information fetched from the database
     to match with reality. */
  for (i = 0; i < img_info_rebase_start; ++i)
    {
      ULONG64 cur_base;
      ULONG cur_size, slot_size;

      /* Files with the needs_rebasing or cannot_rebase flags set have been
	 checked already. */
      if (img_info_list[i].flag.needs_rebasing
      	  || img_info_list[i].flag.cannot_rebase)
	continue;
      /* Check if the files in the old list still exist.  Drop non-existant
	 or unaccessible files. */
      if (access (img_info_list[i].name, F_OK) == -1
	  || !GetImageInfos64 (img_info_list[i].name, NULL,
			       &cur_base, &cur_size))
	{
	  free (img_info_list[i].name);
	  memmove (img_info_list + i, img_info_list + i + 1,
		   (img_info_size - i - 1) * sizeof (img_info_t));
	  --img_info_rebase_start;
	  --img_info_size;
	  continue;
	}
      slot_size = roundup2 (cur_size, ALLOCATION_SLOT);
      if (set_cannot_rebase (&img_info_list[i]))
	img_info_list[i].base = cur_base;
      else
	{
	  /* If the file has been reinstalled, try to rebase to the same address
	     in the first place. */
	  if (cur_base != img_info_list[i].base)
	    {
	      img_info_list[i].flag.needs_rebasing = 1;
	      if (verbose)
		fprintf (stderr, "rebasing %s because it's base has changed (due to being reinstalled?)\n",
			 realname (&img_info_list[i]));
	      /* Set cur_base to the old base to simplify subsequent tests. */
	      cur_base = img_info_list[i].base;
	    }
	  /* However, if the DLL got bigger and doesn't fit into its slot
	     anymore, rebase this DLL from scratch. */
	  if (i + 1 < img_info_rebase_start
	      && cur_base + slot_size + offset > img_info_list[i + 1].base)
	    {
	      img_info_list[i].base = 0;
	      if (verbose)
		fprintf (stderr, "rebasing %s because it won't fit in it's old slot without overlapping next DLL\n",
			 realname (&img_info_list[i]));
	    }
	  /* Does the previous DLL reach into the address space of this
	     DLL?  This happens if the previous DLL is not rebaseable. */
	  else if (i > 0 && cur_base < img_info_list[i - 1].base
				       + img_info_list[i - 1].slot_size)
	    {
	      img_info_list[i].base = 0;
	      if (verbose)
		fprintf (stderr, "rebasing %s because previous DLL now overlaps\n",
			 realname (&img_info_list[i]));
	    }
	  /* Does the file match the base address requirements?  If not,
	     rebase from scratch. */
	  else if ((down_flag && cur_base + slot_size + offset > image_base)
		   || (!down_flag && cur_base < image_base))
	    {
	      img_info_list[i].base = 0;
	      if (verbose)
		fprintf (stderr, "rebasing %s because it's base address is outside the expected area\n",
			 realname (&img_info_list[i]));
	    }
	}
      /* Unconditionally overwrite old with new size. */
      img_info_list[i].size = cur_size;
      img_info_list[i].slot_size = slot_size;
      /* Make sure all DLLs with base address 0 have the needs_rebasing
	 flag set. */
      if (img_info_list[i].base == 0)
	img_info_list[i].flag.needs_rebasing = 1;
    }
  /* The remainder of the function expects img_info_size to be > 0. */
  if (img_info_size == 0)
    return 0;

  /* Now sort entire list by base address.  The files with address 0 will
     be first. */
  if (!force_rebase_flag)
    qsort (img_info_list, img_info_size, sizeof (img_info_t), img_info_cmp);
  /* Try to fit all DLLs with base address 0 into the given list. */
  /* FIXME: This loop only implements the top-down case.  Implement a
     bottom-up case, too, at one point. */
  floating_image_base = image_base;
  end = img_info_size - 1;
  while (img_info_list[0].base == 0)
    {
      ULONG64 new_base = 0;

      /* Skip trailing entries as long as there is no hole. */
       while (end > 0
	      && img_info_list[end].base + img_info_list[end].slot_size
		 + offset >= floating_image_base)
	{
	  floating_image_base = img_info_list[end].base;
	  --end;
	}

      /* Test if one of the DLLs with address 0 fits into the hole. */
      for (i = 0; img_info_list[i].base == 0; ++i)
	{
	  ULONG64 base = floating_image_base - img_info_list[i].slot_size
		  - offset;
	  /* Check if address is still valid */
	  if (check_base_address_sanity (base, FALSE))
	    return -1;
	  if (base >= img_info_list[end].base + img_info_list[end].slot_size
#if defined(__CYGWIN__) || defined(__MSYS__)
	      /* Don't overlap the Cygwin/MSYS DLL. */
	      && (base >= cygwin_dll_image_base + cygwin_dll_image_size
		  || base + img_info_list[i].slot_size <= cygwin_dll_image_base)
#endif
	     )
	    {
	      new_base = base;
	      break;
	    }
	}
      /* Found a match.  Mount into list. */
      if (new_base)
	{
	  img_info_t tmp = img_info_list[i];
	  tmp.base = new_base;
	  memmove (img_info_list + i, img_info_list + i + 1,
		   (end - i) * sizeof (img_info_t));
	  img_info_list[end] = tmp;
	  continue;
	}
      /* Nothing matches.  Set floating_image_base to the start of the
	 uppermost DLL at this point and try again. */
#if defined(__CYGWIN__) || defined(__MSYS__)
      if (floating_image_base >= cygwin_dll_image_base + cygwin_dll_image_size
	  && img_info_list[end].base < cygwin_dll_image_base)
	  floating_image_base = cygwin_dll_image_base;
      else
#endif
	{
	  floating_image_base = img_info_list[end].base;
	  if (--end < 0)
	    {
	      fprintf (stderr,
		       "%s: Too many DLLs for available address space: %s\n",
		       progname, strerror (ENOMEM));
	      return -1;
	    }
	}
    }

  return 0;
}

BOOL
collect_image_info (const char *pathname)
{
  BOOL ret;
  WORD dll_machine;
  const char *pathname_for_db = pathname;
  PCHAR tmp_pathname;

  if (destdir)
    {
      /* If we operate with DESTDIR, only accept dll files from DESTDIR. */
      if (!has_destdir(pathname))
	{
	  fprintf(stderr, "%s: Path name does not match --destdir argument.",
		  pathname);
	  return FALSE;
	}
      /* But never store DESTDIR in database. */
      pathname_for_db += destdir_len;
    }

  /* Skip if file does not exist to prevent ReBaseImage() from using it's
     stupid search algorithm (e.g, PATH, etc.). */
  if (access (pathname, F_OK) == -1)
    {
      if (!quiet)
	fprintf (stderr, "%s: skipped because nonexistent.\n", pathname);
      return TRUE;
    }

  /* Skip if not rebaseable, but only if we're collecting for rebasing,
     not if we're collecting for printing only. */
  if (!image_info_flag && !is_rebaseable (pathname))
    {
      if (!quiet)
	fprintf (stderr, "%s: skipped because not rebaseable\n", pathname);
      return TRUE;
    }

  if (img_info_size >= img_info_max_size)
    {
      img_info_max_size += 100;
      img_info_list = (img_info_t *) realloc (img_info_list,
					      img_info_max_size
					      * sizeof (img_info_t));
      if (!img_info_list)
	{
	  fprintf (stderr, "%s: Out of memory.\n", progname);
	  return FALSE;
	}
    }

  ret = GetImageInfos64 (pathname, &dll_machine,
			 &img_info_list[img_info_size].base,
			 &img_info_list[img_info_size].size);
  if (!ret)
    {
      if (!quiet)
	fprintf (stderr, "%s: skipped because file info unreadable.\n",
		 pathname);
      return TRUE;
    }
  /* We only support IMAGE_FILE_MACHINE_I386 and IMAGE_FILE_MACHINE_AMD64
     so far. */
  if (machine != IMAGE_FILE_MACHINE_I386
      && machine != IMAGE_FILE_MACHINE_AMD64)
    {
      if (quiet)
	fprintf (stderr, "%s: is an executable for a machine type\n"
			 "I don't know about.", pathname);
      return TRUE;
    }
  /* We either operate on 32 bit or 64 bit files.  Never mix them. */
  if (dll_machine != machine)
    {
      if (!quiet)
	fprintf (stderr, "%s: skipped because wrong machine type.\n",
		 pathname);
      return TRUE;
    }
  img_info_list[img_info_size].slot_size
    = roundup2 (img_info_list[img_info_size].size, ALLOCATION_SLOT);
  img_info_list[img_info_size].flag.needs_rebasing = 1;
  img_info_list[img_info_size].flag.cannot_rebase = 0;
  /* This back and forth from POSIX to Win32 is a way to get a full path
     more thoroughly.  For instance, the difference between /bin and
     /usr/bin will be eliminated. */
#if defined (__MSYS__)
  {
    char w32_path[MAX_PATH];
    char full_path[MAX_PATH];
    cygwin_conv_to_full_win32_path (pathname_for_db, w32_path);
    cygwin_conv_to_full_posix_path (w32_path, full_path);
    img_info_list[img_info_size].name_size = strlen (full_path) + 1;
    /* allocate for realname at name+name_size */
    img_info_list[img_info_size].name
      = (char*) malloc (img_info_list[img_info_size].name_size +
		        strlen (pathname) + 1);
    if (!img_info_list[img_info_size].name)
      {
	fprintf (stderr, "%s: Out of memory.\n", progname);
	return FALSE;
      }
    strcpy(img_info_list[img_info_size].name, full_path);
    strcpy (realname (&img_info_list[img_info_size]), pathname);
  }
#elif defined (__CYGWIN__)
  {
    PWSTR w32_path = cygwin_create_path (CCP_POSIX_TO_WIN_W, pathname_for_db);
    if (!w32_path)
      {
	fprintf (stderr, "%s: Out of memory.\n", progname);
	return FALSE;
      }
    tmp_pathname = cygwin_create_path (CCP_WIN_W_TO_POSIX, w32_path);
    if (!tmp_pathname)
      {
	fprintf (stderr, "%s: Out of memory.\n", progname);
	return FALSE;
      }
    img_info_list[img_info_size].name_size = strlen (tmp_pathname) + 1;
    /* allocate for realname at name+name_size */
    img_info_list[img_info_size].name
      = (char*) malloc (img_info_list[img_info_size].name_size +
                        strlen (pathname) + 1);
    if (!img_info_list[img_info_size].name)
      {
	fprintf (stderr, "%s: Out of memory.\n", progname);
	return FALSE;
      }
    strcpy (img_info_list[img_info_size].name, tmp_pathname);
    free (tmp_pathname);
    free (w32_path);
    strcpy (realname (&img_info_list[img_info_size]), pathname);
  }
#else
  {
    char full_path[MAX_PATH];
    GetFullPathName (pathname_for_db, MAX_PATH, full_path, NULL);
    img_info_list[img_info_size].name_size = strlen (full_path) + 1;
    /* allocate for realname at name+name_size */
    img_info_list[img_info_size].name
      = (char*) malloc (img_info_list[img_info_size].name_size +
		        strlen (pathname) + 1);
    if (!img_info_list[img_info_size].name)
      {
	fprintf (stderr, "%s: Out of memory.\n", progname);
	return FALSE;
      }
    strcpy (img_info_list[img_info_size].name, full_path);
    strcpy (realname (&img_info_list[img_info_size]), pathname);
  }
#endif
  if (verbose)
    fprintf (stderr, "rebasing %s because filename given on command line\n",
	     realname (&img_info_list[img_info_size]));
  ++img_info_size;
  return TRUE;
}

void
print_image_info ()
{
  unsigned int i;
  /* Default name field width to longest available for 80 char display. */
  int name_width = (machine == IMAGE_FILE_MACHINE_I386) ? 45 : 41;

  /* Sort list by name. */
  qsort (img_info_list, img_info_size, sizeof (img_info_t), img_info_name_cmp);
  /* Iterate through list and eliminate duplicates. */
  for (i = 0; i + 1 < img_info_size; ++i)
    if (img_info_list[i].name_size == img_info_list[i + 1].name_size
	&& !strcmp (img_info_list[i].name, img_info_list[i + 1].name))
      {
	/* Remove duplicate, but prefer one from the command line over one
	   from the database, because the one from the command line reflects
	   the reality, while the database is wishful thinking. */
	if (img_info_list[i].flag.needs_rebasing == 0)
	  {
	    free (img_info_list[i].name);
	    memmove (img_info_list + i, img_info_list + i + 1,
		     (img_info_size - i - 1) * sizeof (img_info_t));
	  }
	else
	  {
	    free (img_info_list[i + 1].name);
	    if (i + 2 < img_info_size)
	      memmove (img_info_list + i + 1, img_info_list + i + 2,
		       (img_info_size - i - 2) * sizeof (img_info_t));
	  }
	--img_info_size;
	--i;
      }
  /* For entries loaded from database, collect image info to reflect reality.
     Also, collect_image_info sets needs_rebasing to 1, so reset here.
     Also, fetch the longest name length for formatting purposes. */
  for (i = 0; i < img_info_size; ++i)
    {
      if (img_info_list[i].flag.needs_rebasing == 0)
	{
	  ULONG64 base;
	  ULONG size;

	  if (GetImageInfos64 (realname (&img_info_list[i]), NULL, &base, &size))
	    {
	      img_info_list[i].base = base;
	      img_info_list[i].size = size;
	      img_info_list[i].slot_size
		= roundup2 (img_info_list[i].size, ALLOCATION_SLOT);
	    }
	}
      else
	img_info_list[i].flag.needs_rebasing = 0;
      name_width = max (name_width, img_info_list[i].name_size - 1);
    }
  /* Now sort by address. */
  qsort (img_info_list, img_info_size, sizeof (img_info_t), img_info_cmp);
  for (i = 0; i < img_info_size; ++i)
    {
      int tst;
      ULONG64 end = img_info_list[i].base + img_info_list[i].slot_size;

      /* Check for overlap and mark both DLLs. */
      for (tst = i + 1;
	   tst < img_info_size && img_info_list[tst].base < end;
	   ++tst)
	{
	  img_info_list[i].flag.needs_rebasing = 1;
	  img_info_list[tst].flag.needs_rebasing = 1;
	}
      printf ("%-*s base 0x%0*" PRIx64 " size 0x%08x %c\n",
	      name_width,
	      realname (&img_info_list[i]),
	      machine == IMAGE_FILE_MACHINE_I386 ? 8 : 12,
	      (uint64_t) img_info_list[i].base,
	      (uint32_t) img_info_list[i].size,
	      img_info_list[i].flag.needs_rebasing ? '*' : ' ');
    }
}

BOOL
rebase (const char *pathname, ULONG64 *new_image_base, BOOL down_flag)
{
  ULONG64 old_image_base, prev_new_image_base;
  ULONG old_image_size, new_image_size;
  BOOL status;

  /* Skip if not writable. */
  if (access (pathname, W_OK) == -1)
    {
      if (!quiet)
	fprintf (stderr, "%s: skipped because not writable\n", pathname);
      return TRUE;
    }

  /* Calculate next base address, if rebasing down. */
  if (down_flag)
    *new_image_base -= offset;

#if defined(__CYGWIN__) || defined(__MSYS__)
retry:
#endif

  /* Rebase the image. */
  prev_new_image_base = *new_image_base;
  ReBaseImage64 ((char*) pathname,	/* CurrentImageName */
		 "",			/* SymbolPath */
		 TRUE,			/* fReBase */
		 FALSE,		/* fRebaseSysfileOk */
		 down_flag,		/* fGoingDown */
		 0,			/* CheckImageSize */
		 &old_image_size,	/* OldImageSize */
		 &old_image_base,	/* OldImageBase */
		 &new_image_size,	/* NewImageSize */
		 new_image_base,	/* NewImageBase */
		 time (0));		/* TimeStamp */

  /* MS's ReBaseImage seems to never return false! */
  status = GetLastError ();

  /* If necessary, attempt to fix bad relocations. */
  if (status == ERROR_INVALID_DATA)
    {
      if (verbose)
	fprintf (stderr, "%s: fixing bad relocations\n", pathname);
      BOOL status3 = FixImage ((char*) pathname);
      if (!status3)
	{
	  fprintf (stderr, "FixImage (%s) failed with last error = %u\n",
		   pathname, (uint32_t) GetLastError ());
	  return FALSE;
	}

      /* Retry rebase.*/
      ReBaseImage64 ((char*) pathname,	/* CurrentImageName */
		     "",		/* SymbolPath */
		     TRUE,		/* fReBase */
		     FALSE,		/* fRebaseSysfileOk */
		     down_flag,	/* fGoingDown */
		     0,		/* CheckImageSize */
		     &old_image_size,	/* OldImageSize */
		     &old_image_base,	/* OldImageBase */
		     &new_image_size,	/* NewImageSize */
		     new_image_base,	/* NewImageBase */
		     time (0));	/* TimeStamp */

      /* MS's ReBaseImage seems to never return false! */
      status = GetLastError ();
    }

  /* Check status of rebase. */
  if (status != 0)
    {
      fprintf (stderr, "ReBaseImage (%s) failed with last error = %u\n",
	       pathname, (uint32_t) GetLastError ());
      return FALSE;
    }

#if defined(__CYGWIN__) || defined(__MSYS__)
  /* Avoid the case that a DLL is rebased into the address space taken
     by the Cygwin DLL.  Only test in down_flag == TRUE case, otherwise
     the return value in new_image_base is not meaningful */
  if (down_flag
      && *new_image_base >= cygwin_dll_image_base
      && *new_image_base <= cygwin_dll_image_base + cygwin_dll_image_size)
    {
      *new_image_base = cygwin_dll_image_base - new_image_size;
      goto retry;
    }
#endif

  /* Display rebase results, if verbose. */
  if (verbose)
    {
      printf ("%s: new base = %" PRIx64 ", new size = %x\n",
	      pathname,
	      (uint64_t) ((down_flag) ? *new_image_base : prev_new_image_base),
	      (uint32_t) (new_image_size + offset));
    }

  /* Calculate next base address, if rebasing up. */
  if (!down_flag)
    *new_image_base += offset;

  return TRUE;
}

static struct option long_options[] = {
  {"32",	no_argument,	   NULL, '4'},
  {"64",	no_argument,	   NULL, '8'},
  {"base",	required_argument, NULL, 'b'},
  {"destdir",	required_argument, NULL, 'D'},
  {"down",	no_argument,	   NULL, 'd'},
  {"help",	no_argument,	   NULL, 'h'},
  {"usage",	no_argument,	   NULL, 'h'},
  {"info",	no_argument,	   NULL, 'i'},
  {"offset",	required_argument, NULL, 'o'},
  {"oblivious",	no_argument,	   NULL, 'O'},
  {"quiet",	no_argument,	   NULL, 'q'},
  {"database",	no_argument,	   NULL, 's'},
  {"touch",	no_argument,	   NULL, 't'},
  {"filelist",	required_argument, NULL, 'T'},
  {"no-dynamicbase", no_argument,  NULL, 'n'},
  {"verbose",	no_argument,	   NULL, 'v'},
  {"version",	no_argument,	   NULL, 'V'},
  {NULL,	no_argument,	   NULL,  0 }
};

static const char *short_options = "48b:D:dhino:OqstT:vV";

void
parse_args (int argc, char *argv[])
{
  int opt = 0;
  int count_file_list = 0;

  while ((opt = getopt_long (argc, argv, short_options, long_options, NULL))
	 != -1)
    {
      switch (opt)
	{
	case '4':
	  machine = IMAGE_FILE_MACHINE_I386;
	  DB_FILE = IMG_INFO_FILE_I386;
	  break;
	case '8':
	  machine = IMAGE_FILE_MACHINE_AMD64;
	  DB_FILE = IMG_INFO_FILE_AMD64;
	  break;
	case 'b':
	  image_base = string_to_ulonglong (optarg);
	  force_rebase_flag = TRUE;
	  break;
	case 'D':
	  destdir = optarg;
	  destdir_len = strlen(destdir);
	  break;
	case 'd':
	  down_flag = TRUE;
	  break;
	case 'i':
	  image_info_flag = TRUE;
	  break;
	case 'o':
	  offset = string_to_ulonglong (optarg);
	  force_rebase_flag = TRUE;
	  break;
	case 'q':
	  quiet = TRUE;
	  break;
	case 'O':
	  image_oblivious_flag = TRUE;
	  /* -O implies -s, which in turn implies -d, so intentionally
	   * fall through to -s. */
	case 's':
	  image_storage_flag = TRUE;
	  /* FIXME: For now enforce top-down rebasing when using the database.*/
	  down_flag = TRUE;
	  break;
	case 't':
	  ReBaseChangeFileTime = TRUE;
	  break;
	case 'T':
	  if (count_file_list++)
	    {
	      /* Currently only one file list allowed.  Would need an
	       * array of file names and a loop around the file reader
	       * for multiple occurences of this switch. */
	      usage ();
	      exit (1);
	    }
	  file_list = optarg;
	  break;
	case 'n':
	  ReBaseDropDynamicbaseFlag = TRUE;
	  break;
	case 'v':
	  verbose = TRUE;
	  break;
	case 'h':
	  help ();
	  exit (1);
	  break;
	case 'V':
	  version ();
	  exit (1);
	  break;
	default:
	  usage ();
	  exit (1);
	  break;
	}
    }

  if ((image_base == 0 && !image_info_flag && !image_storage_flag)
      || (destdir && !image_storage_flag)
      || (image_base && image_info_flag))
    {
      usage ();
      exit (1);
    }

  if (machine == IMAGE_FILE_MACHINE_I386 && image_base > 0xffffffff)
    {
      fprintf (stderr,
	       "%s: Base address 0x%" PRIx64 " too big for 32 bit machines.\n",
	       progname, (uint64_t) image_base);
      exit (1);
    }

  /* The low address for 32 bit is extremly low, and apparently
     W10 1703 and later rebase all DLLs with start addresses < 0x38000000
     at runtime.  However, we have so many DLLs that a hardcoded lowest
     address of 0x38000000 is just not feasible. */
  low_addr = (machine == IMAGE_FILE_MACHINE_I386) ? 0x001000000ULL
						  : 0x200000000ULL;

  if (image_base && check_base_address_sanity (image_base, TRUE) < 0)
    exit (1);

  args_index = optind;

  /* Initialize db_file and tmp_file from pattern */
#if defined(__CYGWIN__) || defined(__MSYS__)
  /* We don't explicitly free these, but (a) this function is only
   * called once, and (b) we wouldn't free until exit() anyway, and
   * that will happen automatically upon process cleanup.
   */
  db_file = strdup(DB_FILE);
  tmp_file = strdup(TMP_FILE);
#else
  {
    char exepath[LONG_PATH_MAX];
    char* p = NULL;
    char* p2 = NULL;
    size_t sz = 0;

    if (!GetModuleFileNameA (NULL, exepath, LONG_PATH_MAX))
      fprintf (stderr, "%s: can't determine rebase installation path\n",
	       progname);

    /* strip off exename and trailing slash */
    sz = strlen (exepath);
    p = exepath + sz - 1;
    while (p && (p > exepath) && (*p == '/' || *p == '\\'))
      {
        *p = '\0';
        p--;
      }
    p = strrchr(exepath, '/');
    p2 = strrchr(exepath, '\\');
    if (p || p2)
      {
        if (p2 > p)
          p = p2;
        if (p > exepath)
          *p = '\0';
        else
          {
            p++;
            *p = '\0';
          }
      }
    sz = strlen (exepath);

    /* We don't explicitly free these, but (a) this function is only
     * called once, and (b) we wouldn't free until exit() anyway, and
     * that will happen automatically upon process cleanup.
     */
    db_file = (char *) calloc(sz + strlen(DB_FILE) + 1, sizeof(char));
    tmp_file = (char *) calloc(sz + strlen(TMP_FILE) + 1, sizeof(char));
    strcpy(db_file, exepath);
    strcpy(&db_file[sz], DB_FILE);
    strcpy(tmp_file, exepath);
    strcpy(&tmp_file[sz], TMP_FILE);

    for (p = db_file; *p != '\0'; p++)
      if (*p == '/')
        *p = '\\';
    for (p = tmp_file; *p != '\0'; p++)
      if (*p == '/')
        *p = '\\';
  }
#endif
}

unsigned long long
string_to_ulonglong (const char *string)
{
  unsigned long long number = 0;
  number = strtoull (string, NULL, 0);
  return number;
}

void
usage ()
{
  fprintf (stderr,
"usage: %s [-b BaseAddress] [-o Offset] [-D destdir] [-48dOsvV]"
" [-T [FileList | -]] Files...\n"
"       %s -i [-48Os] [-T [FileList | -]] Files...\n"
"       %s --help or --usage for full help text\n",
	   progname, progname, progname);
}

void
help ()
{
  printf ("\
Usage: %s [OPTIONS] [FILE]...\n\
Rebase PE files, usually DLLs, to a specified address or address range.\n\
\n\
  -4, --32                Only rebase 32 bit DLLs."
#ifndef __x86_64__
                          "  This is the default."
#endif
"\n\
  -8, --64                Only rebase 64 bit DLLs."
#ifdef __x86_64__
                          "  This is the default."
#endif
"\n\
  -b, --base=BASEADDRESS  Specifies the base address at which to start rebasing.\n\
  -s, --database          Utilize the rebase database to find unused memory\n\
                          slots to rebase the files on the command line to.\n\
                          (Implies -d).\n\
                          If -b is given, too, the database gets recreated.\n\
  -O, --oblivious         Do not change any files already in the database\n\
                          and do not record any changes to the database.\n\
                          (Implies -s).\n\
  -i, --info              Rather then rebasing, just print the current base\n\
                          address and size of the files.  With -s, use the\n\
                          database.  The files are ordered by base address.\n\
                          A '*' at the end of the line is printed if a\n\
                          collisions with an adjacent file is detected.\n\
\n\
  One of the options -b, -s or -i is mandatory.  If no rebase database exists\n\
  yet, -b is required together with -s.\n\
\n\
  -d, --down              Treat the BaseAddress as upper ceiling and rebase\n\
                          files top-down from there.  Without this option the\n\
                          files are rebased from BaseAddress bottom-up.\n\
                          With the -s option, this option is implicitly set.\n\
  -n, --no-dynamicbase    Remove PE dynamicbase flag from rebased DLLs, if set.\n\
  -o, --offset=OFFSET     Specify an additional offset between adjacent DLLs\n\
                          when rebasing.  Default is no offset.\n\
  -t, --touch             Use this option to make sure the file's modification\n\
                          time is bumped if it has been successfully rebased.\n\
                          Usually rebase does not change the file's time.\n\
  -T, --filelist=FILE     Also rebase the files specified in FILE.  The format\n\
                          of FILE is one DLL per line.\n\
  -D, --destdir=DESTDIR   Files to be rebased are in staging directory DESTDIR.\n\
                          Record them to the database without DESTDIR, they may\n\
                          be moved there without running rebase again.\n\
                          (Requires -s).\n\
  -q, --quiet             Be quiet about non-critical issues.\n\
  -v, --verbose           Print some debug output.\n\
  -V, --version           Print version info and exit.\n\
  -h, --help, --usage     This help.\n",
	  progname);
}

BOOL
is_rebaseable (const char *pathname)
{
  const int pe_signature_offset_offset = 0x3c;
  const int pe_characteristics_offset = 150;
  short int pe_signature_offset = 0;
  BOOL status = FALSE;
  int fd, size;
  long offset;
  DWORD pe_sig;
  WORD pe_char;

  fd = open (pathname, O_RDONLY);
  if (fd == -1)
    return status;

  offset = lseek (fd, pe_signature_offset_offset, SEEK_SET);
  if (offset == -1)
    goto done;

  size = read (fd, &pe_signature_offset, sizeof (pe_signature_offset));
  if (size != sizeof(pe_signature_offset))
    goto done;

  offset = lseek (fd, pe_signature_offset, SEEK_SET);
  if (offset == -1)
    goto done;

  pe_sig = 0;
  size = read (fd, &pe_sig, sizeof (pe_sig));
  if (size != sizeof (pe_sig))
    goto done;

  if (pe_sig != IMAGE_NT_SIGNATURE)
    goto done;

  lseek (fd, 0, SEEK_SET);
  offset = lseek (fd, pe_characteristics_offset, SEEK_CUR);
  if (offset == -1)
    goto done;

  pe_char = 0;
  size = read (fd, &pe_char, sizeof (pe_char));
  if (size != sizeof (pe_char))
    goto done;

  status = ((pe_char & IMAGE_FILE_RELOCS_STRIPPED) == 0) ? TRUE : FALSE;

done:
  close (fd);
  return status;
}

FILE *
file_list_fopen (const char *file_list)
{
  FILE *file = stdin;
  if (strcmp(file_list, stdin_file_list) != 0)
    {
      file = fopen (file_list, "rt");
      if (!file)
	fprintf (stderr, "cannot read %s\n", file_list);
    }
  return file;
}

char *
file_list_fgets (char *buf, int size, FILE *file)
{
  char *status = fgets (buf, size, file);
  if (status)
    {
      size_t length = strlen (buf);
      if (buf[length - 1] == '\n')
	buf[length - 1] = '\0';
    }
  return status;
}

int
file_list_fclose (FILE *file)
{
  int status = 0;
  if (strcmp(file_list, stdin_file_list) != 0)
    status = fclose (file);
  return status;
}

void
version ()
{
  fprintf (stderr, "rebase version %s (imagehelper version %s)\n",
	   VERSION, LIB_VERSION);
  fprintf (stderr, "Copyright (c) 2001, 2002, 2003, 2004, 2008, 2011, 2012, "
	   "2013 Ralf Habacker, Jason Tishler, et al.\n");
}

