#include "core.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_iostream.h>

#define kcalloc SDL_calloc
#define kmalloc SDL_malloc
#define krealloc SDL_realloc
#define kfree SDL_free
#include <klib/khash.h>

#include <swanstation/libretro.h>

KHASH_MAP_INIT_STR(dict, char*);

static struct {
    CoreOptions options;
    khash_t(dict) *vars;
    struct retro_system_info info;
    struct retro_system_av_info avinfo;
    SDL_Renderer *renderer;
    SDL_Texture *frame;
    SDL_AudioStream *audio;
    SDL_FRect frame_rect;
    Uint64 last_frame_tick;
    struct {
        bool joypad[16];
    } input;
    bool cheats;
    bool vars_dirty;
} core;

static RETRO_CALLCONV bool CoreEnvCallback(unsigned cmd, void *data);
static RETRO_CALLCONV void CoreLogCallback(enum retro_log_level level, const char *fmt, ...);
static RETRO_CALLCONV void CoreVideoCallback(
    const void *data,
    unsigned width,
    unsigned height,
    size_t pitch
);
static RETRO_CALLCONV void CoreAudioSampleCallback(int16_t left, int16_t right);
static RETRO_CALLCONV size_t CoreAudioCallback(const int16_t *data, size_t frames);
static RETRO_CALLCONV void CoreInputPollCallback(void);
static RETRO_CALLCONV int16_t CoreInputStateCallback(
    unsigned port,
    unsigned device,
    unsigned index,
    unsigned id
);
static RETRO_CALLCONV uintptr_t CoreCurrentFramebufferCallback(void);
static RETRO_CALLCONV retro_proc_address_t CoreGetProcAddressCallback(const char *sym);

bool Core_Init(SDL_Renderer *renderer, CoreOptions options)
{
    Core_Free();

    SDL_Log("Initializing Core ...");

    core.vars = kh_init(dict);
    core.renderer = renderer;
    core.options = options;

    SDL_assert(retro_api_version() == RETRO_API_VERSION);
    retro_get_system_info(&core.info);
    retro_get_system_av_info(&core.avinfo);
    SDL_Log("Using %s %s", core.info.library_name, core.info.library_version);

    core.frame_rect = (SDL_FRect){
        0,
        0,
        core.avinfo.geometry.base_width,
        core.avinfo.geometry.base_height
    };
    SDL_Log(
        "Video: %lux%lu %.0f",
        core.avinfo.geometry.base_width,
        core.avinfo.geometry.base_height,
        core.avinfo.timing.fps
    );

    core.audio = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &(SDL_AudioSpec){
            .format = SDL_AUDIO_S16LE,
            .channels = 2,
            .freq = core.avinfo.timing.sample_rate,
        },
        0,
        0
    );
    if (!core.audio)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_OpenAudioDeviceStream(): %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(core.audio);

    retro_set_environment(CoreEnvCallback);
    retro_set_video_refresh(CoreVideoCallback);
    retro_set_audio_sample(CoreAudioSampleCallback);
    retro_set_audio_sample_batch(CoreAudioCallback);
    retro_set_input_poll(CoreInputPollCallback);
    retro_set_input_state(CoreInputStateCallback);
    retro_init();
    retro_reset();

    return true;
}

void Core_Free()
{
    retro_deinit();
    kh_destroy(dict, core.vars);
    SDL_memset(&core, 0, sizeof(core));
}

void Core_SetInput(CoreInput id, bool state)
{
    SDL_assert(id < 16);
    core.input.joypad[id] = state;
}

bool Core_LoadGame(const char *path, const char *save)
{
    SDL_Log("Loading \"%s\" (save=\"%s\")...", path, save);
    struct retro_game_info info = {
        .path = path,
    };
    if (!retro_load_game(&info))
    {
        return false;
    }

    void *s = 0;
    size_t ss = 0;
    if (save)
    {
        if (!(s = SDL_LoadFile(save, &ss)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to read save file");
        }
        
        if (!retro_unserialize(s, ss))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "retro_unserialize() failed");
        }
        else
        {
            SDL_Log("Loaded save");
            Core_SetCheatsEnabled(true);
        }

        SDL_free(s);
    }

    return true;
}

