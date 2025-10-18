#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu.h"
#include "bus.h"
#include "cartridge.h"
#include "controller.h"
#include "cpu.h"
#include "mapper.h"
#include "nes.h"
#include "nes_internal.h"
#include "ppu.h"
#include "util.h"

struct NES *nes_create(const NESConfig *config) {
    NES *nes = calloc(1, sizeof(NES));
    if (!nes) {
        return NULL;
    }
    if (config) {
        nes->config = *config;
    } else {
        NESConfig default_config = {
            .enable_audio = true,
            .enable_vsync = true,
            .keep_aspect_ratio = true,
        };
        nes->config = default_config;
    }
    nes->cpu.nes = (struct NES *)nes;
    nes->ppu.nes = (struct NES *)nes;
    nes->apu.nes = (struct NES *)nes;
    apu_init((struct NES *)nes, &nes->apu);
    controller_reset(&nes->controllers[0]);
    controller_reset(&nes->controllers[1]);
    nes->frame_info.pixels = nes->ppu.framebuffer;
    nes->frame_info.width = 256;
    nes->frame_info.height = 240;
    nes->frame_info.pitch = 256 * sizeof(uint32_t);
    nes->frame_info.frame_ready = false;
    nes->frame_info.frame_skipped = false;
    return (struct NES *)nes;
}

void nes_destroy(struct NES *nes_ptr) {
    if (!nes_ptr) {
        return;
    }
    NES *nes = (NES *)nes_ptr;
    cartridge_unload(&nes->cart);
    free(nes);
}

bool nes_load_rom(struct NES *nes_ptr, const char *path) {
    NES *nes = (NES *)nes_ptr;
    if (!cartridge_load(&nes->cart, path, stderr)) {
        return false;
    }
    nes->rom_loaded = true;
    cpu_reset(&nes->cpu, true);
    ppu_reset(&nes->ppu, true);
    apu_reset(&nes->apu, true);
    if (nes->cart.has_battery) {
        nes_load_battery(nes_ptr);
    }
    return true;
}

void nes_eject_rom(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    cartridge_unload(&nes->cart);
    nes->rom_loaded = false;
}

bool nes_has_rom(const struct NES *nes_ptr) {
    const NES *nes = (const NES *)nes_ptr;
    return nes->rom_loaded;
}

void nes_reset(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    cpu_reset(&nes->cpu, false);
    ppu_reset(&nes->ppu, false);
    apu_reset(&nes->apu, false);
}

void nes_power_cycle(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    cpu_reset(&nes->cpu, true);
    ppu_reset(&nes->ppu, true);
    apu_reset(&nes->apu, true);
}

void nes_step(struct NES *nes_ptr, uint32_t cpu_cycles) {
    NES *nes = (NES *)nes_ptr;
    uint32_t executed = 0;
    while (executed < cpu_cycles) {
        uint8_t inst_cycles = cpu_step(&nes->cpu);
        for (uint8_t i = 0; i < inst_cycles; ++i) {
            ppu_step(&nes->ppu);
            ppu_step(&nes->ppu);
            ppu_step(&nes->ppu);
            apu_step(&nes->apu, 1);
            nes_mapper_cpu_step(nes_ptr);
            bool irq = nes_mapper_irq_pending(nes_ptr) || apu_irq_pending(&nes->apu);
            cpu_set_irq_line(&nes->cpu, irq);
        }
        executed += inst_cycles;
    }
}

void nes_run_frame(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    if (!nes->rom_loaded) {
        return;
    }
    nes->frame_info.frame_ready = false;
    if (ppu_frame_ready(&nes->ppu)) {
        ppu_clear_frame_ready(&nes->ppu);
    }
    while (!ppu_frame_ready(&nes->ppu)) {
        uint8_t inst_cycles = cpu_step(&nes->cpu);
        for (uint8_t i = 0; i < inst_cycles; ++i) {
            ppu_step(&nes->ppu);
            ppu_step(&nes->ppu);
            ppu_step(&nes->ppu);
            apu_step(&nes->apu, 1);
            nes_mapper_cpu_step(nes_ptr);
            bool irq = nes_mapper_irq_pending(nes_ptr) || apu_irq_pending(&nes->apu);
            cpu_set_irq_line(&nes->cpu, irq);
        }
    }
    nes->frame_info.frame_ready = true;
    ppu_clear_frame_ready(&nes->ppu);
}

const NESFrameInfo *nes_current_frame(const struct NES *nes_ptr) {
    const NES *nes = (const NES *)nes_ptr;
    return &nes->frame_info;
}

void nes_controller_write(struct NES *nes_ptr, unsigned index, uint8_t state) {
    NES *nes = (NES *)nes_ptr;
    if (index >= 2) {
        return;
    }
    controller_write(&nes->controllers[index], state);
}

uint8_t nes_controller_state(const struct NES *nes_ptr, unsigned index) {
    const NES *nes = (const NES *)nes_ptr;
    if (index >= 2) {
        return 0;
    }
    return nes->controllers[index].state;
}

void nes_set_controller_state(struct NES *nes_ptr, unsigned index, uint8_t state) {
    NES *nes = (NES *)nes_ptr;
    if (index >= 2) {
        return;
    }
    controller_set_state(&nes->controllers[index], state);
}

bool nes_save_battery(const struct NES *nes_ptr) {
    const NES *nes = (const NES *)nes_ptr;
    if (!nes->rom_loaded) {
        return false;
    }
    return cartridge_save_battery(&nes->cart);
}

bool nes_load_battery(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    if (!nes->rom_loaded) {
        return false;
    }
    return cartridge_load_battery(&nes->cart);
}

bool nes_save_state(struct NES *nes_ptr, const char *path) {
    NES_UNUSED(nes_ptr);
    NES_UNUSED(path);
    return false;
}

bool nes_load_state(struct NES *nes_ptr, const char *path) {
    NES_UNUSED(nes_ptr);
    NES_UNUSED(path);
    return false;
}

const Cartridge *nes_cartridge(const struct NES *nes_ptr) {
    const NES *nes = (const NES *)nes_ptr;
    return &nes->cart;
}

size_t nes_audio_samples_available(const struct NES *nes_ptr) {
    const NES *nes = (const NES *)nes_ptr;
    return apu_samples_available(&nes->apu);
}

size_t nes_audio_read(struct NES *nes_ptr, float *out, size_t max_samples) {
    NES *nes = (NES *)nes_ptr;
    size_t count = 0;
    while (count < max_samples && apu_samples_available(&nes->apu) > 0) {
        out[count++] = apu_sample_output(&nes->apu);
    }
    return count;
}
