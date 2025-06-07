#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_storage.h>
#include <SDL3/SDL_filesystem.h>

#include "core.h"

static struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool paused;
    bool paused_on_focus_lost;
    SDL_Mutex *lock;
    bool waiting_for_dialog;
    Uint64 last_autosave_time;
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

    app.lock = SDL_CreateMutex();
    SDL_assert_release(app.lock);

    if (!Core_Init(app.renderer, (CoreOptions){ .data = "data", .saves = "saves" }))
        return SDL_APP_FAILURE;

    Core_SetCheatsEnabled(true);

    SDL_ShowWindow(app.window);
    SDL_SetWindowRelativeMouseMode(app.window, true);
    SDL_SetWindowFullscreen(app.window, true);

    SDL_ShowOpenFileDialog(
        LoadStateDialogCallback,
        0,
        app.window,
        (SDL_DialogFileFilter[]){ {"Save File", "bin"} },
        1,
        SDL_GetBasePath(),
        false
    );
    app.waiting_for_dialog = true;

    app.last_autosave_time = SDL_GetTicks();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *userdata)
{
    SDL_LockMutex(app.lock);

    if (!app.waiting_for_dialog && !app.paused && !app.paused_on_focus_lost)
    {
        Core_RunFrame();

        Uint64 t = SDL_GetTicks();
        if (t - app.last_autosave_time > 60 * 1000)
        {
            Core_SaveGame("data\\autosave.bin");
            app.last_autosave_time = t;
        }
    }

    SDL_FRect s = Core_GetFramebufferRect();
    SDL_SetRenderLogicalPresentation(app.renderer, s.w, s.h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s.w--;
    s.h--;
    SDL_RenderTexture(app.renderer, Core_GetFramebuffer(), &s, 0);
    SDL_RenderPresent(app.renderer);

    SDL_UnlockMutex(app.lock);
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
            SDL_LockMutex(app.lock);
            Core_SetCheatsEnabled(!Core_AreCheatsEnabled());
            SDL_UnlockMutex(app.lock);
        }
        else if (event->key.key == SDLK_2)
        {
            char default_dir[256] = {'\0'};
            SDL_snprintf(default_dir, sizeof(default_dir), "%s%s\\save.bin", SDL_GetBasePath(), "data");
            SDL_ShowSaveFileDialog(
                SaveStateDialogCallback,
                0,
                app.window,
                (SDL_DialogFileFilter[]){ {"Save File", "bin"} },
                1,
                default_dir
            );
            
            SDL_LockMutex(app.lock);
            app.paused_on_focus_lost = true;
            SDL_UnlockMutex(app.lock);
            SDL_Log("Paused on dialog open: %d", app.paused_on_focus_lost);
        }
        else if (event->key.key == SDLK_BACKSLASH)
        {
            SDL_LockMutex(app.lock);
            app.paused = !app.paused;
            SDL_UnlockMutex(app.lock);
            SDL_Log("Paused: %d", app.paused);
        }
    }

    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        SDL_LockMutex(app.lock);
        app.paused_on_focus_lost = event->type == SDL_EVENT_WINDOW_FOCUS_LOST;
        SDL_UnlockMutex(app.lock);
        SDL_Log("Paused on focus lost: %d", app.paused_on_focus_lost);
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        SDL_LockMutex(app.lock);
        bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event->button.button == SDL_BUTTON_LEFT) Core_SetInput(CORE_JOYPAD_Y, down);
        if (event->button.button == SDL_BUTTON_RIGHT) Core_SetInput(CORE_JOYPAD_X, down);
        SDL_UnlockMutex(app.lock);
    }

    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
    {
        SDL_LockMutex(app.lock);
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
        SDL_UnlockMutex(app.lock);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *userdata, SDL_AppResult result)
{
    if (result == SDL_APP_SUCCESS)
    {
        Core_SaveGame("data\\autosave.bin");
    }

    Core_UnloadGame();
    Core_Free();
    SDL_memset(&app, 0, sizeof(app));
}

void SaveStateDialogCallback(void *userdata, const char * const *filelist, int filter)
{
    if (*filelist)
    {
        SDL_LockMutex(app.lock);

        const char *ext = SDL_strrchr(*filelist, '.');
        if (!ext || SDL_strcmp(".bin", ext) != 0)
        {
            char b[512];
            SDL_snprintf(b, sizeof(b), "%s.bin", *filelist);
            Core_SaveGame(b);
        }
        else
        {
            Core_SaveGame(*filelist);
        }

        SDL_UnlockMutex(app.lock);
    }
}

void LoadStateDialogCallback(void *userdata, const char * const *filelist, int filter)
{
    SDL_LockMutex(app.lock);
    if (*filelist)
    {
        Core_LoadGame("data\\rom.chd", *filelist);
        app.waiting_for_dialog = false;
    }
    else
    {
        SDL_ShowOpenFileDialog(
            LoadStateDialogCallback,
            0,
            app.window,
            (SDL_DialogFileFilter[]){ {"Save File", "bin"} },
            1,
            SDL_GetBasePath(),
            false
        );
        app.waiting_for_dialog = true;
    }
    SDL_UnlockMutex(app.lock);
}
