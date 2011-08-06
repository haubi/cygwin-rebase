/*
 * Copyright (c) 2009, 2011 Charles Wilson
 * Based on rebase.c by Jason Tishler
 * Significant contributions by Dave Korn
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#if defined (__CYGWIN__) || defined (__MSYS__)
#include <sys/mman.h>
#endif

#include <windows.h>

/* Fix broken definitions in older w32api. */
#ifndef IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
#define IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE
#endif
#ifndef IMAGE_DLLCHARACTERISTICS_NX_COMPAT
#define IMAGE_DLLCHARACTERISTICS_NX_COMPAT IMAGE_DLL_CHARACTERISTICS_NX_COMPAT
#endif
#ifndef IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY
#define IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY
#endif

static WORD coff_characteristics_set;
static WORD coff_characteristics_clr;
static WORD coff_characteristics_show;
static WORD pe_characteristics_set;
static WORD pe_characteristics_clr;
static WORD pe_characteristics_show;

typedef struct {
  long          flag;
  const char *  name;
  size_t        len;
} symbolic_flags_t;

#define CF(flag,name) { flag, #name, sizeof(#name) - 1 }
static const symbolic_flags_t coff_symbolic_flags[] = {
  CF(0x0001, relocs_stripped),
  CF(0x0002, executable_image),
  CF(0x0004, line_nums_stripped),
  CF(0x0008, local_syms_stripped),
  CF(0x0010, wstrim),
  CF(0x0020, bigaddr),
/*CF(0x0040, unspec_0x0040),*/
  CF(0x0080, bytes_reversed_lo),
  CF(0x0100, 32bit_machine),
  CF(0x0200, sepdbg),
  CF(0x0400, removable_run_from_swap),
  CF(0x0800, net_run_from_swap),
  CF(0x1000, system),
  CF(0x2000, dll),
  CF(0x4000, uniprocessor_only),
  CF(0x8000, bytes_reversed_hi),
  {0, 0, 0}
};

static const symbolic_flags_t pe_symbolic_flags[] = {
/*CF(0x0001, reserved_0x0001),*/
/*CF(0x0002, reserved_0x0002),*/
/*CF(0x0004, reserved_0x0004),*/
/*CF(0x0008, reserved_0x0008),*/
/*CF(0x0010, unspec_0x0010),*/
/*CF(0x0020, unspec_0x0020),*/
  CF(0x0040, dynamicbase),
  CF(0x0080, forceinteg),
  CF(0x0100, nxcompat),
  CF(0x0200, no-isolation),
  CF(0x0400, no-seh),
  CF(0x0800, no-bind),
/*CF(0x1000, reserved_0x1000),*/
  CF(0x2000, wdmdriver),
/*CF(0x4000, reserved_0x4000),*/
  CF(0x8000, tsaware),
  {0, 0, 0}
};


static struct option long_options[] = {
  {"dynamicbase",  optional_argument, NULL, 'd'},
  {"forceinteg",   optional_argument, NULL, 'f'},
  {"nxcompat",     optional_argument, NULL, 'n'},
  {"no-isolation", optional_argument, NULL, 'i'},
  {"no-seh",       optional_argument, NULL, 's'},
  {"no-bind",      optional_argument, NULL, 'b'},
  {"wdmdriver",    optional_argument, NULL, 'W'},
  {"tsaware",      optional_argument, NULL, 't'},
  {"wstrim",       optional_argument, NULL, 'w'},
  {"bigaddr",      optional_argument, NULL, 'l'},
  {"sepdbg",       optional_argument, NULL, 'S'},
  {"filelist",     no_argument, NULL, 'T'},
  {"verbose",      no_argument, NULL, 'v'},
  {"help",         no_argument, NULL, 'h'},
  {"version",      no_argument, NULL, 'V'},
  {NULL, no_argument, NULL, 0}
};
static const char *short_options = "d::f::n::i::s::b::W::t::w::l::S::T:vhV";

static void short_usage (FILE *f);
static void help (FILE *f);
static void version (FILE *f);

int do_mark (const char *pathname);
int get_characteristics(const char *pathname,
                        WORD* coff_characteristics,
                        WORD* pe_characteristics);
int set_coff_characteristics(const char *pathname,
                             WORD coff_characteristics);
int set_pe_characteristics(const char *pathname,
                           WORD pe_characteristics);

