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

    PIMAGE_NT_HEADERS getNTHeader(void)
    {
      return ntheader;
    }

    bool isLoaded(void)
    {
      return Error == 0;
    }

    int getError(void)
    {
      return Error;
    }

    SectionList *getSections(void)
    {
      return sections;
    }

    ~ObjectFile();


  protected:
    char *FileName;
    HANDLE hfile;
    HANDLE hfilemapping;
    LPVOID lpFileBase;
    PIMAGE_NT_HEADERS ntheader;
    SectionList *sections;
    uint ImageBase;
    int Error;
    bool isWritable;
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
