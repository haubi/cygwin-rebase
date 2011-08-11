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
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#if defined (__CYGWIN__) || defined (__MSYS__)
#include <sys/mman.h>
#endif
#if defined(__MSYS__)
/* MSYS has no inttypes.h */
# define PRIu64 "llu"
# define PRIx64 "llx"
#else
# include <inttypes.h>
#endif

#include <windows.h>

#if defined(__MSYS__)
/* MSYS has no strtoull */
unsigned long long strtoull(const char *, char **, int);
#endif

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

typedef struct
{
  const char *pathname;
  PIMAGE_DOS_HEADER dosheader;
  union
    {
      PIMAGE_NT_HEADERS32 ntheader32;
      PIMAGE_NT_HEADERS64 ntheader64;
    };
  BOOL is_64bit;
} pe_file;

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

enum {
  SIZEOF_STACK_RESERVE = 0,
  SIZEOF_STACK_COMMIT,
  SIZEOF_HEAP_RESERVE,
  SIZEOF_HEAP_COMMIT,
  SIZEOF_CYGWIN_HEAP,
  NUM_SIZEOF_VALUES		/* Keep at the end */
};

typedef enum {
  DONT_HANDLE = 0,
  DO_READ = 1,
  DO_WRITE = 2
} do_handle_t;

static do_handle_t handle_any_sizeof;

typedef struct {
  ULONGLONG  value;
  const char *name;
  const char *unit;
  do_handle_t handle;
  BOOL        is_ulong;		/* Only for 64 bit files. */
  ULONG       offset64;
  ULONG       offset32;
} sizeof_values_t;

sizeof_values_t sizeof_vals[5] = {
  { 0, "stack reserve size"      , "bytes", 0, FALSE,
    offsetof (IMAGE_NT_HEADERS64, OptionalHeader.SizeOfStackReserve),
    offsetof (IMAGE_NT_HEADERS32, OptionalHeader.SizeOfStackReserve),
  },
  { 0, "stack commit size"       , "bytes", 0, FALSE,
    offsetof (IMAGE_NT_HEADERS64, OptionalHeader.SizeOfStackCommit),
    offsetof (IMAGE_NT_HEADERS32, OptionalHeader.SizeOfStackCommit),
  },
  { 0, "Win32 heap reserve size" , "bytes", 0, FALSE,
    offsetof (IMAGE_NT_HEADERS64, OptionalHeader.SizeOfHeapReserve),
    offsetof (IMAGE_NT_HEADERS32, OptionalHeader.SizeOfHeapReserve),
  },
  { 0, "Win32 heap commit size"  , "bytes", 0, FALSE,
    offsetof (IMAGE_NT_HEADERS64, OptionalHeader.SizeOfHeapCommit),
    offsetof (IMAGE_NT_HEADERS32, OptionalHeader.SizeOfHeapCommit),
  },
  { 0, "initial Cygwin heap size", "MB", 0, TRUE,
    offsetof (IMAGE_NT_HEADERS64, OptionalHeader.LoaderFlags),
    offsetof (IMAGE_NT_HEADERS32, OptionalHeader.LoaderFlags),
  }
};

#define pulonglong(struct, offset)	(PULONGLONG)((PBYTE)(struct)+(offset))
#define pulong(struct, offset)		(PULONG)((PBYTE)(struct)+(offset))

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
  {"stack-reserve",optional_argument, NULL, 'x'},
  {"stack-commit", optional_argument, NULL, 'X'},
  {"heap-reserve", optional_argument, NULL, 'y'},
  {"heap-commit",  optional_argument, NULL, 'Y'},
  {"cygwin-heap",  optional_argument, NULL, 'z'},
  {"filelist",     no_argument, NULL, 'T'},
  {"verbose",      no_argument, NULL, 'v'},
  {"help",         no_argument, NULL, 'h'},
  {"version",      no_argument, NULL, 'V'},
  {NULL, no_argument, NULL, 0}
};
static const char *short_options
	= "d::f::n::i::s::b::W::t::w::l::S::x::X::y::Y::z::T:vhV";

static void short_usage (FILE *f);
static void help (FILE *f);
static void version (FILE *f);

int do_mark (const char *pathname);
pe_file *pe_open (const char *path, BOOL writing);
void pe_close (pe_file *pep);
void get_and_set_sizes(const pe_file *pep);
int get_characteristics(const pe_file *pep,
                        WORD* coff_characteristics,
                        WORD* pe_characteristics);
int set_coff_characteristics(const pe_file *pep,
                             WORD coff_characteristics);
