/*
 * Copyright (c) 2002 Ralf Habacker  <Ralf.Habacker@freenet.de>
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

#include <stdlib.h>

#include <iostream>
#include <sstream>

#include <windows.h>

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

#include "objectfile.h"

using namespace std;

void Usage();
int fVerbose = 1;

#if defined(__CYGWIN__) || defined(__MSYS__)
char *Win32Path(char *s);
char * Win32Path(char * s)
{
  if (!s || *s == '\0')
    return strdup("");
#if defined(HAVE_DECL_CYGWIN_CONV_PATH) && HAVE_DECL_CYGWIN_CONV_PATH
  {
    char * r = (char *)cygwin_create_path(CCP_POSIX_TO_WIN_A, s);
    return (r ? r : strdup(""));
  }
#else
  {
    char buf[MAX_PATH];
    cygwin_conv_to_win32_path(s, buf);
    return strdup(buf);
  }
#endif
}
#else
#define Win32Path(s)  s
#endif


int
main(int argc, char* argv[])
{
  ObjectFileList cache;

  for (int i= 1; i < argc; i++)
    {
      LinkedObjectFile dll(argv[i]);
      // FIXME: add this stuff
      // dll.checkRelocations();
      // dll.fixRelocations();
      if (dll.isLoaded())
        dll.rebind(cache);
    }
  return 0;
}


void
Usage()
{
  cout << "usage: rebind  <dll path>" << endl;
}