static void display_flags (const char *field_name, const symbolic_flags_t *syms,
                           WORD show_symbolic, WORD old_flag_value,
			   WORD new_flag_value);
static char *symbolic_flags (const symbolic_flags_t *syms, long show, long value);
static void append_and_decorate (char **str, int is_set, const char *name, int len);
static void *xmalloc (size_t num);
#define XMALLOC(type, num)      ((type *) xmalloc ((num) * sizeof(type)))
static void handle_coff_flag_option (const char *option_name,
                                     const char *option_arg,
                                     WORD   flag_value);
static void handle_pe_flag_option (const char *option_name,
                                   const char *option_arg,
                                   WORD   flag_value);
void parse_args (int argc, char *argv[]);
int string_to_bool  (const char *string, int *value);
int string_to_ulong (const char *string, unsigned long *value);
FILE *file_list_fopen (const char *file_list);
char *file_list_fgets (char *buf, int size, FILE *file);
int file_list_fclose (FILE *file);


int args_index = 0;
int verbose = 0;
const char *file_list = 0;
const char *stdin_file_list = "-";
int mark_any = 0;

int
main (int argc, char *argv[])
{
  int files_attempted = 0;
  int i = 0;
  int ret = 0;

  parse_args (argc, argv);

  /* Operate on files in file list, if specified. */
  if (file_list)
    {
      int status = 0, ret = 0;
      char filename[MAX_PATH + 2];
      FILE *file = file_list_fopen (file_list);

      if (!file)
	exit (2);

      while (file_list_fgets (filename, MAX_PATH + 2, file))
	{
          files_attempted++;
	  if ((status = do_mark (filename)) != 0)
	    ret = 2;
	}

      file_list_fclose (file);

      if (files_attempted == 0)
        {
          /* warn the user */
          fprintf (stderr,
             "Error: Could not find any filenames in %s\n",
             (strcmp(file_list, stdin_file_list) == 0 ? "<stdin>" : file_list));
        }
    }

  /* Operate on files listed as command line arguments.
   * Don't reset files_attempted because we do not require
   * any args if -T filelist
   */
  for (i = args_index; i < argc; i++)
    {
      const char *filename = argv[i];
      files_attempted++;
      if (do_mark (filename) != 0)
	ret = 2;
    }

  if (files_attempted == 0)
    {
      /* warn the user */
      fprintf (stderr, "Error: no files to process\n");
      short_usage (stderr);
      exit (2);
    }

  return ret;
}

