/*
 * Copyright (c) 2002 Ralf Habacker 
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

#include <windows.h>
/* Take care of old w32api releases which screwed up the definition. */
#ifndef IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
# define IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE 0x40
#endif

#include "objectfile.h"
#include "imagehelper.h"

BOOL ReBaseChangeFileTime = FALSE;
BOOL ReBaseDropDynamicbaseFlag = FALSE;

BOOL ReBaseImage64 (
  LPCSTR CurrentImageName,
  LPCSTR SymbolPath,       // ignored
  BOOL fReBase,
  BOOL fRebaseSysfileOk,   // ignored
  BOOL fGoingDown,
  ULONG CheckImageSize,    // ignored
  ULONG *OldImageSize,
  ULONG64 *OldImageBase,
  ULONG *NewImageSize,
  ULONG64 *NewImageBase,
  ULONG TimeStamp
)
{
  if (fReBase == 0)
    {
      SetLastError(ERROR_INVALID_PARAMETER);
      return false;
    }

  LinkedObjectFile dll(CurrentImageName,true);

  if (!dll.isLoaded())
    {
      SetLastError(ERROR_FILE_NOT_FOUND);
      return false;
    }

  if (!dll.checkRelocations())
    {
      if (Base::debug)
        std::cerr << "error: dll relocation errors - please fix the errors at first" << std::endl;
      SetLastError(ERROR_INVALID_DATA);
      return false;
    }

  PIMAGE_NT_HEADERS32 ntheader32 = dll.getNTHeader32 ();
  PIMAGE_NT_HEADERS64 ntheader64 = dll.getNTHeader64 ();

  // set new header elements
  if (dll.is64bit ())
    {
      *OldImageBase = ntheader64->OptionalHeader.ImageBase;
      *OldImageSize = ntheader64->OptionalHeader.SizeOfImage;
      *NewImageSize = ntheader64->OptionalHeader.SizeOfImage;
    }
  else
    {
      *OldImageBase = ntheader32->OptionalHeader.ImageBase;
      *OldImageSize = ntheader32->OptionalHeader.SizeOfImage;
      *NewImageSize = ntheader32->OptionalHeader.SizeOfImage;
    }

  // Round NewImageSize to be consistent with MS's rebase.
  const ULONG imageSizeGranularity = 0x10000;
  ULONG remainder = *NewImageSize % imageSizeGranularity;
  if (remainder)
    *NewImageSize = (*NewImageSize - remainder) + imageSizeGranularity;

  if (fGoingDown)
    *NewImageBase -= *NewImageSize;

  // already rebased
  if ((dll.is64bit () && ntheader64->OptionalHeader.ImageBase == *NewImageBase)
      || (dll.is32bit () && ntheader32->OptionalHeader.ImageBase == *NewImageBase))
    {
      if (!fGoingDown)
        *NewImageBase += *NewImageSize;
      if (Base::debug)
        std::cerr << "dll is already rebased" << std::endl;
      SetLastError(NO_ERROR);
      return true;
    }

  if (dll.is64bit ())
    {
      ntheader64->OptionalHeader.ImageBase = *NewImageBase;
      ntheader64->FileHeader.TimeDateStamp = TimeStamp;
    }
  else
    {
      ntheader32->OptionalHeader.ImageBase = *NewImageBase;
      ntheader32->FileHeader.TimeDateStamp = TimeStamp;
    }

  int64_t difference = *NewImageBase - *OldImageBase;

  if (!dll.performRelocation(difference))
    {
      if (Base::debug)
        std::cerr << "error: could not rebase image" << std::endl;
      SetLastError(ERROR_BAD_FORMAT);
      return false;
    }

  if (ReBaseChangeFileTime)
    dll.setFileTime (TimeStamp);

  if (ReBaseDropDynamicbaseFlag)
    {
      if (dll.is64bit ())
	ntheader64->OptionalHeader.DllCharacteristics
	  &= ~IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
      else
	ntheader32->OptionalHeader.DllCharacteristics
	  &= ~IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
    }

  if (!fGoingDown)
    *NewImageBase += *NewImageSize;

  SetLastError(NO_ERROR);
  return true;
}

BOOL ReBaseImage (
  LPCSTR CurrentImageName,
  LPCSTR SymbolPath,       // ignored
  BOOL fReBase,
  BOOL fRebaseSysfileOk,   // ignored
  BOOL fGoingDown,
  ULONG CheckImageSize,    // ignored
  ULONG *OldImageSize,
  ULONG *OldImageBase,
  ULONG *NewImageSize,
  ULONG *NewImageBase,
  ULONG TimeStamp
)
{
  ULONG64 old_base = *OldImageBase;
  ULONG64 new_base = *NewImageBase;
  BOOL ret = ReBaseImage64 (CurrentImageName, SymbolPath, fReBase,
			    fRebaseSysfileOk, fGoingDown, CheckImageSize,
			    OldImageSize, &old_base, NewImageSize, &new_base,
			    TimeStamp);
  *OldImageBase = (ULONG) old_base;
  *NewImageBase = (ULONG) new_base;
  return ret;
}
