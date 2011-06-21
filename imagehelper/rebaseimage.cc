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

#include "objectfile.h"
#include "imagehelper.h"

BOOL ReBaseImage(
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

  PIMAGE_NT_HEADERS ntheader = dll.getNTHeader();

  // set new header elements
  *OldImageBase = ntheader->OptionalHeader.ImageBase;
  *OldImageSize = ntheader->OptionalHeader.SizeOfImage;
  *NewImageSize = ntheader->OptionalHeader.SizeOfImage;

  // Round NewImageSize to be consistent with MS's rebase.
  const ULONG imageSizeGranularity = 0x10000;
  ULONG remainder = *NewImageSize % imageSizeGranularity;
  if (remainder)
    *NewImageSize = (*NewImageSize - remainder) + imageSizeGranularity;

  if (fGoingDown)
    *NewImageBase -= *NewImageSize;

  // already rebased
  if (ntheader->OptionalHeader.ImageBase == *NewImageBase)
    {
      if (!fGoingDown)
        *NewImageBase += *NewImageSize;
      if (Base::debug)
        std::cerr << "dll is already rebased" << std::endl;
      SetLastError(NO_ERROR);
      return true;
    }

  ntheader->OptionalHeader.ImageBase = *NewImageBase;
  ntheader->FileHeader.TimeDateStamp = TimeStamp;

  int difference = *NewImageBase - *OldImageBase;

  if (!dll.performRelocation(difference))
    {
      if (Base::debug)
        std::cerr << "error: could not rebase image" << std::endl;
      SetLastError(ERROR_BAD_FORMAT);
      return false;
    }

  if (!fGoingDown)
    *NewImageBase += *NewImageSize;

  SetLastError(NO_ERROR);
  return true;
}
