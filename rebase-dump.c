/*
 * Copyright (c) 2011 Charles Wilson
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
#if defined(__MSYS__)
/* MSYS has no inttypes.h */
# define PRIx64 "llx"
#else
# include <inttypes.h>
#endif
#include <errno.h>
#include <windows.h>
#include "rebase-db.h"

BOOL load_image_info ();
void parse_args (int argc, char *argv[]);
void usage ();
void help ();
void version ();

int args_index = 0;
BOOL verbose = FALSE;
BOOL quiet = FALSE;

const char *progname;

img_info_hdr_t hdr;
img_info_t *img_info_list = NULL;
unsigned int img_info_size = 0;
unsigned int img_info_rebase_start = 0;
unsigned int img_info_max_size = 0;
char *db_file = NULL;

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
  setlocale (LC_ALL, "");
  gen_progname (argv[0]);
  parse_args (argc, argv);

  if (args_index != argc - 1)
    {
      fprintf (stderr, "%s: no database file specified\n",
	       progname);
      usage ();
      return 1;
    }
  db_file = strdup (argv[args_index]);

  if (load_image_info () < 0)
    return 2;

  /* Nothing to do? */
  if (img_info_size == 0)
    return 0;

  dump_rebasedb (stdout, &hdr, img_info_list, img_info_size);
  return 0;
}

int
load_image_info ()
{
  int fd;
  int ret = 0;
  int i;

  fd = open (db_file, O_RDONLY | O_BINARY);
  if (fd < 0)
    {
      fprintf (stderr, "%s: failed to open rebase database \"%s\":\n%s\n",
	       progname, db_file, strerror (errno));
      return -1;
    }
  /* First read the header. */
  if (read (fd, &hdr, sizeof hdr) < 0)
    {
      fprintf (stderr, "%s: failed to read rebase database \"%s\":\n%s\n",
	       progname, db_file, strerror (errno));
      close (fd);
      return -1;
    }
  if (verbose)
    printf ("== read %" PRIu64 " (0x%08" PRIx64 ") bytes (database header)\n",
	    (unsigned long long) sizeof hdr, (unsigned long long) sizeof hdr);

  /* Check the header. */
  if (memcmp (hdr.magic, IMG_INFO_MAGIC, 4) != 0)
    {
      fprintf (stderr, "%s: \"%s\" is not a valid rebase database.\n",
	       progname, db_file);
      close (fd);
      return -1;
    }
  if (verbose)
    {
      dump_rebasedb_header (stdout, &hdr);
    }
  if (hdr.machine != IMAGE_FILE_MACHINE_I386 &&
      hdr.machine != IMAGE_FILE_MACHINE_AMD64)
    {
      fprintf (stderr, "%s: \"%s\" is a database file for a machine type\n"
		       "I don't know about.", progname, db_file);
      close (fd);
      return -1;
    }
  if (hdr.version != IMG_INFO_VERSION)
    {
      fprintf (stderr, "%s: \"%s\" is a version %u rebase database.\n"
		       "I can only handle versions up to %lu.\n",
	       progname, db_file, hdr.version, IMG_INFO_VERSION);
      close (fd);
      return -1;
    }
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
      && read (fd, img_info_list, img_info_size * sizeof (img_info_t)) < 0)
    {
      fprintf (stderr, "%s: failed to read rebase database \"%s\":\n%s\n",
	       progname, db_file, strerror (errno));
      ret = -1;
    }
  if (ret == 0 && verbose)
    {
      printf ("== read %" PRIu64 " (0x%08" PRIx64 ") bytes (database w/o strings)\n",
              (unsigned long long) img_info_size * sizeof (img_info_t),
              (unsigned long long) img_info_size * sizeof (img_info_t));
    }
  /* Make sure all pointers are NULL (also dump db as read) */
  if (ret == 0)
    {
      if (verbose)
	printf ("---- database records without strings ----\n");

      for (i = 0; i < img_info_size; ++i)
        {
          img_info_list[i].name = NULL;
	  if (verbose)
            printf ("%03d: base 0x%0*" PRIx64 " size 0x%08lx slot 0x%08lx namesize %4ld %c\n",
	            i,
	            hdr.machine == IMAGE_FILE_MACHINE_I386 ? 8 : 12,
	            img_info_list[i].base,
	            img_info_list[i].size,
	            img_info_list[i].slot_size,
	            img_info_list[i].name_size,
	            img_info_list[i].flag.needs_rebasing ? '*' : ' ');
        }
    }

  /* Eventually read the strings. */
  if (ret == 0)
    {
      if (verbose)
	printf ("---- database strings ----\n");

      for (i = 0; i < img_info_size; ++i)
	{
	  img_info_list[i].name = (char *)
				  calloc (img_info_list[i].name_size, sizeof(char));
	  if (!img_info_list[i].name)
	    {
	      fprintf (stderr, "%s: Out of memory.\n", progname);
	      ret = -1;
	      break;
	    }
	  if (read (fd, (void *)img_info_list[i].name,
		    (size_t) img_info_list[i].name_size) < 0)
	    {
	      fprintf (stderr, "%s: failed to read rebase database \"%s\": "
		       "%s\n", progname, db_file, strerror (errno));
	      ret = -1;
	      break;
	    }
	  else if (verbose)
	    {
              printf ("%03d: namesize %4ld (0x%04lx) %s\n", i,
		      img_info_list[i].name_size,
		      img_info_list[i].name_size,
		      img_info_list[i].name);
	    }
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

static struct option long_options[] = {
  {"help",	no_argument,	   NULL, 'h'},
  {"usage",	no_argument,	   NULL, 'h'},
  {"quiet",	no_argument,	   NULL, 'q'},
  {"usage",	no_argument,	   NULL, 'h'},
  {"verbose",	no_argument,	   NULL, 'v'},
  {"version",	no_argument,	   NULL, 'V'},
  {NULL,	no_argument,	   NULL,  0 }
};

static const char *short_options = "hqvV";

void
parse_args (int argc, char *argv[])
{
  int opt = 0;

  while ((opt = getopt_long (argc, argv, short_options, long_options, NULL))
	 != -1)
    {
      switch (opt)
	{
	case 'q':
	  quiet = TRUE;
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

  args_index = optind;
}

void
usage ()
{
  fprintf (stderr,
"usage: %s [-hqvV] dbfile\n"
"       %s --help or --usage for full help text\n",
	   progname, progname);
}

void
help ()
{
  printf ("\
Usage: %s [OPTIONS] [FILE]n\
Dumps the rebase database file in readable format.\n\
  -q, --quiet             Be quiet about non-critical issues.\n\
  -v, --verbose           Print some debug output.\n\
  -V, --version           Print version info and exit.\n\
  -h, --help, --usage     This help.\n",
	  progname);
}

void
version ()
{
  fprintf (stderr, "rebase-dump version %s (imagehelper version %s)\n",
	   VERSION, LIB_VERSION);
  fprintf (stderr, "Copyright (c) 2011 Charles Wilson\n");
}

