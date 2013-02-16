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
#include <iomanip>
#include <time.h>

#include "objectfile.h"

// read a dll into the cache

#if defined(__CYGWIN__) || defined(__MSYS__)
#include <sys/cygwin.h>
#endif

static PCWSTR
Win32Path(const char *s)
{
  /* No multithreading so a static global buffer is sufficient. */
  static WCHAR w32_pbuf[32768];

  if (!s || *s == '\0')
    return L"";
#if !defined (__CYGWIN__)
  MultiByteToWideChar (CP_OEMCP, 0, s, -1, w32_pbuf, 32768);
#elif defined(__MSYS__)
  {
    char buf[MAX_PATH];
    cygwin_conv_to_win32_path(s, buf);
    MultiByteToWideChar (CP_OEMCP, 0, buf, -1, w32_pbuf, 32768);
  }
#else
  cygwin_conv_path (CCP_POSIX_TO_WIN_W, s, w32_pbuf, 32768 * sizeof (WCHAR));
#endif
  return w32_pbuf;
}


//------- class ObjectFile ------------------------------------------

ObjectFile::ObjectFile(const char *aFileName, bool writeable)
{
  sections = 0;
  isWritable = writeable;
  FileName = 0;
  lpFileBase = 0;
  hfile = 0;
  hfilemapping = 0;
  PCWSTR win32_path = Win32Path(aFileName);

  // search for raw filename
  hfile = CreateFileW(win32_path,
		      writeable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
		      FILE_SHARE_READ, NULL, OPEN_EXISTING,
		      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (hfile != INVALID_HANDLE_VALUE)
    FileName = strdup(aFileName);

  // not found, try with PATH env
  else
    {
      char name[MAX_PATH];
      char path[MAX_PATH*20];
      char *s = getenv("PATH");
      strcpy(path,s);

      const char *basename = strrchr(aFileName,'/');
      basename = basename ? basename+1 : aFileName;

      for (s = strtok(path,":"); s; s = strtok(NULL,":") )
        {
          strcpy(name,s);
          strcat(name,"/");
          strcat(name,basename);
          if (debug)
            std::cerr << __FUNCTION__ << ": name:" << name << std::endl;
          hfile = CreateFileW(win32_path,
		      writeable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
		      FILE_SHARE_READ, NULL, OPEN_EXISTING,
		      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
          // found
          if (hfile != INVALID_HANDLE_VALUE)
            break;
        }

      if (!s)
        {
          Error = 2;
          return;
        }
      FileName = strdup(name);
    }

  hfilemapping = CreateFileMapping(hfile, NULL, writeable ? PAGE_READWRITE : PAGE_READONLY , 0, 0,  NULL);
  if (hfilemapping == 0)
    {
      CloseHandle(hfile);
      Error = 2;
      return;
    }

  lpFileBase = MapViewOfFile(hfilemapping, writeable ? FILE_MAP_WRITE : FILE_MAP_READ,0, 0, 0);
  if (lpFileBase == 0)
    {
      CloseHandle(hfilemapping);
      CloseHandle(hfile);
      Error = 3;
      return;
    }

  // create shortcuts
  PIMAGE_DOS_HEADER dosheader = (PIMAGE_DOS_HEADER)lpFileBase;
  ntheader = (PIMAGE_NT_HEADERS32) ((char *)dosheader + dosheader->e_lfanew);

  // basic file sanity checks to avoid crashes.
  
  // is it an executable at all?
  if (dosheader->e_magic != 0x5a4d)	/* "MZ" */
    {
      Error = 4;
      return;
    }
  // filesize big enough to allow at least reading the NT header?
  if (GetFileSize (hfile, NULL)
      < (char *) ntheader - (char *) dosheader + sizeof *ntheader)
    {
      Error = 4;
      return;
    }
  // correct signature for a PE file?
  if (ntheader->Signature != 0x00004550)
    {
      Error = 4;
      return;
    }

  is64bit_img = ntheader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  machine_type = ntheader->FileHeader.Machine;

  sections = new SectionList(lpFileBase);

  ImageBase = is64bit_img
	      ? getNTHeader64 ()->OptionalHeader.ImageBase
	      : getNTHeader32 ()->OptionalHeader.ImageBase;

  Error = 0;
}

ObjectFile::~ObjectFile()
{
  if (sections)
    delete sections;
  if (FileName)
    free(FileName);
  if (lpFileBase)
    UnmapViewOfFile(lpFileBase);
  if (hfilemapping)
    CloseHandle(hfilemapping);
  if (hfile)
    CloseHandle(hfile);
}




int LinkedObjectFile::level = 0;

LinkedObjectFile::LinkedObjectFile(const char *aFileName, bool writable) : ObjectFile(aFileName,writable)
{
  exports = 0;
  imports = 0;
  relocs = 0;
  isPrinted = 0;

  if (Error)
    return;

  if (debug)
    {
      std::cerr << "Base:       0x" << std::setw(8) << std::setfill('0') \
      << std::hex << lpFileBase << std::dec << std::endl;
      std::cerr << "ImageBase:  0x" << std::setw(8) << std::setfill('0') \
      << std::hex << ImageBase << std::dec << std::endl;
    }

  PIMAGE_NT_HEADERS32 ntheader32 = getNTHeader32 ();
  PIMAGE_NT_HEADERS64 ntheader64 = getNTHeader64 ();

  Section *edata = sections->find(".edata");
  if (edata)
    exports = new Exports(*edata);
  else
    {
      DataDirectory *dir;
      if (is64bit ())
	dir = (DataDirectory *) &ntheader64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
      else
	dir = (DataDirectory *) &ntheader32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
      exports = new Exports(*sections, dir);
    }


  Section *idata = sections->find(".idata");
  if (idata)
    imports = new Imports(*idata);
  else
    {
      DataDirectory *dir;
      if (is64bit ())
	dir = (DataDirectory *)&ntheader64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
      else
	dir = (DataDirectory *)&ntheader32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
      imports = new Imports(*sections, dir);
    }



  relocs = new Relocations(*sections,".reloc");

}

bool LinkedObjectFile::rebind(ObjectFileList &cache)
{
  if (!isWritable)
    {
      if (debug)
        std::cerr << __FUNCTION__ << ": error - the objectfile is not writeable" << std::endl;
      return false;
    }
  // FIXME: set error code

  ImportDescriptor *p;

  Section *idata = sections->find(".idata");
  Section *text = sections->find(".text");

  LinkedObjectFile *obj;

  imports->reset();

  while ((p = imports->getNextDescriptor()) != NULL)
    {
      bool autoImportFlag;
      int *patch_address;
      char *dllname = (char *)idata->getAdjust() + p->Name;
      //  std::cerr << dllname << std::endl;

      if (!(obj = (LinkedObjectFile *)cache.get(dllname) ) )
        {
          obj = new LinkedObjectFile(dllname);
          if (obj->getError())
            {
              if (debug)
                std::cerr << "cant load dll '" << dllname << "'" << std::endl;
              delete obj;
              continue;
            }
          cache.add(obj);
        }
      if (debug)
        std::cerr << obj->getFileName() << std::endl;

      PIMAGE_THUNK_DATA hintArray = PIMAGE_THUNK_DATA ((uint) p->OriginalFirstThunk + imports->getAdjust());

      PIMAGE_THUNK_DATA firstArray;
      if (debug)
        std::cerr << "FirstThunk 0x" << std::setw(8) << std::setfill('0') \
        << std::hex << p->FirstThunk << std::dec << std::endl;

      if ((autoImportFlag = text->isIn((uint)p->FirstThunk)))
        firstArray = PIMAGE_THUNK_DATA ((uint) p->FirstThunk + text->getAdjust());
      else
        firstArray = PIMAGE_THUNK_DATA ((uint) p->FirstThunk + imports->getAdjust());

      if (debug)
        std::cerr << "FirstArray 0x" << std::setw(8) << std::setfill('0') \
        << std::hex << firstArray << std::dec << std::endl;

      for (; hintArray->u1.Function; hintArray++, firstArray++)
        {
          PIMAGE_IMPORT_BY_NAME a = PIMAGE_IMPORT_BY_NAME ((uint)hintArray->u1.AddressOfData + idata->getAdjust());

          if (debug)
            std::cerr << "symbol: " << a->Name << std::endl;

          if (autoImportFlag)
            patch_address = (int *)&firstArray;
          else
            patch_address = (int *)&firstArray->u1.Function;

          if (debug)
            std::cerr << "patch_address 0x" << std::setw(8) << std::setfill('0') \
            << std::hex << patch_address << std::dec << \
            " content 0x" << std::setw(8) << std::setfill('0') \
            << std::hex << *patch_address << std::dec << \
            std::endl;

          char *name = (char *)a->Name;

          uint addr = obj->exports->getVirtualAddress(name);
          if (debug)
            std::cerr << "symaddr: 0x" << std::setw(8) << std::setfill('0') \
            << std::hex << addr + obj->ImageBase << std::dec << std::endl;
          *patch_address = (addr + obj->ImageBase);
        }
      // set
      p->TimeDateStamp = 0xffffffff;
      p->ForwarderChain = 0xffffffff;
    }

  PIMAGE_NT_HEADERS32 ntheader32 = getNTHeader32 ();
  PIMAGE_NT_HEADERS64 ntheader64 = getNTHeader64 ();

  if (is64bit ())
    ntheader64->FileHeader.TimeDateStamp = time(0);
  else
    ntheader32->FileHeader.TimeDateStamp = time(0);

#if 1
  // fill bound import section
  DataDirectory *bdp;
  SectionHeader *first_section;
  BoundImportDescriptor *bp_org;
  
  if (is64bit ())
    {
      bdp = (DataDirectory *)&ntheader64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT];
      first_section = (SectionHeader *)(ntheader64+1);
      bp_org = (BoundImportDescriptor *)(&first_section[ntheader64->FileHeader.NumberOfSections]);
    }
  else
    {
      bdp = (DataDirectory *)&ntheader32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT];
      first_section = (SectionHeader *)(ntheader32+1);
      bp_org = (BoundImportDescriptor *)(&first_section[ntheader32->FileHeader.NumberOfSections]);
    }
  BoundImportDescriptor *bp = bp_org;
  char *bp2 = (char *)&bp[cache.getCount() + 1];

  cache.reset();

  while ((obj = (LinkedObjectFile *)cache.getNext()) != NULL)
    {
      bp->TimeDateStamp = time(0);
      bp->OffsetModuleName = (uintptr_t) bp2 - (uintptr_t) bp_org;
      // bp->Reserved
      bp->NumberOfModuleForwarderRefs = 0;
      bp++;

      strcpy(bp2,obj->getFileName());
      bp2 +=  strlen(obj->getFileName()) + 1;
    }

  // set stop entry
  bp->TimeDateStamp = 0;
  bp->OffsetModuleName = 0;
  // bp->Reserved
  bp->NumberOfModuleForwarderRefs = 0;

  // set data directory entry
  bdp->VirtualAddress = (uintptr_t) bp_org - (uintptr_t) lpFileBase;
  bdp->Size = (uintptr_t) bp2 - (uintptr_t) bp_org;
#endif
  return true;
}

