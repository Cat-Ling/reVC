#pragma once
#ifndef __MODLOADER_H__
#define __MODLOADER_H__

void ModloaderInit();
const char* ModloaderGetOverridePath(const char* originalPath);
void ModloaderLoadCdDirectory();

struct ModFolder {
    char name[64];
    int8 enabled;
};
extern ModFolder gModFolders[64];
extern int gNumModFolders;

#endif
