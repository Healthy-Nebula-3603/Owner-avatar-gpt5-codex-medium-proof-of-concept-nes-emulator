#ifndef NES_H
#define NES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct NES;
struct Cartridge;
struct CPU6502;
struct PPU;
struct APU;
struct Controller;

typedef struct NESConfig {
    bool enable_audio;
    bool enable_vsync;
    bool keep_aspect_ratio;
} NESConfig;

typedef struct NESFrameInfo {
    const uint32_t *pixels;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    bool frame_ready;
    bool frame_skipped;
} NESFrameInfo;

struct NES *nes_create(const NESConfig *config);
void nes_destroy(struct NES *nes);

bool nes_load_rom(struct NES *nes, const char *path);
void nes_eject_rom(struct NES *nes);
bool nes_has_rom(const struct NES *nes);

void nes_reset(struct NES *nes);
void nes_power_cycle(struct NES *nes);

void nes_step(struct NES *nes, uint32_t cpu_cycles);
void nes_run_frame(struct NES *nes);

const NESFrameInfo *nes_current_frame(const struct NES *nes);

void nes_controller_write(struct NES *nes, unsigned index, uint8_t state);
uint8_t nes_controller_state(const struct NES *nes, unsigned index);
void nes_set_controller_state(struct NES *nes, unsigned index, uint8_t state);

bool nes_save_battery(const struct NES *nes);
bool nes_load_battery(struct NES *nes);

bool nes_save_state(struct NES *nes, const char *path);
bool nes_load_state(struct NES *nes, const char *path);

const struct Cartridge *nes_cartridge(const struct NES *nes);

size_t nes_audio_samples_available(const struct NES *nes);
size_t nes_audio_read(struct NES *nes, float *out, size_t max_samples);

#endif /* NES_H */