int
do_mark (const char *pathname)
{
  int has_relocs;
  int is_executable;
  int is_dll;
  WORD old_coff_characteristics;
  WORD new_coff_characteristics;
  WORD old_pe_characteristics;
  WORD new_pe_characteristics;

  /* Skip if file does not exist */
  if (access (pathname, F_OK) == -1)
    {
      fprintf (stderr, "%s: skipped because nonexistent\n", pathname);
      return 0;
    }

  if (mark_any)
    {
      /* Skip if not writable. */
      if (access (pathname, W_OK) == -1)
        {
          fprintf (stderr, "%s: skipped because not writable\n", pathname);
          return 0;
        }
    }

  if (get_characteristics (pathname,
                           &old_coff_characteristics,
                           &old_pe_characteristics) != 0)
    {
      fprintf (stderr,
               "%s: skipped because could not read file characteristics\n",
               pathname);
      return 0;
    }
  new_coff_characteristics = old_coff_characteristics;
  new_coff_characteristics |= coff_characteristics_set;
  new_coff_characteristics &= ~coff_characteristics_clr;

  new_pe_characteristics = old_pe_characteristics;
  new_pe_characteristics |= pe_characteristics_set;
  new_pe_characteristics &= ~pe_characteristics_clr;

  is_executable = ((new_coff_characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) > 0);
  is_dll        = ((new_coff_characteristics & IMAGE_FILE_DLL) > 0);
  has_relocs    = ((new_coff_characteristics & IMAGE_FILE_RELOCS_STRIPPED) == 0);

  /* validation and warnings about things that are
     problematic, but that we are not explicitly
     changing */
  if (!has_relocs)
    {
      if (verbose
         && (new_pe_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
         && (old_pe_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE))
        {
          fprintf (stderr,
                   "Warning: file has no relocation info but has dynbase set (%s).\n",
                   pathname);
        }
    }
  if (!is_executable || is_dll)
    {
      if (verbose
         && (new_pe_characteristics & IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE)
         && (old_pe_characteristics & IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE))
        {
          fprintf (stderr,
                   "Warning: file is non-executable but has tsaware set (%s).\n",
                   pathname);
        }
    }
 
  if (mark_any)
    {
      /* validation and warnings about things we are changing */
      if (!has_relocs)
        {
          if (   (new_pe_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
             && !(old_pe_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE))
            {
              fprintf (stderr,
                       "Warning: setting dynbase on file with no relocation info (%s).\n",
                       pathname);
            }
        } 

      if (!is_executable || is_dll)
        {
          if (   (new_pe_characteristics & IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE)
             && !(old_pe_characteristics & IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE))
            {
              fprintf (stderr,
                       "Warning: setting tsaware on non-executable (%s).\n",
                       pathname);
            }
        }

      /* setting */
      if (new_coff_characteristics != old_coff_characteristics)
        if (set_coff_characteristics (pathname,new_coff_characteristics) != 0)
          {
            fprintf (stderr,
                     "Error: could not update coff characteristics (%s): %s\n",
                      pathname, strerror (errno));
            return 1;
          }
      if (new_pe_characteristics != old_pe_characteristics)
        if (set_pe_characteristics (pathname,new_pe_characteristics) != 0)
          {
            fprintf (stderr,
                     "Error: could not update pe characteristics (%s): %s\n",
                      pathname, strerror (errno));
            return 1;
          }
    }

  /* Display characteristics. */
  if (verbose
      || !mark_any
      || coff_characteristics_show
      || pe_characteristics_show)
    {
      printf ("%s: ", pathname);
      display_flags ("coff", coff_symbolic_flags,
                     coff_characteristics_show ?:
                     verbose ? old_coff_characteristics : 0,
                     old_coff_characteristics,
                     new_coff_characteristics);
      display_flags ("pe", pe_symbolic_flags,
                     pe_characteristics_show ?:
                     verbose ? old_pe_characteristics : 0,
                     old_pe_characteristics,
                     new_pe_characteristics);
      puts ("");
    }

  return 0;
}

static void
display_flags (const char *field_name, const symbolic_flags_t *syms,
               WORD show_symbolic, WORD old_flag_value,
	       WORD new_flag_value)
{
  if (show_symbolic)
    {
      if (old_flag_value != new_flag_value)
        {
          char * old_symbolic = symbolic_flags (syms, show_symbolic, old_flag_value);
          char * new_symbolic = symbolic_flags (syms, show_symbolic, new_flag_value);
          printf ("%s(0x%04x%s==>0x%04x%s) ", field_name,
              old_flag_value, old_symbolic,
              new_flag_value, new_symbolic);
          free (old_symbolic);
          free (new_symbolic);
        }
      else
        {
          char * old_symbolic = symbolic_flags (syms, show_symbolic, old_flag_value);
          printf ("%s(0x%04x%s) ", field_name,
              old_flag_value, old_symbolic);
          free (old_symbolic);
        }
    }
  else
    {
      if (old_flag_value != new_flag_value)
        printf ("%s(0x%04x==>0x%04x) ", field_name,
            old_flag_value, new_flag_value);
      else
        printf ("%s(0x%04x) ", field_name, old_flag_value);
    }
}

static char *
symbolic_flags (const symbolic_flags_t *syms, long show, long value)
{
  int i;
  char * rVal = NULL;
  for (i = 0; syms[i].name; i++)
    {
      if (syms[i].flag & show)
        {
          append_and_decorate (&rVal,
            (value & syms[i].flag),
            syms[i].name,
            syms[i].len);
        }
    }
  if (rVal)
    {
      size_t len = strlen (rVal);
      char *tmp = XMALLOC (char, len + 3);
      *tmp = '[';
      memcpy (tmp+1, rVal, len);
      tmp[len+1] = ']';
      tmp[len+2] = '\0';
      free (rVal);
      rVal = tmp;
    }
  return rVal;
}

static void
append_and_decorate (char **str, int is_set, const char *name, int len)
{
  char *tmp;
  int slen;
  if (!*str)
    {
      *str = XMALLOC (char, len + 2);
      (*str)[0] = (is_set ? '+' : '-');
      memcpy ((*str)+1, name, len);
      (*str)[len+1] = '\0';
      return; 
    }
  else
    { 
      slen = strlen (*str);
      tmp = XMALLOC (char, slen + 2 + len + 1);
      memcpy (tmp, *str, slen);
      free (*str);
      *str = tmp;
      tmp = *str + slen;
      *tmp++ = ',';
      *tmp++ = (is_set ? '+' : '-');
      memcpy (tmp, name, len);
      tmp[len] = '\0';
    } 
}

static void *
xmalloc (size_t num)
{
  void *p = (void *) malloc (num);
  if (!p)
    {
      fputs ("Memory exhausted", stderr);
      exit (2);
    }
  return p;
}

static void
handle_pe_flag_option (const char *option_name,
                       const char *option_arg,
                       WORD   flag_value)
{
  int bool_value;
  if (!option_arg)
    {
      pe_characteristics_show |= flag_value;
    }
  else
    {
      if (string_to_bool (option_arg, &bool_value) != 0)
        {
          fprintf (stderr, "Invalid argument for %s: %s\n", 
                   option_name, option_arg);
          short_usage (stderr);
          exit (1);
        }
      if (bool_value)
        pe_characteristics_set |= flag_value;
      else
        pe_characteristics_clr |= flag_value;
    }
}

static void
handle_coff_flag_option (const char *option_name,
                         const char *option_arg,
                         WORD   flag_value)
{
  int bool_value;
  if (!option_arg)
    {
      coff_characteristics_show |= flag_value;
    }
  else
    {
      if (string_to_bool (option_arg, &bool_value) != 0)
        {
          fprintf (stderr, "Invalid argument for %s: %s\n", 
                   option_name, option_arg);
          short_usage (stderr);
          exit (1);
        }
      if (bool_value)
        coff_characteristics_set |= flag_value;
      else
        coff_characteristics_clr |= flag_value;
    }
}

void
parse_args (int argc, char *argv[])
{
  int c;

  while (1)
    {
      int option_index = 0;
      c = getopt_long (argc, argv, short_options, long_options, &option_index);
      if (c == -1)
        break;

      switch (c)
	{
	case 'h':
	  help (stdout);
          exit (0);
          break;
	case 'V':
	  version (stdout);
	  exit (0);
	  break;

	case 'd':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE);
	  break;
	case 'n':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_NX_COMPAT);
	  break;
	case 't':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE);
	  break;
	case 'f':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY);
	  break;
	case 'i':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_NO_ISOLATION);
	  break;
	case 's':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_NO_SEH);
	  break;
	case 'b':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_NO_BIND);
	  break;
	case 'W':
	  handle_pe_flag_option (long_options[option_index].name,
	                         optarg,
	                         IMAGE_DLLCHARACTERISTICS_WDM_DRIVER);
	  break;
	case 'w':
	  handle_coff_flag_option (long_options[option_index].name,
	                           optarg,
	                           IMAGE_FILE_AGGRESIVE_WS_TRIM);
	  break;
	case 'l':
	  handle_coff_flag_option (long_options[option_index].name,
	                           optarg,
	                           IMAGE_FILE_LARGE_ADDRESS_AWARE);
	  break;
	case 'S':
	  handle_coff_flag_option (long_options[option_index].name,
	                           optarg,
	                           IMAGE_FILE_DEBUG_STRIPPED);
	  break;
	case 'T':
	  file_list = optarg;
	  break;
	case 'v':
	  verbose = TRUE;
	  break;
        case '?':
          break;
	default:
	  short_usage (stderr);
	  exit (1);
	  break;
	}
    }

  args_index = optind;
  mark_any =   pe_characteristics_set
             | pe_characteristics_clr
             | coff_characteristics_set
             | coff_characteristics_clr;
}