int set_pe_characteristics(const pe_file *pep,
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
static void handle_num_option (const char *option_name,
			       const char *option_arg,
			       int option_index);
void parse_args (int argc, char *argv[]);
int string_to_bool  (const char *string, int *value);
int string_to_ulonglong (const char *string, unsigned long long *value);
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

  pe_file *pep = pe_open (pathname, mark_any != 0
				    || handle_any_sizeof == DO_WRITE);
  if (!pep)
    {
      fprintf (stderr,
               "%s: skipped because could not open\n",
               pathname);
      return 0;
    }

  get_characteristics (pep,
		       &old_coff_characteristics,
		       &old_pe_characteristics);
  get_and_set_sizes (pep);

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
        set_coff_characteristics (pep, new_coff_characteristics);
      if (new_pe_characteristics != old_pe_characteristics)
        set_pe_characteristics (pep, new_pe_characteristics);
    }

  /* Display characteristics. */
  if (verbose
      || !mark_any
      || coff_characteristics_show
      || pe_characteristics_show
      || handle_any_sizeof != DONT_HANDLE)
    {
      BOOL printed_characteristic = FALSE;
      int i;

      printf ("%s: ", pathname);
      if (verbose
	  || (!mark_any && handle_any_sizeof == DONT_HANDLE)
	  || coff_characteristics_show || pe_characteristics_show)
	{
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
	  printed_characteristic = TRUE;
	}

      for (i = 0; i < NUM_SIZEOF_VALUES; ++i)
	{
	  if (sizeof_vals[i].handle != DONT_HANDLE)
	    {
	      printf ("%*s%-24s: %" PRIu64 " (0x%" PRIx64 ") %s\n",
		      printed_characteristic ? (int) strlen (pathname) + 2
					     : 0, "",
		      sizeof_vals[i].name,
		      sizeof_vals[i].value,
		      sizeof_vals[i].value,
		      sizeof_vals[i].unit);
	      printed_characteristic = TRUE;
	    }
	}
    }

  pe_close (pep);

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
handle_num_option (const char *option_name,
		   const char *option_arg,
		   int option_index)
{
  if (!option_arg)
    {
      if (sizeof_vals[option_index].handle == DONT_HANDLE)
	sizeof_vals[option_index].handle = DO_READ;
      if (handle_any_sizeof == DONT_HANDLE)
	handle_any_sizeof = DO_READ;
    }
  else if (string_to_ulonglong (option_arg, &sizeof_vals[option_index].value)
	   /* 48 bit address space */
	   || sizeof_vals[option_index].value > 0x0000ffffffffffffULL
	   /* Just a ULONG value */
	   || (sizeof_vals[option_index].is_ulong
	       && sizeof_vals[option_index].value > ULONG_MAX))
    {
      fprintf (stderr, "Invalid argument for %s: %s\n", 
	       option_name, option_arg);
      short_usage (stderr);
      exit (1);
    }
  else
    {
      sizeof_vals[option_index].handle = DO_WRITE;
      handle_any_sizeof = DO_WRITE;
    }
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
      /* Workaround the problem that option_index is not valid if the user
	 specified a short option. */
      if (option_index == 0 && c != long_options[0].val)
	{
	  for (option_index = 1;
	       long_options[option_index].name;
	       ++option_index)
	    if (long_options[option_index].val == c)
	      break;
	}

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
	case 'x':
	  handle_num_option (long_options[option_index].name,
			     optarg,
			     SIZEOF_STACK_RESERVE);
	  break;
	case 'X':
	  handle_num_option (long_options[option_index].name,
			     optarg,
			     SIZEOF_STACK_COMMIT);
	  break;
	case 'y':
	  handle_num_option (long_options[option_index].name,
			     optarg,
			     SIZEOF_HEAP_RESERVE);
	  break;
	case 'Y':
	  handle_num_option (long_options[option_index].name,
			     optarg,
			     SIZEOF_HEAP_COMMIT);
	  break;
	case 'z':
	  handle_num_option (long_options[option_index].name,
			     optarg,
			     SIZEOF_CYGWIN_HEAP);
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
  unsigned long long number = 0;
  if (!string || !*string)
    return 1;

  if (string_to_ulonglong (string, &number) != 0)
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
string_to_ulonglong (const char *string, unsigned long long *value)
{
  unsigned long long number = 0;
  char * endp;
  errno = 0;

  /* null or empty input */
  if (!string || !*string)
    return 1;

  number = strtoull (string, &endp, 0);

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
  pef.pathname = path;
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

void
get_and_set_size (const pe_file *pep, sizeof_values_t *val)
{
  if (val->handle == DO_READ)
    {
      if (!pep->is_64bit)
	val->value = *pulong (pep->ntheader32, val->offset32);
      else if (val->is_ulong)
	val->value = *pulong (pep->ntheader64, val->offset64);
      else
	val->value = *pulonglong (pep->ntheader64, val->offset64);
    }
  else if (val->handle == DO_WRITE)
    {
      if ((!pep->is_64bit || val->is_ulong) && val->value >= ULONG_MAX)
	{
	  fprintf (stderr, "%s: Skip writing %s, value too big\n",
		   pep->pathname, val->name);
	  val->handle = DONT_HANDLE;
	}
      else if (!pep->is_64bit)
	*pulong (pep->ntheader32, val->offset32) = val->value;
      else if (val->is_ulong)
	*pulong (pep->ntheader64, val->offset64) = val->value;
      else
	*pulonglong (pep->ntheader64, val->offset64) = val->value;
    }
}

void
get_and_set_sizes (const pe_file *pep)
{
  int i;

  for (i = 0; i < NUM_SIZEOF_VALUES; ++i)
    get_and_set_size (pep, sizeof_vals + i);
}

int
get_characteristics(const pe_file *pep,
                    WORD* coff_characteristics,
                    WORD* pe_characteristics)
{
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
  return 0;
}

int
set_coff_characteristics(const pe_file *pep,
                         WORD coff_characteristics)
{
  if (pep->is_64bit)
    pep->ntheader64->FileHeader.Characteristics = coff_characteristics;
  else
    pep->ntheader32->FileHeader.Characteristics = coff_characteristics;
  return 0;
}

int
set_pe_characteristics(const pe_file *pep,
                       WORD pe_characteristics)
{
  if (pep->is_64bit)
    pep->ntheader64->OptionalHeader.DllCharacteristics = pe_characteristics;
  else
    pep->ntheader32->OptionalHeader.DllCharacteristics = pe_characteristics;
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
  fputs (
"Usage: peflags [OPTIONS] file(s)...\n"
"Sets, clears, or displays various flags in PE files (that is, exes and dlls).\n"
"Also sets or displays various numerical values which influence executable\n"
"startup.  For each flag, if an argument is given, then the specified flag\n"
"will be set or cleared; if no argument is given, then the current value of\n"
"that flag will be displayed.  For each numerical value, if an argument is\n"
"given, the specified value will be overwritten; if no argument is given, the\n"
"numerical value will be displayed in decimal and hexadecimal notation.\n" 
"\n"
"  -d, --dynamicbase  [BOOL]   Image base address may be relocated using\n"
"                              address space layout randomization (ASLR).\n"
"  -f, --forceinteg   [BOOL]   Code integrity checks are enforced.\n"
"  -n, --nxcompat     [BOOL]   Image is compatible with data execution\n"
"                              prevention (DEP).\n"
"  -i, --no-isolation [BOOL]   Image understands isolation but do not isolate\n"
"                              the image.\n"
"  -s, --no-seh       [BOOL]   Image does not use structured exception handling\n"
"                              (SEH). No SE handler may be called in this image.\n"
"  -b, --no-bind      [BOOL]   Do not bind this image.\n"
"  -W, --wdmdriver    [BOOL]   Driver uses the WDM model.\n"
"  -t, --tsaware      [BOOL]   Image is Terminal Server aware.\n"
"  -w, --wstrim       [BOOL]   Aggressively trim the working set.\n"
"  -l, --bigaddr      [BOOL]   The application can handle addresses larger\n"
"                              than 2 GB.\n"
"  -S, --sepdbg       [BOOL]   Debugging information was removed and stored\n"
"                              separately in another file.\n"
"  -x, --stack-reserve [NUM]   Reserved stack size of the process in bytes.\n"
"  -X, --stack-commit  [NUM]   Initial commited portion of the process stack\n"
"                              in bytes.\n"
"  -y, --heap-reserve  [NUM]   Reserved heap size of the default application\n"
"                              heap in bytes.  Note that this value has no\n"
"                              significant meaning to Cygwin applications.\n"
"                              See the -z, --cygwin-heap option instead.\n"
"  -Y, --heap-commit   [NUM]   Initial commited portion of the default\n"
"                              application heap in bytes.  Note that this value\n"
"                              has no significant meaning to Cygwin applications.\n"
"                              See the -z, --cygwin-heap option instead.\n"
"  -z, --cygwin-heap   [NUM]   Initial reserved heap size of the Cygwin\n"
"                              application heap in Megabytes.  This value is\n"
"                              only evaluated starting with Cygwin 1.7.10.\n"
"                              Useful values are between 4 and 2048.  If 0,\n"
"                              Cygwin uses the default heap size of 384 Megs.\n"
"                              Has no meaning for non-Cygwin applications.\n"
"  -T, --filelist FILE         Indicate that FILE contains a list\n"
"                              of PE files to process\n"
"  -v, --verbose               Display diagnostic information\n"
"  -V, --version               Display version information\n"
"  -h, --help                  Display this help\n"
"\n"
"BOOL: may be 1, true, or yes - indicates that the flag should be set\n"
"          if 0, false, or no - indicates that the flag should be cleared\n"
"          if not present, then display symbolicly the value of the flag\n"
"NUM : may be any 32 bit value for 32 bit executables, any 48 bit value for\n"
"      64 bit executables.\n"
"Valid forms for short options: -d, -d0, -d1, -dfalse, etc\n"
"                               -z, -z0, -z1024, -z0x400, etc\n"
"Valid forms for long options :  --tsaware, --tsaware=true, etc\n"
"                                --cygwin-heap, --cygwin-heap=512, etc\n"
"For flag values, to set a value, and display the results symbolic, repeat the\n"
"option:  --tsaware=true --tsaware -d0 -d\n"
"\n", f);
}

static void
version (FILE *f)
{
  fprintf (f, "peflags version %s\n", VERSION);
  fprintf (f, "Copyright (c) 2009, 2011  Charles Wilson, Dave Korn, Jason Tishler, et al.\n");
}

