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

// lists Imagebase and Image size of a dll

#include <iostream>
#include <sstream>

#include "objectfile.h"
#include "imagehelper.h"


BOOL GetImageInfos64(LPCSTR filename, WORD *machine,
		     ULONG64 *ImageBase, ULONG *ImageSize)
{
  LinkedObjectFile dll(filename);

  if (!dll.isLoaded())
    {
      if (Base::debug)
        std::cerr << "error: could not open file" << std::endl;
      SetLastError(ERROR_FILE_NOT_FOUND);
      return false;
    }

  if (dll.is64bit ())
    {
      *ImageBase = dll.getNTHeader64 ()->OptionalHeader.ImageBase;
      *ImageSize = dll.getNTHeader64 ()->OptionalHeader.SizeOfImage;
    }
  else
    {
      *ImageBase = dll.getNTHeader32 ()->OptionalHeader.ImageBase;
      *ImageSize = dll.getNTHeader32 ()->OptionalHeader.SizeOfImage;
    }
  if (machine)
    *machine = dll.machine ();

  if (Base::debug)
    std::cerr << "ImageBase: 0x" << std::hex << *ImageBase << " ImageSize: 0x" << std::hex << *ImageSize << std::endl;

  SetLastError(NO_ERROR);
  return true;
}

BOOL GetImageInfos(LPCSTR filename, ULONG *ImageBase, ULONG *ImageSize)
{
  ULONG64 base;
  BOOL ret = GetImageInfos64 (filename, NULL, &base, ImageSize);
  *ImageBase = (ULONG) base;
  return ret;
}