int
string_to_bool (const char *string, int *value)
{
  unsigned long number = 0;
  if (!string || !*string)
    return 1;

  if (string_to_ulong (string, &number) != 0)
    {
      size_t len = strlen (string);
      if ( (len == 4 && strcasecmp (string, "true") == 0)
         ||(len == 3 && strcasecmp (string, "yes") == 0) 
         ||(len == 1 && strcasecmp (string, "t") == 0)
         ||(len == 1 && strcasecmp (string, "y") == 0)) 
        {
          *value = TRUE;
        }
      else if ( (len == 5 && strcasecmp (string, "false") == 0)
         ||(len == 2 && strcasecmp (string, "no") == 0) 
         ||(len == 1 && strcasecmp (string, "f") == 0) 
         ||(len == 1 && strcasecmp (string, "n") == 0))
        {
          *value = FALSE;
        }
      else
        {
          return 1;
        }
    }
  else
    {
      *value = (number != 0);
    }
  return 0; 
}

int
string_to_ulong (const char *string, unsigned long *value)
{
  unsigned long number = 0;
  char * endp;
  errno = 0;

  /* null or empty input */
  if (!string || !*string)
    return 1;

  number = strtoul (string, &endp, 0);

  /* out of range */
  if (ERANGE == errno)
    return 1;

  /* no valid numeric input */
  if (endp == string)
    return 1;

  /* non-numeric trailing characters */
  if (*endp != '\0')
    return 1;

  *value = number;
  return 0;
}

