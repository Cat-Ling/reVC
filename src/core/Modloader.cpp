#include "common.h"
#include "Modloader.h"
#include "CdStream.h"
#include "Streaming.h"
#include "ModelInfo.h"
#include "TxdStore.h"
#include "ColStore.h"
#include "FileMgr.h"
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>

struct ModFile {
    char basename[64];
    char path[1024];
    int8 folderIndex;
};

ModFolder gModFolders[64];
int gNumModFolders = 0;

#define MAX_MOD_FILES 16384
static ModFile* gModFiles = nil;
static int gNumModFiles = 0;

static void StringToLower(char* str) {
    while (*str) {
        *str = tolower(*str);
        str++;
    }
}

static void ScanDirectory(const char* dirPath, int folderIndex) {
    DIR* dir = opendir(dirPath);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dirPath, entry->d_name);

        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                ScanDirectory(path, folderIndex);
            } else if (S_ISREG(st.st_mode)) {
                if (gNumModFiles < MAX_MOD_FILES) {
                    strncpy(gModFiles[gNumModFiles].basename, entry->d_name, 63);
                    gModFiles[gNumModFiles].basename[63] = '\0';
                    StringToLower(gModFiles[gNumModFiles].basename);
                    
                    strncpy(gModFiles[gNumModFiles].path, path, 1023);
                    gModFiles[gNumModFiles].path[1023] = '\0';
                    
                    gModFiles[gNumModFiles].folderIndex = folderIndex;
                    gNumModFiles++;
                }
            }
        }
    }
    closedir(dir);
}

void ModloaderInit() {
    if (!gModFiles) {
        gModFiles = (ModFile*)malloc(sizeof(ModFile) * MAX_MOD_FILES);
    }
    gNumModFiles = 0;
    gNumModFolders = 0;
    
    char modloaderPath[1024];
    snprintf(modloaderPath, sizeof(modloaderPath), "%smodloader", CFileMgr::GetRootDirName());
    for (int i = 0; modloaderPath[i]; i++) {
        if (modloaderPath[i] == '\\') modloaderPath[i] = '/';
    }
    
    DIR* dir = opendir(modloaderPath);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", modloaderPath, entry->d_name);
        
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (gNumModFolders < 64) {
                strncpy(gModFolders[gNumModFolders].name, entry->d_name, 63);
                gModFolders[gNumModFolders].name[63] = '\0';
                gModFolders[gNumModFolders].enabled = 1;
                
                ScanDirectory(path, gNumModFolders);
                
                gNumModFolders++;
            }
        }
    }
    closedir(dir);
    
    debug("Modloader: Found %d loose files across %d folders.\n", gNumModFiles, gNumModFolders);
}

const char* ModloaderGetOverridePath(const char* originalPath) {
    if (!gModFiles || gNumModFiles == 0) return originalPath;

    // Extract basename from originalPath
    const char* basename = strrchr(originalPath, '/');
    const char* basenameWin = strrchr(originalPath, '\\');
    
    if (basenameWin && (!basename || basenameWin > basename)) {
        basename = basenameWin;
    }
    
    if (!basename) basename = originalPath;
    else basename++; // Skip the slash

    char lowerBasename[64];
    strncpy(lowerBasename, basename, 63);
    lowerBasename[63] = '\0';
    StringToLower(lowerBasename);
    
    // Trim trailing whitespace (especially \r from Windows line endings)
    int len = strlen(lowerBasename);
    while (len > 0 && isspace(lowerBasename[len-1])) {
        lowerBasename[len-1] = '\0';
        len--;
    }

    // Linear search is fast enough for file opens which usually happen at startup/loadscreens
    for (int i = 0; i < gNumModFiles; i++) {
        if (gModFiles[i].folderIndex >= 0 && !gModFolders[gModFiles[i].folderIndex].enabled) continue;
        
        if (strcmp(gModFiles[i].basename, lowerBasename) == 0) {
            return gModFiles[i].path;
        }
    }

    return originalPath;
}

void ModloaderLoadCdDirectory() {
    for (int i = 0; i < gNumModFiles; i++) {
        if (gModFiles[i].folderIndex >= 0 && !gModFolders[gModFiles[i].folderIndex].enabled) continue;

        const char* ext = strrchr(gModFiles[i].basename, '.');
        if (ext) {
            int modelId = -1;
            bool bStream = false;

            char nameNoExt[64];
            strncpy(nameNoExt, gModFiles[i].basename, ext - gModFiles[i].basename);
            nameNoExt[ext - gModFiles[i].basename] = '\0';

            if (!strcmp(ext, ".dff")) {
                if (CModelInfo::GetModelInfo(nameNoExt, &modelId)) {
                    bStream = true;
                }
            } else if (!strcmp(ext, ".txd")) {
                modelId = CTxdStore::FindTxdSlot(nameNoExt);
                if (modelId != -1) {
                    modelId += STREAM_OFFSET_TXD;
                    bStream = true;
                }
            } else if (!strcmp(ext, ".col")) {
                modelId = CColStore::FindColSlot(nameNoExt);
                if (modelId != -1) {
                    modelId += STREAM_OFFSET_COL;
                    bStream = true;
                }
            }

            if (bStream && modelId != -1) {
                struct stat st;
                if (stat(gModFiles[i].path, &st) == 0) {
                    uint32 customOffset = CdStreamAddCustomFile(gModFiles[i].path);
                    if (customOffset != 0) {
                        uint32 numSectors = (st.st_size + CDSTREAM_SECTOR_SIZE - 1) / CDSTREAM_SECTOR_SIZE;
                        if (numSectors > CStreaming::ms_streamingBufferSize) {
                            CStreaming::ms_streamingBufferSize = numSectors;
                        }
                        CStreaming::ms_aInfoForModel[modelId].SetCdPosnAndSize(customOffset, numSectors);
                    }
                }
            }
        }
    }
}
