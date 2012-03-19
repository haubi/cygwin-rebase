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

#ifndef OBJECTFILE_H
#define OBJECTFILE_H

#include "sections.h"

class ObjectFile : public Base
  {

  public:
    ObjectFile(const char *FileName, bool writeable = false);

    // return in the image stored filename
    const char *getFileName(void)
    {
      return FileName;
    }

    PIMAGE_NT_HEADERS64 getNTHeader64 (void)
    {
      return (PIMAGE_NT_HEADERS64) ntheader;
    }

    PIMAGE_NT_HEADERS32 getNTHeader32 (void)
    {
      return ntheader;
    }

    bool isLoaded(void)
    {
      return Error == 0;
    }

    bool is64bit(void)
    {
      return is64bit_img;
    }

    bool is32bit(void)
    {
      return !is64bit_img;
    }

    WORD machine(void)
    {
      return machine_type;
    }

    int getError(void)
    {
      return Error;
    }

    SectionList *getSections(void)
    {
      return sections;
    }

    void setFileTime (ULONG seconds_since_epoche)
    {
      LARGE_INTEGER filetime;
/* 100ns difference between Windows and UNIX timebase. */
#define FACTOR (0x19db1ded53e8000LL)
/* # of 100ns intervals per second. */
#define NSPERSEC 10000000LL
      filetime.QuadPart = seconds_since_epoche * NSPERSEC + FACTOR;
      if (!SetFileTime (hfile, NULL, NULL, (FILETIME *) &filetime))
        std::cerr << "SetFileTime: " << GetLastError () << std::endl;
    }

    ~ObjectFile();


  protected:
    char *FileName;
    HANDLE hfile;
    HANDLE hfilemapping;
    LPVOID lpFileBase;
    SectionList *sections;
    ULONG64 ImageBase;
    int Error;
    bool isWritable;

  private:
    PIMAGE_NT_HEADERS32 ntheader;
    bool is64bit_img;
    WORD machine_type;
  };

class ObjectFileList;

class LinkedObjectFile : public ObjectFile
  {

  public:
    LinkedObjectFile(const char *FileName, bool writable = false);
    ~LinkedObjectFile();

    bool rebind(ObjectFileList &cache);
    bool unbind(void);
    bool checkRelocations(void)
    {
      return relocs->check();
    }
    bool fixRelocations(void)
    {
      return relocs->fix();
    }
    bool performRelocation(int difference)
    {
      return relocs->relocate(difference);
    }
    bool PrintDependencies(ObjectFileList &cache);

    Imports *getImports()
    {
      return imports;
    }
    Exports *getExports()
    {
      return exports;
    }

  protected:
    Imports *imports;
    Exports *exports;
    Relocations *relocs;
    static int level;
    bool isPrinted;
  };

#include "objectfilelist.h"

#endif
