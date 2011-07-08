/*
 * Copyright (c) 2001, 2002, 2003, 2004, 2008, 2011 Jason Tishler
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * A copy of the GNU General Public License can be found at
 * http://www.gnu.org/
 *
 * Written by Jason Tishler <jason@tishler.net>
 *
 * $Id$
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <getopt.h>
#include <string.h>
#include <inttypes.h>
#include "imagehelper.h"

BOOL collect_image_info (const char *pathname);
void print_image_info ();
BOOL rebase (const char *pathname, ULONG64 *new_image_base);
void parse_args (int argc, char *argv[]);
unsigned long long string_to_ulonglong (const char *string);
void usage ();
BOOL is_rebaseable (const char *pathname);
FILE *file_list_fopen (const char *file_list);
char *file_list_fgets (char *buf, int size, FILE *file);
int file_list_fclose (FILE *file);
void version ();

ULONG64 image_base = 0;
BOOL down_flag = FALSE;
BOOL image_info_flag = FALSE;
ULONG offset = 0;
int args_index = 0;
int verbose = 0;
const char *file_list = 0;
const char *stdin_file_list = "-";

ULONG ALLOCATION_SLOT;	/* Allocation granularity. */

typedef struct _img_info
{
  const char *name;
  ULONG64 base;
  ULONG size;
} img_info_t;

img_info_t *img_info_list = NULL;
unsigned int img_info_size = 0;
unsigned int img_info_max_size = 0;

#ifdef __CYGWIN__
ULONG64 cygwin_dll_image_base = 0;
ULONG cygwin_dll_image_size = 0;
#endif

int
main (int argc, char *argv[])
{
  ULONG64 new_image_base = 0;
  int i = 0;
  SYSTEM_INFO si;
  BOOL status;

  setlocale (LC_ALL, "");
  parse_args (argc, argv);
  new_image_base = image_base;
  GetSystemInfo (&si);
  ALLOCATION_SLOT = si.dwAllocationGranularity;

#ifdef __CYGWIN__
  /* Fetch the Cygwin DLLs data to make sure that DLLs aren't rebased
     into the memory area taken by the Cygwin DLL. */
  GetImageInfos64 ("/bin/cygwin1.dll", NULL,
		   &cygwin_dll_image_base, &cygwin_dll_image_size);
  /* Take the three shared memory areas preceeding the DLL into account. */
  cygwin_dll_image_base -= 3 * ALLOCATION_SLOT;
  /* Add a slack of 8 * 64K at the end of the Cygwin DLL.  This leave a
     bit of room to install newer, bigger Cygwin DLLs, as well as room to
     install non-optimized DLLs for debugging purposes.  Otherwise the
     slightest change might break fork again :-P */
  cygwin_dll_image_size += 3 * ALLOCATION_SLOT + 8 * ALLOCATION_SLOT;
#endif

  /* Rebase file list, if specified. */
  if (file_list)
    {
      status = TRUE;
      char filename[MAX_PATH + 2];
      FILE *file = file_list_fopen (file_list);
      if (!file)
	exit (2);

      while (file_list_fgets (filename, MAX_PATH + 2, file))
	{
	  status = image_info_flag ? collect_image_info (filename)
				    : rebase (filename, &new_image_base);
	  if (!status)
	    break;
	}

      file_list_fclose (file);
      if (!status)
	exit (2);
    }

  /* Rebase command line arguments. */
  for (i = args_index; i < argc; i++)
    {
      const char *filename = argv[i];
      status = image_info_flag ? collect_image_info (filename)
				 : rebase (filename, &new_image_base);
      if (!status)
	exit (2);
    }

  if (image_info_flag)
    print_image_info ();

  exit (0);
}

BOOL
collect_image_info (const char *pathname)
{
  /* Skip if file does not exist to prevent ReBaseImage() from using it's
     stupid search algorithm (e.g, PATH, etc.). */
  if (access (pathname, F_OK) == -1)
    {
      fprintf (stderr, "%s: skipped because nonexistent\n", pathname);
      return TRUE;
    }

  if (img_info_size <= img_info_max_size)
    {
      img_info_max_size += 100;
      img_info_list = (img_info_t *) realloc (img_info_list,
					      img_info_max_size
					      * sizeof (img_info_t));
      if (!img_info_list)
	{
	  fprintf (stderr, "Out of memory.\n");
	  exit (2);
	}
    }

  if (GetImageInfos64 (pathname, NULL,
		       &img_info_list[img_info_size].base,
		       &img_info_list[img_info_size].size))
    img_info_list[img_info_size++].name = strdup (pathname);
  return TRUE;
}

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

