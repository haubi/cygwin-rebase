/*
 * Copyright (c) 2001 Jason Tishler
 *         2002 Ralf Habacker 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id$
 */

#include <iostream>
#include <sstream>
#include <string>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>
#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif
#include <windows.h>

#include "imagehelper.h"

using namespace std;

#ifdef __CYGWIN__
string PosixToWin32(const string& aPosixPath);
#else
#define PosixToWin32(aPosixPath) aPosixPath
#endif

void ParseArgs(int argc, char* argv[]);
unsigned long StringToUlong(const string& aString);
void Usage();

ULONG64 theImageBase = 0;
BOOL theDownFlag = FALSE;
bool theDebugFlag = false;
BOOL theCheckFlag = FALSE;
BOOL theFixFlag = FALSE;
ULONG theOffset = 0;
int theArgsIndex = 0;
int theListFlag = 0;


int
main(int argc, char* argv[])
{
  ParseArgs(argc, argv);
  ULONG64 aNewImageBase = theImageBase;

  for (int i = theArgsIndex; i < argc; i++)
    {
      string aFile = PosixToWin32(argv[i]);
      if (theListFlag)
        {
          ULONG64 ImageBase;
          ULONG ImageSize;
          GetImageInfos64(const_cast<LPSTR>(aFile.c_str()),&ImageBase,&ImageSize);
          cout << aFile << ": " << "ImageBase: 0x" << hex << ImageBase << " ImageSize: 0x" << hex << ImageSize << endl;
        }
      else if (theCheckFlag)
        {
          CheckImage(const_cast<char*>(aFile.c_str()));
        }
      else if (theFixFlag)
        {
          FixImage(const_cast<char*>(aFile.c_str()));
        }
      else
        {
          if (theDownFlag)
            aNewImageBase -= theOffset;

          ULONG anOldImageSize, aNewImageSize;
          ULONG64 anOldImageBase;
          ULONG64 aPrevNewImageBase = aNewImageBase;
          ReBaseImage64(const_cast<char*>(aFile.c_str()), // CurrentImageName
			0, // SymbolPath
			TRUE, // fReBase
			FALSE, // fRebaseSysfileOk
			theDownFlag, // fGoingDown
			0, // CheckImageSize
			&anOldImageSize, // OldImageSize
			&anOldImageBase, // OldImageBase
			&aNewImageSize, // NewImageSize
			&aNewImageBase, // NewImageBase
			time(0)); // TimeStamp

          // ReBaseImage seems to never returns false!
          DWORD aStatus2 = GetLastError();
          if (aStatus2 != 0)
            {
              cerr << "ReBaseImage(" << aFile.c_str() <<"," << hex << aNewImageBase <<") failed with last error = " <<
              dec << aStatus2 << ": " << strerror(aStatus2) << endl;
              exit(2);
            }
          cout << aFile << hex << ": new base = " <<
          ((theDownFlag) ? aNewImageBase : aPrevNewImageBase) <<
          ", new size = " << aNewImageSize + theOffset << endl;

          if (!theDownFlag)
            aNewImageBase += theOffset;
        }
    }


  exit(0);
}
#if defined(__CYGWIN__) || defined(__MSYS__)
string
PosixToWin32(const string& aPosixPath)
{
#if defined(HAVE_DECL_CYGWIN_CONV_PATH) && HAVE_DECL_CYGWIN_CONV_PATH
  string rVal;
  char * aWin32Path =
      (char *)cygwin_create_path(CCP_POSIX_TO_WIN_A, aPosixPath.c_str());
  if (aWin32Path)
  {
	rVal = string(aWin32Path);
    free(aWin32Path);
  }
  return rVal;
#else
  char aWin32Path[MAX_PATH];
  cygwin_conv_to_win32_path(aPosixPath.c_str(), aWin32Path);
  return aWin32Path;
#endif
}
#endif

void
ParseArgs(int argc, char* argv[])
{
  const char* anOptions = "b:o:Ddlcf";
  for (int anOption; (anOption = getopt(argc, argv, anOptions)) != -1;)
    {
      switch (anOption)
        {
        case 'b':
          theImageBase = StringToUlong(optarg);
          break;
        case 'd':
          theDownFlag = TRUE;
          break;
        case 'o':
          theOffset = StringToUlong(optarg);
          break;
        case 'l':
          theListFlag = 1;
          break;
        case 'c':
          theCheckFlag = 1;
          break;
        case 'f':
          theFixFlag = 1;
          break;
        case 'D':
          theDebugFlag= TRUE;
          SetImageHelperDebug(1);
          break;
        default:
          Usage();
          exit(1);
          break;
        }
    }

  if (theImageBase == 0 && !theListFlag  && !theCheckFlag && !theFixFlag)
    {
      Usage();
      exit(1);
    }

  if (theListFlag || theCheckFlag || theFixFlag )
    theArgsIndex = optind;
  else
    theArgsIndex = optind;
}

unsigned long
StringToUlong(const string& aString)
{
  stringstream ss;
  unsigned long aUlong;
  string::size_type anIndex = aString.find("0x");
  if (anIndex == 0)
    ss << hex << string(aString, 2, aString.size() - 2);
  else
    ss << aString;
  ss >> aUlong;
  return aUlong;
}

extern float release;

void
Usage()
{
  cerr << "rebase Release: " << release << endl;
  cerr << "usage: rebase [-D] -b BaseAddress [-d] -o Offset <file> ...  rebase <file>  ([-D] print debug infos)" << endl;
  cerr << "usage: rebase [-D] -l <file> ...        list Imagebase and -size of <file>" << endl;
  cerr << "usage: rebase [-D] -c                   check relocations" << endl;
  cerr << "usage: rebase [-D] -f                   fix bad relocations" << endl;
}