void Core_UnloadGame()
{
    retro_unload_game();
}

void Core_SaveGame(const char *save)
{
    size_t size = retro_serialize_size();
    void *data = SDL_malloc(size);

    if (!retro_serialize(data, size))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "retro_serialize() failed");
        SDL_free(data);
        return;
    }
    else if (!SDL_SaveFile(save, data, size))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to write save state");
        SDL_free(data);
        return;
    }

    SDL_Log("Saved state");
}

void Core_SetCheatsEnabled(bool enabled)
{
    core.cheats = enabled;
    SDL_Log("Cheats: %d", enabled);
}

bool Core_AreCheatsEnabled()
{
    return core.cheats;
}

bool Core_RunFrame()
{
    if (core.cheats)
    {
        unsigned char *mem = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        SDL_assert(mem);
        size_t size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
        SDL_assert(size);

        float x, y;
        SDL_GetRelativeMouseState(&x, &y);
        *((uint16_t*)&mem[0x1A26CA]) -= x * 0.5;
        *((uint16_t*)&mem[0x411C0]) += y * 0.5;
    }

    Uint64 tick = SDL_GetTicks();
    if ((tick - core.last_frame_tick) / 1000.0 < 1 / core.avinfo.timing.fps)
    {
        return false;
    }

    retro_run();
    SDL_FlushAudioStream(core.audio);
    core.last_frame_tick = tick;
    return true;
}

SDL_Texture *Core_GetFramebuffer()
{
    return core.frame;
}

SDL_FRect Core_GetFramebufferRect()
{
    return core.frame_rect;
}

void Core_SetVar(const char *key, const char *value)
{
    khiter_t it = kh_get(dict, core.vars, key);
    if (it == kh_end(core.vars))
    {
        int ret = 0;
        it = kh_put(dict, core.vars, key, &ret);
    }
    else
    {
        SDL_free(kh_value(core.vars, it));
    }
    kh_value(core.vars, it) = SDL_strdup(value);
    core.vars_dirty = true;
    // SDL_Log("\"%s\" = \"%s\"", key, kh_value(core.vars, it));
}

const char *Core_GetVar(const char *key)
{
    khiter_t it = kh_get(dict, core.vars, key);
    return (it == kh_end(core.vars)) ? (kh_value(core.vars, it)) : (0);
}

