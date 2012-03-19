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

#ifndef MY_IMAGEHLP_H
#define MY_IMAGEHLP_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set to TRUE if ReBaseImage{64} should also set the files last write
   time to TimeStamp when the file has been successfully rebased. */
extern BOOL ReBaseChangeFileTime;

BOOL ReBaseImage64(
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
);

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
);

BOOL BindImage(
  LPCSTR ImageName,
  LPCSTR DllPath,
  LPCSTR SymbolPath
);

typedef enum _IMAGEHLP_STATUS_REASON {
  BindOutOfMemory,
  BindRvaToVaFailed,
  BindNoRoomInImage,
  BindImportModuleFailed,
  BindImportProcedureFailed,
  BindImportModule,
  BindImportProcedure,
  BindForwarder,
  BindForwarderNOT,
  BindImageModified,
  BindExpandFileHeaders,
  BindImageComplete,
  BindMismatchedSymbols,
  BindSymbolsNotUpdated
} IMAGEHLP_STATUS_REASON;

typedef BOOL(__stdcall *PIMAGEHLP_STATUS_ROUTINE)(IMAGEHLP_STATUS_REASON,LPSTR,LPSTR,ULONG,ULONG);

BOOL BindImageEx(
  DWORD Flags,
  LPCSTR ImageName,
  LPCSTR DllPath,
  LPCSTR SymbolPath,
  PIMAGEHLP_STATUS_ROUTINE
  StatusRoutine
);

BOOL GetImageInfos64(
  LPCSTR ImageName,
  WORD *machine,
  ULONG64 *ImageBase,
  ULONG *ImageSize
);

BOOL GetImageInfos(
  LPCSTR ImageName,
  ULONG *ImageBase,
  ULONG *ImageSize
);

BOOL CheckImage(
  LPCSTR ImageName
);

BOOL FixImage(
  LPCSTR ImageName
);

DWORD SetImageHelperDebug(
  DWORD level
);

#ifdef __cplusplus
}
#endif
#endif
