

BOOL GetImageInfos(char *filename, uint *ImageBase, uint *ImageSize);
BOOL CheckImage(char *filename);
BOOL FixImage(char *filename);
BOOL ReBaseImage(
  PSTR CurrentImageName,
  PSTR SymbolPath,        // ignored
  BOOL fReBase,
  BOOL fRebaseSysfileOk,   // ignored
  BOOL fGoingDown,         // ignored
  ULONG CheckImageSize,    // ignored
  ULONG *OldImageSize,
  ULONG *OldImageBase,
  ULONG *NewImageSize,
  ULONG *NewImageBase,
  ULONG TimeStamp
);