#if !defined (__CYGWIN__) && !defined (__MSYS__)
/* Minimal mmap on files for Win32 */
#define PROT_READ 0
#define PROT_WRITE 1
#define MAP_SHARED 0
#define MAP_FAILED NULL

void *
mmap (void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  HANDLE fh = (HANDLE) _get_osfhandle (fd);
  HANDLE h = CreateFileMapping (fh, NULL, prot ? PAGE_READWRITE : PAGE_READONLY,
				0, 0, NULL);
  if (!h)
    return MAP_FAILED;
  addr = MapViewOfFileEx (h, prot ? FILE_MAP_WRITE : FILE_MAP_READ,
			  0, 0, 4096, addr);
  CloseHandle (h);
  return addr;
}

int
munmap (void *addr, size_t len)
{
  UnmapViewOfFile (addr);
  return 0;
}
#endif

typedef struct
{
  PIMAGE_DOS_HEADER dosheader;
  union
    {
      PIMAGE_NT_HEADERS32 ntheader32;
      PIMAGE_NT_HEADERS64 ntheader64;
    };
  BOOL is_64bit;
} pe_file;

pe_file *
pe_open (const char *path, BOOL writing)
{
  /* Non-threaded application. */
  static pe_file pef;
  int fd;
  void *map;
  
  fd = open (path, O_BINARY | (writing ? O_RDWR : O_RDONLY));
  if (fd == -1)
    return NULL;
  map = mmap (NULL, 4096, PROT_READ | (writing ? PROT_WRITE : 0), MAP_SHARED,
	      fd, 0);
  if (map == MAP_FAILED)
    {
      close (fd);
      return NULL;
    }
  pef.dosheader = (PIMAGE_DOS_HEADER) map;
  pef.ntheader32 = (PIMAGE_NT_HEADERS32)
		   ((PBYTE) pef.dosheader + pef.dosheader->e_lfanew);
  /* Sanity checks */
  if (pef.dosheader->e_magic != 0x5a4d	/* "MZ" */
      || (PBYTE) pef.ntheader32 - (PBYTE) map + sizeof *pef.ntheader32 >= 4096
      || pef.ntheader32->Signature != 0x00004550)
    {
      munmap (map, 4096);
      close (fd);
      return NULL;
    }
  pef.is_64bit = pef.ntheader32->OptionalHeader.Magic
		 == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  return &pef;
}

void
pe_close (pe_file *pep)
{
  if (pep)
    munmap ((void *) pep->dosheader, 4096);
}

int
get_characteristics(const char *pathname,
                    WORD* coff_characteristics,
                    WORD* pe_characteristics)
{
  pe_file *pep = pe_open (pathname, FALSE);
  if (!pep)
    return 1;
  if (pep->is_64bit)
    {
      *coff_characteristics = pep->ntheader64->FileHeader.Characteristics;
      *pe_characteristics = pep->ntheader64->OptionalHeader.DllCharacteristics;
    }
  else
    {
      *coff_characteristics = pep->ntheader32->FileHeader.Characteristics;
      *pe_characteristics = pep->ntheader32->OptionalHeader.DllCharacteristics;
    }
  pe_close (pep);
  return 0;
}

