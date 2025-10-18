#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nes.h"

static void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s <rom.nes>\n", progname);
}

static void audio_callback(void *userdata, Uint8 *stream, int length) {
    struct NES *nes = userdata;
    float *out = (float *)stream;
    int frames = length / (int)sizeof(float);
    size_t fetched = nes_audio_read(nes, out, (size_t)frames);
    for (int i = (int)fetched; i < frames; ++i) {
        out[i] = 0.0f;
    }
}

static SDL_Rect compute_dest_rect(int window_w, int window_h) {
    const float target_aspect = 256.0f / 240.0f;
    float window_aspect = (float)window_w / (float)window_h;
    SDL_Rect rect;
    if (window_aspect > target_aspect) {
        rect.h = window_h;
        rect.w = (int)(window_h * target_aspect);
        rect.x = (window_w - rect.w) / 2;
        rect.y = 0;
    } else {
        rect.w = window_w;
        rect.h = (int)(window_w / target_aspect);
        rect.x = 0;
        rect.y = (window_h - rect.h) / 2;
    }
    return rect;
}

static uint8_t update_button_state(uint8_t state, SDL_Scancode scancode, bool pressed) {
    switch (scancode) {
    case SDL_SCANCODE_X: state = pressed ? (state | 0x01) : (state & ~0x01); break; // A
    case SDL_SCANCODE_Z: state = pressed ? (state | 0x02) : (state & ~0x02); break; // B
    case SDL_SCANCODE_RSHIFT: state = pressed ? (state | 0x04) : (state & ~0x04); break; // Select
    case SDL_SCANCODE_RETURN: state = pressed ? (state | 0x08) : (state & ~0x08); break; // Start
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W: state = pressed ? (state | 0x10) : (state & ~0x10); break;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S: state = pressed ? (state | 0x20) : (state & ~0x20); break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A: state = pressed ? (state | 0x40) : (state & ~0x40); break;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D: state = pressed ? (state | 0x80) : (state & ~0x80); break;
    default: break;
    }
    return state;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    NESConfig config = {
        .enable_audio = true,
        .enable_vsync = true,
        .keep_aspect_ratio = true,
    };

    struct NES *nes = nes_create(&config);
    if (!nes) {
        fprintf(stderr, "Failed to create NES instance\n");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (!nes_load_rom(nes, argv[1])) {
        fprintf(stderr, "Unable to load ROM: %s\n", argv[1]);
        nes_destroy(nes);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Window *window = SDL_CreateWindow(
        "NES Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        256 * 3,
        240 * 3,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        nes_destroy(nes);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        nes_destroy(nes);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             256,
                                             240);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        nes_destroy(nes);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_AudioSpec desired = {0};
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = audio_callback;
    desired.userdata = nes;

    SDL_AudioSpec obtained = {0};
    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (audio_device == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio_device, 0);
    }

    bool running = true;
    uint8_t controller_state = 0;
    uint32_t last_ticks = SDL_GetTicks();
    const double target_frame_ms = 1000.0 / 60.098;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                bool pressed = (event.type == SDL_KEYDOWN);
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && pressed) {
                    running = false;
                }
                controller_state = update_button_state(controller_state, event.key.keysym.scancode, pressed);
                break;
            }
            default:
                break;
            }
        }

        nes_set_controller_state(nes, 0, controller_state);
        nes_run_frame(nes);

        const NESFrameInfo *frame = nes_current_frame(nes);
        if (frame && frame->frame_ready) {
            SDL_UpdateTexture(texture, NULL, frame->pixels, (int)frame->pitch);
            int win_w = 0;
            int win_h = 0;
            SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
            SDL_Rect dst = compute_dest_rect(win_w, win_h);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, config.keep_aspect_ratio ? &dst : NULL);
            SDL_RenderPresent(renderer);
        }

        uint32_t current_ticks = SDL_GetTicks();
        double elapsed = current_ticks - last_ticks;
        if (elapsed < target_frame_ms) {
            SDL_Delay((Uint32)(target_frame_ms - elapsed));
        }
        last_ticks = SDL_GetTicks();
    }

    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
    }

    nes_save_battery(nes);
    nes_destroy(nes);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