void
print_image_info ()
{
  unsigned int i;

  qsort (img_info_list, img_info_size, sizeof (img_info_t), img_info_cmp);
  for (i = 0; i < img_info_size; ++i)
    printf ("%-47s base 0x%08" PRIx64 " size 0x%08lx\n",
	    img_info_list[i].name,
	    img_info_list[i].base,
	    img_info_list[i].size);
}

BOOL
rebase (const char *pathname, ULONG64 *new_image_base)
{
  ULONG64 old_image_base, prev_new_image_base;
  ULONG old_image_size, new_image_size;
  BOOL status, status2;

  /* Skip if file does not exist to prevent ReBaseImage() from using it's
     stupid search algorithm (e.g, PATH, etc.). */
  if (access (pathname, F_OK) == -1)
    {
      fprintf (stderr, "%s: skipped because nonexistent\n", pathname);
      return TRUE;
    }

  /* Skip if not writable. */
  if (access (pathname, W_OK) == -1)
    {
      fprintf (stderr, "%s: skipped because not writable\n", pathname);
      return TRUE;
    }

  /* Skip if not rebaseable. */
  if (!is_rebaseable (pathname))
    {
      if (verbose)
	fprintf (stderr, "%s: skipped because not rebaseable\n", pathname);
      return TRUE;
    }

  /* Calculate next base address, if rebasing down. */
  if (down_flag)
    *new_image_base -= offset;

#ifdef __CYGWIN__
retry:
#endif

  /* Rebase the image. */
  prev_new_image_base = *new_image_base;
  status = ReBaseImage64 ((char*) pathname,	/* CurrentImageName */
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
  status2 = GetLastError ();

  /* If necessary, attempt to fix bad relocations. */
  if (status2 == ERROR_INVALID_DATA)
    {
      if (verbose)
	fprintf (stderr, "%s: fixing bad relocations\n", pathname);
      BOOL status3 = FixImage ((char*) pathname);
      if (!status3)
	{
	  fprintf (stderr, "FixImage (%s) failed with last error = %lu\n",
		   pathname, GetLastError ());
	  return FALSE;
	}

      /* Retry rebase.*/
      status = ReBaseImage64 ((char*) pathname,	/* CurrentImageName */
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
      status2 = GetLastError ();
    }

  /* Check status of rebase. */
  if (status2 != 0)
    {
      fprintf (stderr, "ReBaseImage (%s) failed with last error = %lu\n",
	       pathname, GetLastError ());
      return FALSE;
    }

#ifdef __CYGWIN__
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
      printf ("%s: new base = %" PRIx64 ", new size = %lx\n",
	      pathname,
	      ((down_flag) ? *new_image_base : prev_new_image_base),
	      new_image_size + offset);
    }

  /* Calculate next base address, if rebasing up. */
  if (!down_flag)
    *new_image_base += offset;

  return TRUE;
}

void
parse_args (int argc, char *argv[])
{
  const char *anOptions = "b:dio:T:vV";
  int anOption = 0;

  while ((anOption = getopt (argc, argv, anOptions)) != -1)
    {
      switch (anOption)
	{
	case 'b':
	  image_base = string_to_ulonglong (optarg);
	  break;
	case 'd':
	  down_flag = TRUE;
	  break;
	case 'i':
	  image_info_flag = TRUE;
	  break;
	case 'o':
	  offset = string_to_ulonglong (optarg);
	  break;
	case 'T':
	  file_list = optarg;
	  break;
	case 'v':
	  verbose = TRUE;
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

  if ((image_base == 0 && !image_info_flag)
      || (image_base && image_info_flag))
    {
      usage ();
      exit (1);
    }

  args_index = optind;
}

unsigned long long
string_to_ulonglong (const char *string)
{
  unsigned long long number = 0;
  number = strtoull (string, 0, 0);
  return number;
}

void
usage ()
{
  fprintf (stderr,
	   "usage: rebase -b BaseAddress [-Vdv] [-o Offset] "
	   "[-T FileList | -] Files...\n"
	   "       rebase -i [-T FileList | -] Files...\n");
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
    goto done;

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
  fprintf (stderr, "Copyright (c) 2001, 2002, 2003, 2004, 2008, 2011 "
	   "Ralf Habacker, Jason Tishler, et al.\n");
}