int
set_coff_characteristics(const char *pathname,
                         WORD coff_characteristics)
{
  pe_file *pep = pe_open (pathname, TRUE);
  if (!pep)
    return 1;
  if (pep->is_64bit)
    pep->ntheader64->FileHeader.Characteristics = coff_characteristics;
  else
    pep->ntheader32->FileHeader.Characteristics = coff_characteristics;
  pe_close (pep);
  return 0;
}

int
set_pe_characteristics(const char *pathname,
                       WORD pe_characteristics)
{
  pe_file *pep = pe_open (pathname, TRUE);
  if (!pep)
    return 1;
  if (pep->is_64bit)
    pep->ntheader64->OptionalHeader.DllCharacteristics = pe_characteristics;
  else
    pep->ntheader32->OptionalHeader.DllCharacteristics = pe_characteristics;
  pe_close (pep);
  return 0;
}

FILE *
file_list_fopen (const char *file_list)
{
  FILE *file = stdin;
  if (strcmp(file_list, stdin_file_list) != 0)
    {
      file = fopen (file_list, "r");
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

static void
short_usage (FILE *f)
{
  fputs ("Usage: peflags [OPTIONS] file(s)...\n", f);
  fputs ("Sets or clears various flags in PE files (that is, exes and dlls)\n", f);
  fputs ("Use --help for full help text\n", f);
}

static void
help (FILE *f)
{
  fputs ("Usage: peflags [OPTIONS] file(s)...\n", f);
  fputs ("Sets, clears, or displays various flags in PE files (that is,\n", f);
  fputs ("exes and dlls).  For each flag, if an argument is given, then\n", f);
  fputs ("the specified flag will be set or cleared; if no argument is\n", f);
  fputs ("given, then the current value of that flag will be displayed.\n", f);
  fputs ("\n", f);
  fputs ("  -d, --dynamicbase  [BOOL]   Image base address may be relocated using\n", f);
  fputs ("                              address space layout randomization (ASLR)\n", f);
  fputs ("  -f, --forceinteg   [BOOL]   Code integrity checks are enforced\n", f);
  fputs ("  -n, --nxcompat     [BOOL]   Image is compatible with data execution\n", f);
  fputs ("                              prevention\n", f);
  fputs ("  -i, --no-isolation [BOOL]   Image understands isolation but do not\n", f);
  fputs ("                              isolate the image\n", f);
  fputs ("  -s, --no-seh       [BOOL]   Image does not use SEH. No SE handler may\n", f);
  fputs ("                              be called in this image\n", f);
  fputs ("  -b, --no-bind      [BOOL]   Do not bind this image\n", f);
  fputs ("  -W, --wdmdriver    [BOOL]   Driver uses the WDM model\n", f);
  fputs ("  -t, --tsaware      [BOOL]   Image is Terminal Server aware\n", f);
  fputs ("  -w, --wstrim       [BOOL]   Aggressively trim the working set.\n", f);
  fputs ("  -l, --bigaddr      [BOOL]   The application can handle addresses\n", f);
  fputs ("                              larger than 2 GB\n", f);
  fputs ("  -S, --sepdbg       [BOOL]   Debugging information was removed and\n", f);
  fputs ("                              stored separately in another file.\n", f);
  fputs ("  -T, --filelist FILE         Indicate that FILE contains a list\n", f);
  fputs ("                              of PE files to process\n", f);
  fputs ("  -v, --verbose               Display diagnostic information\n", f);
  fputs ("  -V, --version               Display version information\n", f);
  fputs ("  -h, --help                  Display this help\n", f);
  fputs ("\n", f);
  fputs ("BOOL: may be 1, true, or yes - indicates that the flag should be set\n", f);
  fputs ("          if 0, false, or no - indicates that the flag should be cleared\n", f);
  fputs ("          if not present, then display symbolicly the value of the flag\n", f);
  fputs ("Valid forms for short options: -d, -d0, -d1, -dfalse, etc\n", f);
  fputs ("Valid forms for long options :  --tsaware, --tsaware=true, etc\n", f);
  fputs ("To set a value, and display the results symbolic, repeat the option:\n", f);
  fputs ("  --tsaware=true --tsaware -d0 -d\n", f);
  fputs ("\n", f);
}

static void
version (FILE *f)
{
  fprintf (f, "peflags version %s\n", VERSION);
  fprintf (f, "Copyright (c) 2009, 2011  Charles Wilson, Dave Korn, Jason Tishler, et al.\n");
}

