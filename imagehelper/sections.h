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

#ifndef SECTIONS_H
#define SECTIONS_H

#include <windows.h>

#if !defined(__CYGWIN__) || defined(__MSYS__)
/* MinGW|MSYS: mingw only defines uintptr_t for        */
/* MSVC 2005 or better, and MSYS doesn't have stdint.h */
# ifndef _UINTPTR_T_DEFINED
#  define _UINTPTR_T_DEFINED
#  ifdef _WIN64
     typedef unsigned __int64 uintptr_t;
#  else
     typedef unsigned int uintptr_t;
#  endif
# endif
#else
# include <stdint.h>
#endif

typedef unsigned int uint;
typedef unsigned short ushort;

/// the image section header.
/// Its encapsulate the IMAGE_SECTION_HEADER structure from the windows header file
class SectionHeader : public IMAGE_SECTION_HEADER
	{
	}; 

/// the image data directory.
/// Its encapsulate the IMAGE_EXPORT_DIRECTORY structure from the windows header file
class ExportDirectory : public IMAGE_EXPORT_DIRECTORY 
	{
	};

/// the image data directory.
/// Its encapsulate the IMAGE_DATA_DIRECTORY structure from the windows header file
class DataDirectory : public IMAGE_DATA_DIRECTORY 
	{
	};

/// the image data directory. 
/// Its encapsulate the IMAGE_IMPORT_DESCRIPTOR structure from the windows header file
class ImportDescriptor : public IMAGE_IMPORT_DESCRIPTOR
  {
  };

/// the bound import descriptor. 
/// Its encapsulate the IMAGE_BOUND_IMPORT_DESCRIPTOR structure from the windows header file
class BoundImportDescriptor : public IMAGE_BOUND_IMPORT_DESCRIPTOR 
  {
  };


/// the base class. 
/// It should be used in all classes to enable/disable debugging support. 
class Base
  {
  public:
    static int debug; /// 1 = print dedebug mode 
  };


class SectionBase : public Base
  {
  public:
    ptrdiff_t getAdjust()
    {
      return adjust;
    }

  protected:
    ptrdiff_t adjust;
  private:
  };

class Section : public SectionBase
  {
  public:
    Section(void *FileBase, SectionHeader *p);
    void debugprint(const char *title = "");
    void print(const char *title = "");
    bool isIn(Section &in);
    bool isIn(uint addr);
    char *getName(void)
    {
      return Name;
    }
    int getVirtualAddress(void)
    {
      return header->VirtualAddress;
    }
    int getSize(void)
    {
      return header->SizeOfRawData;
    }

    // return memory adress of section
    void *getStartAddress(void)
    {
      return (void *)(header->VirtualAddress + adjust);
    }

    void *rva2real(uint addr = 0)
    {
      return (void *)(addr + adjust);
    }
  private:
    char Name[9];
    //  uint FileBase;
    SectionHeader *header;
  };

class SectionList : public Base
  {
  public:
    SectionList(void *FileBase);
    ~SectionList();
    bool add
      (Section *asection);
    Section *find(const char *name);
    Section *find(uint address);

    // reset iterator
    void reset(void);

    // return next item
    Section *getNext(void);

  private:
    uintptr_t FileBase;
    SectionHeader *header;
    Section **sections;
    int count;
    int iterator;
  };



class Exports : SectionBase
  {
  public:
    Exports(Section &asection);
    Exports(SectionList &sections, DataDirectory *iddp);
    uint getVirtualAddress(char *symbol, uint *ordinal = 0);
    //  int getAdjust() { return adjust; }

    void reset();

    // return next exported name
    char *getNext(void);

    void dump(const char *title = "");

  private:
    ExportDirectory *exports;
    DataDirectory *header;
    DWORD iterator;
    //  int adjust;

  };


class Imports : public SectionBase 
  {
  public:
    Imports(Section &asection);
    Imports(SectionList &sections, DataDirectory *iddp);
    void reset(void);
    ImportDescriptor *getNextDescriptor(void);
    void dump(const char *title = "");

  private:
    ImportDescriptor *imports;
    ImportDescriptor *iterator;
  };

class Relocations : SectionBase
  {
  public:
    // create a relocation object using section named "section" from  the section list "sections"
    Relocations(SectionList &sectionList, const char *sectionName);

    // check for bad relocations
    bool check(void);

    // fix bad relocations
    bool fix(void);

    // precondition: fixed dll
    bool relocate(int64_t difference);

  private:
    PIMAGE_BASE_RELOCATION relocs;
    SectionList *sections;
    int size;   // section size
  };

#endif

