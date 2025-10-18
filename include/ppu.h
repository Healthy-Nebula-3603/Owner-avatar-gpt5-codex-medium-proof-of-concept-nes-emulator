#ifndef PPU_H
#define PPU_H

#include <stdbool.h>
#include <stdint.h>

struct NES;

typedef struct PPU {
    struct NES *nes;

    uint8_t palette_ram[32];

    uint8_t oam[256];
    uint8_t secondary_oam[32];

    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oam_addr;

    uint16_t v;
    uint16_t t;
    uint8_t fine_x;
    bool write_toggle;

    int16_t scanline;
    int16_t cycle;
    uint64_t frame;

    bool nmi_occured;
    bool nmi_output;
    bool sprite_zero_hit;
    bool sprite_overflow;

    uint8_t nametable_byte;
    uint8_t attribute_byte;
    uint8_t tile_low;
    uint8_t tile_high;
    uint16_t tile_shift_lo;
    uint16_t tile_shift_hi;
    uint8_t attrib_latch;
    uint16_t attrib_shift_lo;
    uint16_t attrib_shift_hi;

    uint8_t vram[0x1000];

    uint8_t buffered_data;

    uint8_t sprite_patterns_lo[8];
    uint8_t sprite_patterns_hi[8];
    uint8_t sprite_positions[8];
    uint8_t sprite_priorities[8];
    uint8_t sprite_indices[8];
    uint8_t sprite_count;
    bool sprite_zero_in_line;

    bool odd_frame;

    uint32_t framebuffer[256 * 240];
    bool frame_complete;
} PPU;

void ppu_reset(PPU *ppu, bool hard_reset);
void ppu_step(PPU *ppu);

uint8_t ppu_read_register(PPU *ppu, uint16_t addr);
void ppu_write_register(PPU *ppu, uint16_t addr, uint8_t value);

uint8_t ppu_cpu_peek(const PPU *ppu, uint16_t addr);
void ppu_increment_x(PPU *ppu);
void ppu_increment_y(PPU *ppu);

uint8_t ppu_palette_read(const PPU *ppu, uint16_t index);
void ppu_palette_write(PPU *ppu, uint16_t index, uint8_t value);
void ppu_oam_dma_write(PPU *ppu, uint8_t index, uint8_t value);

const uint32_t *ppu_framebuffer(const PPU *ppu);
bool ppu_frame_ready(const PPU *ppu);
void ppu_clear_frame_ready(PPU *ppu);

#endif /* PPU_H */