static RETRO_CALLCONV bool CoreEnvCallback(unsigned cmd, void *data)
{
    if (cmd & (RETRO_ENVIRONMENT_EXPERIMENTAL | RETRO_ENVIRONMENT_PRIVATE))
        return false;

    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            ((struct retro_log_callback *)data)->log = CoreLogCallback;
            return true;
        }

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        {
            *(const char **)data = core.options.saves;
            return true;
        }

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        {
            *(const char **)data = core.options.data;
            return true;
        }

    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        {
            *(enum retro_hw_context_type*)data = RETRO_HW_CONTEXT_D3D11;
            return true;
        }

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        {
            *(bool*)data = core.vars_dirty;
            core.vars_dirty = false;
            return true;
        }

    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        {
            const struct retro_game_geometry *g = data;
            SDL_Log("Video: %lux%lu", g->base_width, g->base_height);
            core.frame_rect = (SDL_FRect){
                0,
                0,
                 g->base_width,
                 g->base_height
            };
            return true;
        }

    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        {
            struct retro_hw_render_callback *cb = data;
            if (cb->context_type != RETRO_HW_CONTEXT_D3D11)
            {
                SDL_LogError(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Core requested graphics context other than DirectX11"
                );
                return false;
            }
            cb->get_current_framebuffer = CoreCurrentFramebufferCallback;
            cb->get_proc_address = CoreGetProcAddressCallback;
            return true;
        }

    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        {
            *(unsigned int*)data = 0;
            return true;
        }

    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        return false;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        {
            const struct retro_core_option_display *d = data;
            return true;
        }

    case RETRO_ENVIRONMENT_SET_VARIABLES:
        {
            for (const struct retro_variable *var = data; var->key; var++)
            {
                char buffer[64] = {'\0'};
                SDL_Log("\"%s\" = \"%s\"", var->key, var->value);

                const char *start = SDL_strstr(var->value, "; ");
                SDL_assert_release(start);
                const char *end = SDL_strchr(start, '|');
                SDL_assert_release(end);
                int64_t l = end - (start + 2);
                SDL_assert_release(l > 0 && l < SDL_arraysize(buffer) - 1);
                SDL_memcpy(buffer, start + 2, l);
                buffer[l] = '\0';

                Core_SetVar(var->key, buffer);
            }
            return true;
        }

    case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable *var = data;
            var->value = Core_GetVar(var->key);
            return true;
        }

    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        {
            const struct retro_controller_info *info = data;
            for (unsigned int i = 0; i < info->num_types; i++)
            {
            }
            return true;
        }

    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        {
            const struct retro_disk_control_callback *cb = data;
            return true;
        }

    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        {
            *(unsigned*)data = 0;
            return true;
        }

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            const enum retro_pixel_format *f = data;
            const SDL_PixelFormat formats[] = {
                [RETRO_PIXEL_FORMAT_XRGB8888] = SDL_PIXELFORMAT_XRGB8888,
                [RETRO_PIXEL_FORMAT_RGB565] = SDL_PIXELFORMAT_RGB565,
            };
            core.frame = SDL_CreateTexture(
                core.renderer,
                formats[*f],
                SDL_TEXTUREACCESS_STREAMING,
                core.avinfo.geometry.max_width,
                core.avinfo.geometry.max_height
            );
            SDL_assert(core.frame);
            SDL_Log("Created framebuffer (%d)", *f);
            return true;
        }

    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        {
            const struct retro_input_descriptor *d = data;
            for (; d->description; d++)
            {
                // SDL_Log(
                //     "%s: port=%lu dev=%lu idx=%lu id=%lu",
                //     d->description,
                //     d->port,
                //     d->device,
                //     d->index,
                //     d->id
                // );
            }
            return false;
        }

    case RETRO_ENVIRONMENT_SET_MESSAGE:
        {
            const struct retro_message *msg = data;
            SDL_Log("%s: %s", core.info.library_name, msg->msg);
            return true;
        }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        {
            const struct retro_core_options_update_display_callback *cb = data;
            cb = 0;
            return true;
        }
    }

    SDL_Log("? %d", cmd);
    return false;
}

static RETRO_CALLCONV void CoreLogCallback(enum retro_log_level level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, args);
    va_end(args);
}


RETRO_CALLCONV void CoreVideoCallback(
    const void *data,
    unsigned width,
    unsigned height,
    size_t pitch
)
{
    if (!data)
    {
        return;
    }

    core.frame_rect.w = width;
    core.frame_rect.h = height;
    SDL_Rect r = { 0, 0, core.frame_rect.w, core.frame_rect.h };
    SDL_UpdateTexture(core.frame, &r, data, (int)pitch);
}

RETRO_CALLCONV void CoreAudioSampleCallback(int16_t left, int16_t right)
{
    int16_t buf[] = { left, right };
    SDL_PutAudioStreamData(core.audio, buf, sizeof(buf));
}

RETRO_CALLCONV size_t CoreAudioCallback(const int16_t *data, size_t frames)
{
    SDL_PutAudioStreamData(core.audio, data, (int)frames * sizeof(int16_t) * 2);
    return frames;
}

RETRO_CALLCONV void CoreInputPollCallback(void)
{
}

RETRO_CALLCONV int16_t CoreInputStateCallback(
    unsigned port,
    unsigned device,
    unsigned index,
    unsigned id
)
{
    if (port != 0) return 0;

    if (device == RETRO_DEVICE_JOYPAD) return core.input.joypad[id];

    SDL_Log("Unknown input device %lu", device);
    return 0;
}

RETRO_CALLCONV uintptr_t CoreCurrentFramebufferCallback(void)
{
    SDL_Log("get buffer");
    return 0;
}

RETRO_CALLCONV retro_proc_address_t CoreGetProcAddressCallback(const char *sym)
{
    SDL_Log("get sym %s", sym);
    return 0;
}