bool LinkedObjectFile::PrintDependencies(ObjectFileList &cache)
{
  ImportDescriptor *p;

  LinkedObjectFile *obj;

  imports->reset();

  const char *filler="                                                          ";

  if (!isPrinted)
    {
      if (level == 0)
        std::cout << getFileName() << std::endl;
      else
        std::cout << filler + strlen(filler)-level*2 << getFileName() << std::endl;
    }
  else
    {
      isPrinted = true;
      return true;
    }

  while ((p = imports->getNextDescriptor()) != NULL)
    {
      Section *sect = sections->find(p->Name);
      char *dllname = (char *)sect->getAdjust() + p->Name;

      if (!(obj = (LinkedObjectFile *)cache.get(dllname) ) )
        {
          obj = new LinkedObjectFile(dllname);
          if (obj->getError())
            {
              if (debug)
                std::cerr << "cant load dll '" << dllname << "'" << std::endl;
              delete obj;
              continue;
            }
          cache.add(obj);
        }

      level++;
      obj->PrintDependencies(cache);
      level--;
    }
  return false;
}

//
// for text symbols - read
//

bool LinkedObjectFile::unbind(void)
{
  if (!isWritable)
    {
      if (debug)
        std::cerr << __FUNCTION__ << ": error - the objectfile is not writeable" << std::endl;
      return false;
    }
  // FIXME: set error code

  imports->reset();

  ImportDescriptor *p;

  while ((p = imports->getNextDescriptor()) != NULL)
    {

      // set
      p->TimeDateStamp = 0;
      p->ForwarderChain = 0;
    }
  if (is64bit ())
    getNTHeader64 ()->FileHeader.TimeDateStamp = time(0);
  else
    getNTHeader32 ()->FileHeader.TimeDateStamp = time(0);

  // fill bound import section
  DataDirectory *bdp;
  
  if (is64bit ())
    bdp = (DataDirectory *) &getNTHeader64 ()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT];
  else
    bdp = (DataDirectory *) &getNTHeader32 ()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT];

  // set data directory entry
  bdp->VirtualAddress = 0;
  bdp->Size = 0;
  return true;
}


