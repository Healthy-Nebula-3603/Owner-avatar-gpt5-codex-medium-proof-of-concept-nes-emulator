#ifndef NES_INTERNAL_H
#define NES_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "apu.h"
#include "cartridge.h"
#include "controller.h"
#include "cpu.h"
#include "nes.h"
#include "ppu.h"

typedef struct NES {
    NESConfig config;
    Cartridge cart;
    CPU6502 cpu;
    PPU ppu;
    APU apu;
    Controller controllers[2];
    uint8_t cpu_ram[2048];
    bool dma_active;
    uint16_t dma_base;
    NESFrameInfo frame_info;
    bool rom_loaded;
} NES;

#endif /* NES_INTERNAL_H */
