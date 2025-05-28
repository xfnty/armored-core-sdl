#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_storage.h>
#include <SDL3/SDL_filesystem.h>

#include "core.h"

static struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    bool paused;
    bool paused_on_focus_lost;
    volatile const char *load_rom_path;
} app;

static void SaveStateDialogCallback(void *userdata, const char * const *filelist, int filter);
static void LoadStateDialogCallback(void *userdata, const char * const *filelist, int filter);

SDL_AppResult SDL_AppInit(void **userdata, int argc, char **argv)
{
    SDL_SetAppMetadata("SDL3 Libretro Frontend", "0.1.0", "com.xfnty.libretro-frontend");

    SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS);
    SDL_assert_release(
        SDL_CreateWindowAndRenderer(
            "SDL3 Libretro Frontend",
            500,
            500,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE,
            &app.window,
            &app.renderer
        )
    );

    if (!Core_Init(app.renderer, (CoreOptions){ .data = "data", .saves = "saves" }))
        return SDL_APP_FAILURE;

    if (!Core_LoadGame("data\\rom.chd", "data\\save.bin"))
        return SDL_APP_FAILURE;

    Core_SetCheatsEnabled(true);

    SDL_ShowWindow(app.window);
    SDL_SetWindowRelativeMouseMode(app.window, true);
    SDL_SetWindowFullscreen(app.window, true);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *userdata)
{
    if (app.load_rom_path)
    {
        Core_UnloadGame();
        Core_LoadGame("data\\rom.chd", app.load_rom_path);
        app.load_rom_path = 0;
    }

    if (!app.paused && !app.paused_on_focus_lost)
    {
        Core_RunFrame();
    }
    
    SDL_FRect s = Core_GetFramebufferRect();
    SDL_SetRenderLogicalPresentation(app.renderer, s.w, s.h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s.w--;
    s.h--;
    SDL_RenderTexture(app.renderer, Core_GetFramebuffer(), &s, 0);
    SDL_RenderPresent(app.renderer);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *userdata, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_KEY_DOWN)
    {
        if (event->key.key == SDLK_F)
        {
            SDL_SetWindowFullscreen(app.window, !(SDL_GetWindowFlags(app.window) & SDL_WINDOW_FULLSCREEN));
        }
        else if (event->key.key == SDLK_ESCAPE)
        {
            SDL_SetWindowRelativeMouseMode(app.window, !SDL_GetWindowRelativeMouseMode(app.window));
        }
        else if (event->key.key == SDLK_1)
        {
            Core_SetCheatsEnabled(!Core_AreCheatsEnabled());
        }
        else if (event->key.key == SDLK_2)
        {
            char default_dir[256] = {'\0'};
            SDL_snprintf(default_dir, sizeof(default_dir), "%s%s\\save.bin", SDL_GetBasePath(), "data");
            SDL_ShowSaveFileDialog(
                SaveStateDialogCallback,
                0,
                app.window,
                (SDL_DialogFileFilter[]){ {"Game State", "bin"} },
                1,
                default_dir
            );
        }
        else if (event->key.key == SDLK_3)
        {
            char default_dir[256] = {'\0'};
            SDL_snprintf(default_dir, sizeof(default_dir), "%s%s\\save.bin", SDL_GetBasePath(), "data");
            SDL_ShowOpenFileDialog(
                LoadStateDialogCallback,
                0,
                app.window,
                (SDL_DialogFileFilter[]){ {"Game State", "bin"} },
                1,
                default_dir,
                false
            );
        }
        else if (event->key.key == SDLK_BACKSLASH)
        {
            app.paused = !app.paused;
            SDL_Log("Paused: %d", app.paused);
        }
    }

    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        app.paused_on_focus_lost = event->type == SDL_EVENT_WINDOW_FOCUS_LOST;
        SDL_Log("Paused on focus lost: %d", app.paused_on_focus_lost);
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event->button.button == SDL_BUTTON_LEFT) Core_SetInput(CORE_JOYPAD_Y, down);
        if (event->button.button == SDL_BUTTON_RIGHT) Core_SetInput(CORE_JOYPAD_X, down);
    }

    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
    {
        bool down = event->type == SDL_EVENT_KEY_DOWN;
        if (event->key.key == SDLK_LEFT) Core_SetInput(CORE_JOYPAD_LEFT, down);
        if (event->key.key == SDLK_RIGHT) Core_SetInput(CORE_JOYPAD_RIGHT, down);
        if (event->key.key == SDLK_W) Core_SetInput(CORE_JOYPAD_UP, down);
        if (event->key.key == SDLK_S) Core_SetInput(CORE_JOYPAD_DOWN, down);
        if (event->key.key == SDLK_A) Core_SetInput(CORE_JOYPAD_L, down);
        if (event->key.key == SDLK_D) Core_SetInput(CORE_JOYPAD_R, down);
        if (event->key.key == SDLK_RETURN) Core_SetInput(CORE_JOYPAD_START, down);
        if (event->key.key == SDLK_BACKSPACE) Core_SetInput(CORE_JOYPAD_SELECT, down);
        if (event->key.key == SDLK_SPACE) Core_SetInput(CORE_JOYPAD_B, down);
        if (event->key.key == SDLK_X) Core_SetInput(CORE_JOYPAD_A, down);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *userdata, SDL_AppResult result)
{
    if (result == SDL_APP_SUCCESS)
    {
        Core_SaveGame("data\\save.bin");
    }
    
    Core_UnloadGame();
    Core_Free();
    SDL_memset(&app, 0, sizeof(app));
}

void SaveStateDialogCallback(void *userdata, const char * const *filelist, int filter)
{
    if (!filelist)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SaveStateDialogCallback: %s", SDL_GetError());
        return;
    }

    if (*filelist)
    {
        Core_SaveGame(*filelist);
    }
}

void LoadStateDialogCallback(void *userdata, const char * const *filelist, int filter)
{
    if (!filelist) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadStateDialogCallback: %s", SDL_GetError());
        return;
    }

    if (*filelist)
    {
        app.load_rom_path = *filelist;
    }
}