LinkedObjectFile::~LinkedObjectFile()
{
  if (exports)
    delete exports;
  if (imports)
    delete imports;
  if (relocs)
    delete relocs;
}

#ifdef OBJECTFILE_MAIN
int main(int argc, char **argv)
{
  ObjectFile test("/bin/cygz.dll");
  std::cout << test.getFileName() << std::endl;


  LinkedObjectFile test("/bin/cygz.dll");

  /* FIXME: update necessary
    char *symbol = "gzgets"; 
    std::cerr << std::setw(20) << std::setfill(' ') << symbol << " 0x" \
      << std::setw(8) << std::setfill('0') \
      << std::hex << test.getSymbolAddress(symbol) << std::dec << std::endl;

    symbol = "_dist_code"; 
    std::cerr << std::setw(20) << std::setfill(' ') << symbol << " 0x" \
      << std::setw(8) << std::setfill('0') \
      << std::hex << test.getSymbolAddress(symbol) << std::dec << std::endl;
   
    symbol = "zlibVersion"; 
    std::cerr << std::setw(20) << std::setfill(' ') << symbol << " 0x" \
      << std::setw(8) << std::setfill('0') \
      << std::hex << test.getSymbolAddress(symbol) << std::dec << std::endl;
  */

  return 0;
}

#endif
