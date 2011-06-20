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
 *  $Id$
 */

#include <iostream>
#include <sstream>

#include "imagehelper.h"
#include "objectfile.h"

BOOL BindImageEx(
  DWORD Flags,
  LPSTR ImageName,
  LPSTR DllPath,
  LPSTR SymbolPath,
  PIMAGEHLP_STATUS_ROUTINE StatusRoutine
)
{
  ObjectFileList cache;
  LinkedObjectFile dll(ImageName);

  if (!dll.isLoaded())
    {
      if (Base::debug)
        std::cerr << "error: could not open file" << std::endl;
      SetLastError(ERROR_FILE_NOT_FOUND);
      return false;
    }

  if (!dll.rebind(cache))
    {
      SetLastError(ERROR_INVALID_DATA);
      return false;
    }

  SetLastError(NO_ERROR);
  return true;
}


BOOL BindImage(
  LPSTR ImageName,
  LPSTR DllPath,
  LPSTR SymbolPath
)
{
  return BindImageEx(0,ImageName,DllPath,SymbolPath,0);
}


