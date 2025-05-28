#pragma once

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

typedef struct {
    const char *data;
    const char *saves;
} CoreOptions;

typedef enum {
    CORE_JOYPAD_B = 0,
    CORE_JOYPAD_Y = 1,
    CORE_JOYPAD_SELECT = 2,
    CORE_JOYPAD_START = 3,
    CORE_JOYPAD_UP = 4,
    CORE_JOYPAD_DOWN = 5,
    CORE_JOYPAD_LEFT = 6,
    CORE_JOYPAD_RIGHT = 7,
    CORE_JOYPAD_A = 8,
    CORE_JOYPAD_X = 9,
    CORE_JOYPAD_L = 10,
    CORE_JOYPAD_R = 11,
    CORE_JOYPAD_L2 = 12,
    CORE_JOYPAD_R2 = 13,
    CORE_JOYPAD_L3 = 14,
    CORE_JOYPAD_R3 = 15,
} CoreInput;

bool Core_Init(SDL_Renderer *renderer, CoreOptions options);
void Core_Free();

void Core_SetInput(CoreInput id, bool state);

bool Core_LoadGame(const char *path, const char *save);
void Core_UnloadGame();

void Core_SaveGame(const char *save);

void Core_SetCheatsEnabled(bool enabled);
bool Core_AreCheatsEnabled();

bool Core_RunFrame();
SDL_Texture *Core_GetFramebuffer();
SDL_FRect Core_GetFramebufferRect();

const char *Core_GetVar(const char *key);
void        Core_SetVar(const char *key, const char *value);
