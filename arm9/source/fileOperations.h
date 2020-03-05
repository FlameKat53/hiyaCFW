#include <nds.h>

#ifndef FILE_COPY
#define FILE_COPY

extern off_t getFileSize(const char *fileName);
extern int fcopy(const char *sourcePath, const char *destinationPath);

#endif // FILE_COPY