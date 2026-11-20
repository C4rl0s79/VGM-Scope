#pragma once
#include "player/playera.hpp"
#include "utils/FileLoader.h"
#include "utils/DataLoader.h"

inline int scope_load_file(PlayerA& player, const char* path, DATA_LOADER** output)
{
    if (!path || !output) return -1;
    *output = nullptr;
    DATA_LOADER* loader = FileLoader_Init(path);
    if (!loader) return -4;
    DataLoader_SetPreloadBytes(loader, 0x100);
    if (DataLoader_Load(loader) != 0) {
        DataLoader_Deinit(loader);
        return -4;
    }
    if (player.LoadFile(loader) != 0) {
        player.UnloadFile();
        DataLoader_Deinit(loader);
        return -5;
    }
    *output = loader;
    return 0;
}
