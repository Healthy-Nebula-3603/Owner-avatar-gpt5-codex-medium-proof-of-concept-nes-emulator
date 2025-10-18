#include "ppu.h"

#include <string.h>

#include "bus.h"
#include "nes.h"
#include "nes_internal.h"
#include "util.h"

typedef struct SpriteFetch {
    uint8_t y;
    uint8_t tile;
    uint8_t attr;
    uint8_t x;
} SpriteFetch;

static const uint32_t ppu_palettes[64] = {
    0x7C7C7CFF, 0x0000FFFF, 0x3F00FFFF, 0x7F00FFFF, 0x9C007CFF, 0xAC0021FF, 0xA70000FF, 0x7F0B00FF,
    0x432F00FF, 0x004700FF, 0x005100FF, 0x003F17FF, 0x1B3F5FFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xBCBCBCFF, 0x0073FFFF, 0x335CFFFF, 0x8E3CFFFF, 0xB837B7FF, 0xC7374EFF, 0xC43815FF, 0xA54C00FF,
    0x6F5F00FF, 0x137400FF, 0x007D06FF, 0x007852FF, 0x00679DFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0x38B8FFFF, 0x5889FFFF, 0xA879FFFF, 0xF36AFFFF, 0xFF6E95FF, 0xFF7D59FF, 0xEA9E22FF,
    0xB9B000FF, 0x6BC100FF, 0x2CCB42FF, 0x00C98FFF, 0x00B5DFFF, 0x3A3A3AFF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0xA6E1FFFF, 0xC3D2FFFF, 0xE4C8FFFF, 0xFBC5FFFF, 0xFFC5EAFF, 0xFFCDBFFF, 0xF8D6A5FF,
    0xE4E18BFF, 0xC5EA8AFF, 0xA5F4A6FF, 0x7FF8CEFF, 0x7FE4FFFF, 0xB8B8B8FF, 0x000000FF, 0x000000FF
};

static inline bool ppu_rendering_enabled(const PPU *ppu) {
    return (ppu->mask & 0x18) != 0;
}

static inline bool ppu_show_background(const PPU *ppu) {
    return (ppu->mask & 0x08) != 0;
}

static inline bool ppu_show_sprites(const PPU *ppu) {
    return (ppu->mask & 0x10) != 0;
}

static inline bool ppu_show_background_left(const PPU *ppu) {
    return (ppu->mask & 0x02) != 0;
}

static inline bool ppu_show_sprites_left(const PPU *ppu) {
    return (ppu->mask & 0x04) != 0;
}

static inline uint16_t background_table_address(const PPU *ppu) {
    return (ppu->ctrl & 0x10) ? 0x1000 : 0x0000;
}

static inline uint16_t sprite_table_address(const PPU *ppu) {
    return (ppu->ctrl & 0x08) ? 0x1000 : 0x0000;
}

static inline uint8_t sprite_height(const PPU *ppu) {
    return (ppu->ctrl & 0x20) ? 16 : 8;
}

static void update_shifters(PPU *ppu) {
    if (ppu_show_background(ppu)) {
        ppu->tile_shift_lo <<= 1;
        ppu->tile_shift_hi <<= 1;
        ppu->attrib_shift_lo <<= 1;
        ppu->attrib_shift_hi <<= 1;
    }

    if (ppu_show_sprites(ppu) && ppu->cycle >= 1 && ppu->cycle <= 256) {
        for (uint8_t i = 0; i < ppu->sprite_count; ++i) {
            if (ppu->sprite_positions[i] > 0) {
                ppu->sprite_positions[i]--;
            } else {
                ppu->sprite_patterns_lo[i] <<= 1;
                ppu->sprite_patterns_hi[i] <<= 1;
            }
        }
    }
}

static void reload_background_shifters(PPU *ppu) {
    ppu->tile_shift_lo = (uint16_t)((ppu->tile_shift_lo & 0xFF00) | ppu->tile_low);
    ppu->tile_shift_hi = (uint16_t)((ppu->tile_shift_hi & 0xFF00) | ppu->tile_high);
    uint16_t attrib = (uint16_t)((ppu->attrib_latch & 0x01) ? 0xFF : 0x00);
    ppu->attrib_shift_lo = (uint16_t)((ppu->attrib_shift_lo & 0xFF00) | attrib);
    attrib = (uint16_t)((ppu->attrib_latch & 0x02) ? 0xFF : 0x00);
    ppu->attrib_shift_hi = (uint16_t)((ppu->attrib_shift_hi & 0xFF00) | attrib);
}

static void fetch_background_data(PPU *ppu, uint16_t addr) {
    switch ((ppu->cycle - 1) & 0x07) {
    case 0:
        reload_background_shifters(ppu);
        ppu->nametable_byte = nes_ppu_read(ppu->nes, 0x2000 | (ppu->v & 0x0FFF));
        break;
    case 2: {
        uint16_t attribute_addr = 0x23C0 | (ppu->v & 0x0C00) | ((ppu->v >> 4) & 0x38) | ((ppu->v >> 2) & 0x07);
        ppu->attribute_byte = nes_ppu_read(ppu->nes, attribute_addr);
        uint16_t coarse_x = ppu->v & 0x001F;
        uint16_t coarse_y = (ppu->v >> 5) & 0x001F;
        uint8_t shift = (uint8_t)(((coarse_y & 0x02) << 1) | (coarse_x & 0x02));
        ppu->attrib_latch = (uint8_t)((ppu->attribute_byte >> shift) & 0x03);
        break;
    }
    case 4: {
        uint16_t pt_addr = background_table_address(ppu) + ((uint16_t)ppu->nametable_byte * 16) + ((ppu->v >> 12) & 0x7);
        ppu->tile_low = nes_ppu_read(ppu->nes, pt_addr);
        break;
    }
    case 6: {
        uint16_t pt_addr = background_table_address(ppu) + ((uint16_t)ppu->nametable_byte * 16) + ((ppu->v >> 12) & 0x7);
        ppu->tile_high = nes_ppu_read(ppu->nes, (uint16_t)(pt_addr + 8));
        break;
    }
    case 7:
        ppu_increment_x(ppu);
        break;
    default:
        NES_UNUSED(addr);
        break;
    }
}

static void copy_horizontal(PPU *ppu) {
    if (ppu_rendering_enabled(ppu)) {
        ppu->v = (uint16_t)((ppu->v & 0x7BE0) | (ppu->t & 0x041F));
    }
}

static void copy_vertical(PPU *ppu) {
    if (ppu_rendering_enabled(ppu)) {
        ppu->v = (uint16_t)((ppu->v & 0x041F) | (ppu->t & 0x7BE0));
    }
}

static void evaluate_sprites(PPU *ppu, int next_scanline) {
    ppu->sprite_count = 0;
    ppu->sprite_zero_in_line = false;

    if (next_scanline >= 240) {
        return;
    }

    uint8_t height = sprite_height(ppu);
    for (uint8_t i = 0; i < 64; ++i) {
        uint8_t y = ppu->oam[i * 4 + 0];
        int16_t diff = (int16_t)next_scanline - (int16_t)y;
        if (diff >= 0 && diff < height) {
            if (ppu->sprite_count < 8) {
                ppu->sprite_patterns_lo[ppu->sprite_count] = 0;
                ppu->sprite_patterns_hi[ppu->sprite_count] = 0;
                ppu->sprite_positions[ppu->sprite_count] = ppu->oam[i * 4 + 3];
                ppu->sprite_priorities[ppu->sprite_count] = ppu->oam[i * 4 + 2];
                ppu->sprite_indices[ppu->sprite_count] = i;
                if (i == 0) {
                    ppu->sprite_zero_in_line = true;
                }

                uint8_t tile_index = ppu->oam[i * 4 + 1];
                uint8_t attr = ppu->oam[i * 4 + 2];
                bool flip_vertical = (attr & 0x80) != 0;
                bool flip_horizontal = (attr & 0x40) != 0;

                uint16_t row = (uint16_t)diff;
                if (flip_vertical) {
                    row = (uint16_t)(height - 1 - row);
                }

                uint16_t addr;
                if (height == 16) {
                    uint8_t bank = tile_index & 0x01;
                    tile_index &= 0xFE;
                    if (row >= 8) {
                        tile_index++;
                        row -= 8;
                    }
                    addr = (uint16_t)(bank * 0x1000 + tile_index * 16 + row);
                } else {
                    addr = (uint16_t)(sprite_table_address(ppu) + tile_index * 16 + row);
                }

                uint8_t lo = nes_ppu_read(ppu->nes, addr);
                uint8_t hi = nes_ppu_read(ppu->nes, (uint16_t)(addr + 8));

                if (flip_horizontal) {
                    lo = (uint8_t)(((lo & 0x01) << 7) | ((lo & 0x02) << 5) | ((lo & 0x04) << 3) | ((lo & 0x08) << 1) |
                                   ((lo & 0x10) >> 1) | ((lo & 0x20) >> 3) | ((lo & 0x40) >> 5) | ((lo & 0x80) >> 7));
                    hi = (uint8_t)(((hi & 0x01) << 7) | ((hi & 0x02) << 5) | ((hi & 0x04) << 3) | ((hi & 0x08) << 1) |
                                   ((hi & 0x10) >> 1) | ((hi & 0x20) >> 3) | ((hi & 0x40) >> 5) | ((hi & 0x80) >> 7));
                }

                ppu->sprite_patterns_lo[ppu->sprite_count] = lo;
                ppu->sprite_patterns_hi[ppu->sprite_count] = hi;

                ppu->sprite_count++;
            } else {
                ppu->status |= 0x20;
                break;
            }
        }
    }
}

static uint8_t background_pixel(const PPU *ppu, uint8_t *palette) {
    if (!ppu_show_background(ppu)) {
        *palette = 0;
        return 0;
    }
    uint16_t bit = (uint16_t)(0x8000 >> ppu->fine_x);
    uint8_t p0 = (ppu->tile_shift_lo & bit) ? 1 : 0;
    uint8_t p1 = (ppu->tile_shift_hi & bit) ? 1 : 0;
    uint8_t bg = (uint8_t)((p1 << 1) | p0);
    uint8_t a0 = (ppu->attrib_shift_lo & bit) ? 1 : 0;
    uint8_t a1 = (ppu->attrib_shift_hi & bit) ? 1 : 0;
    *palette = (uint8_t)((a1 << 1) | a0);
    return bg;
}

static uint8_t sprite_pixel(PPU *ppu, uint8_t *palette, bool *priority, bool *zero) {
    *palette = 0;
    *priority = false;
    *zero = false;

    if (!ppu_show_sprites(ppu)) {
        return 0;
    }

    for (uint8_t i = 0; i < ppu->sprite_count; ++i) {
        if (ppu->sprite_positions[i] != 0) {
            continue;
        }
        uint8_t lo = (ppu->sprite_patterns_lo[i] & 0x80) ? 1 : 0;
        uint8_t hi = (ppu->sprite_patterns_hi[i] & 0x80) ? 1 : 0;
        uint8_t px = (uint8_t)((hi << 1) | lo);
        if (px != 0) {
            *palette = (uint8_t)((ppu->sprite_priorities[i] & 0x03) + 0x04);
            *priority = (ppu->sprite_priorities[i] & 0x20) == 0;
            *zero = (ppu->sprite_indices[i] == 0);
            return px;
        }
    }
    return 0;
}

void ppu_reset(PPU *ppu, bool hard_reset) {
    struct NES *nes = ppu->nes;
    memset(ppu, 0, sizeof(*ppu));
    ppu->nes = nes;
    if (hard_reset) {
        memset(ppu->oam, 0xFF, sizeof(ppu->oam));
        memset(ppu->vram, 0x00, sizeof(ppu->vram));
        memset(ppu->palette_ram, 0x00, sizeof(ppu->palette_ram));
    }
    ppu->scanline = -1;
    ppu->cycle = 0;
    ppu->frame = 0;
    ppu->status = 0;
    ppu->ctrl = 0;
    ppu->mask = 0;
    ppu->v = 0;
    ppu->t = 0;
    ppu->fine_x = 0;
    ppu->write_toggle = false;
    ppu->odd_frame = false;
}

void ppu_step(PPU *ppu) {
    bool rendering = ppu_rendering_enabled(ppu);

    if (ppu->scanline == -1 && ppu->cycle == 1) {
        ppu->status &= (uint8_t)~0x80;
        ppu->status &= (uint8_t)~0x40;
        ppu->status &= (uint8_t)~0x20;
    }

    if (ppu->scanline >= 0 && ppu->scanline < 240 && ppu->cycle >= 1 && ppu->cycle <= 256) {
        uint8_t bg_palette = 0;
        uint8_t bg_pixel = background_pixel(ppu, &bg_palette);
        uint8_t sprite_palette = 0;
        bool sprite_priority = false;
        bool sprite_zero = false;
        uint8_t spr_pixel = sprite_pixel(ppu, &sprite_palette, &sprite_priority, &sprite_zero);

        bool render_bg_left = ppu_show_background_left(ppu) || ppu->cycle > 8;
        bool render_spr_left = ppu_show_sprites_left(ppu) || ppu->cycle > 8;
        if (!render_bg_left) {
            bg_pixel = 0;
        }
        if (!render_spr_left) {
            spr_pixel = 0;
        }

        uint16_t palette_addr = 0x3F00;
        if (bg_pixel == 0 && spr_pixel == 0) {
            palette_addr = 0x3F00;
        } else if (bg_pixel == 0 && spr_pixel > 0) {
            palette_addr = (uint16_t)(0x3F10 + sprite_palette * 4 + spr_pixel);
        } else if (bg_pixel > 0 && spr_pixel == 0) {
            palette_addr = (uint16_t)(0x3F00 + bg_palette * 4 + bg_pixel);
        } else {
            if (sprite_priority) {
                palette_addr = (uint16_t)(0x3F10 + sprite_palette * 4 + spr_pixel);
            } else {
                palette_addr = (uint16_t)(0x3F00 + bg_palette * 4 + bg_pixel);
            }
            if (sprite_zero && ppu->sprite_zero_in_line && ppu->scanline >= 0 && ppu->cycle - 1 < 255) {
                ppu->status |= 0x40;
            }
        }

        uint8_t color_index = ppu_palette_read(ppu, (uint16_t)(palette_addr & 0x1F));
        ppu->framebuffer[ppu->scanline * 256 + (ppu->cycle - 1)] = ppu_palettes[color_index & 0x3F];
    }

    if ((ppu->cycle >= 2 && ppu->cycle <= 257) || (ppu->cycle >= 321 && ppu->cycle <= 337)) {
        update_shifters(ppu);
        if (rendering) {
            fetch_background_data(ppu, 0);
        }
    }

    if (rendering) {
        if (ppu->cycle == 256) {
            ppu_increment_y(ppu);
        }
        if (ppu->cycle == 257) {
            ppu->tile_shift_lo = 0;
            ppu->tile_shift_hi = 0;
            ppu->attrib_shift_lo = 0;
            ppu->attrib_shift_hi = 0;
            copy_horizontal(ppu);
            evaluate_sprites(ppu, ppu->scanline + 1);
        }
        if (ppu->cycle == 338 || ppu->cycle == 340) {
            ppu->nametable_byte = nes_ppu_read(ppu->nes, 0x2000 | (ppu->v & 0x0FFF));
        }
        if (ppu->scanline == -1 && ppu->cycle >= 280 && ppu->cycle <= 304) {
            copy_vertical(ppu);
        }
    }

    if (ppu->cycle == 1) {
        if (ppu->scanline >= 0 && ppu->scanline < 240) {
            ppu->frame_complete = false;
        }
    }

    if (ppu->scanline == 241 && ppu->cycle == 1) {
        ppu->status |= 0x80;
        if (ppu->ctrl & 0x80) {
            cpu_trigger_nmi(&ppu->nes->cpu);
        }
    }

    if (ppu->scanline == 239 && ppu->cycle == 340) {
        ppu->frame_complete = true;
    }

    ppu->cycle++;

    bool visible_scanline = (ppu->scanline >= 0 && ppu->scanline < 240);

    if (ppu->cycle > 340) {
        ppu->cycle = 0;
        ppu->scanline++;
        if (ppu->scanline > 261) {
            ppu->scanline = -1;
            ppu->frame++;
            ppu->odd_frame = !ppu->odd_frame;
        }
        if (visible_scanline) {
            nes_mapper_signal_scanline(ppu->nes);
        }
    }

    if (ppu->scanline == -1 && ppu->cycle == 0 && rendering && ppu->odd_frame) {
        ppu->cycle = 1;
    }
}

uint8_t ppu_read_register(PPU *ppu, uint16_t addr) {
    addr = 0x2000 | (addr & 0x0007);
    switch (addr) {
    case 0x2002: {
        uint8_t value = (uint8_t)(ppu->status & 0xE0);
        value |= (uint8_t)(ppu->buffered_data & 0x1F);
        ppu->status &= (uint8_t)~0x80;
        ppu->write_toggle = false;
        return value;
    }
    case 0x2004:
        return ppu->oam[ppu->oam_addr];
    case 0x2007: {
        uint16_t addr_v = ppu->v & 0x3FFF;
        uint8_t value = nes_ppu_read(ppu->nes, addr_v);
        uint8_t ret = value;
        if (addr_v < 0x3F00) {
            ret = ppu->buffered_data;
            ppu->buffered_data = value;
        } else {
            ppu->buffered_data = value;
        }
        ppu->v += (uint16_t)((ppu->ctrl & 0x04) ? 32 : 1);
        return ret;
    }
    default:
        break;
    }
    return 0;
}

void ppu_write_register(PPU *ppu, uint16_t addr, uint8_t value) {
    addr = 0x2000 | (addr & 0x0007);
    switch (addr) {
    case 0x2000: {
        bool previous_nmi = (ppu->ctrl & 0x80) != 0;
        ppu->ctrl = value;
        ppu->t = (uint16_t)((ppu->t & 0xF3FF) | ((value & 0x03) << 10));
        bool new_nmi = (value & 0x80) != 0;
        if (!previous_nmi && new_nmi && (ppu->status & 0x80)) {
            cpu_trigger_nmi(&ppu->nes->cpu);
        }
        break;
    }
    case 0x2001:
        ppu->mask = value;
        break;
    case 0x2003:
        ppu->oam_addr = value;
        break;
    case 0x2004:
        ppu->oam[ppu->oam_addr++] = value;
        break;
    case 0x2005:
        if (!ppu->write_toggle) {
            ppu->fine_x = (uint8_t)(value & 0x07);
            ppu->t = (uint16_t)((ppu->t & 0xFFE0) | (value >> 3));
            ppu->write_toggle = true;
        } else {
            ppu->t = (uint16_t)((ppu->t & 0x8FFF) | ((value & 0x07) << 12));
            ppu->t = (uint16_t)((ppu->t & 0xFC1F) | ((value & 0xF8) << 2));
            ppu->t &= 0x7FFF;
            ppu->write_toggle = false;
        }
        break;
    case 0x2006:
        if (!ppu->write_toggle) {
            ppu->t = (uint16_t)((ppu->t & 0x00FF) | ((value & 0x3F) << 8));
            ppu->write_toggle = true;
        } else {
            ppu->t = (uint16_t)((ppu->t & 0xFF00) | value);
            ppu->t &= 0x3FFF;
            ppu->v = ppu->t;
            ppu->write_toggle = false;
        }
        break;
    case 0x2007:
        nes_ppu_write(ppu->nes, ppu->v & 0x3FFF, value);
        ppu->v += (uint16_t)((ppu->ctrl & 0x04) ? 32 : 1);
        break;
    default:
        break;
    }
}

uint8_t ppu_cpu_peek(const PPU *ppu, uint16_t addr) {
    return nes_ppu_read(ppu->nes, addr & 0x3FFF);
}

void ppu_increment_x(PPU *ppu) {
    if (!ppu_rendering_enabled(ppu)) {
        return;
    }
    if ((ppu->v & 0x001F) == 31) {
        ppu->v &= (uint16_t)~0x001F;
        ppu->v ^= 0x0400;
    } else {
        ppu->v += 1;
    }
}

void ppu_increment_y(PPU *ppu) {
    if (!ppu_rendering_enabled(ppu)) {
        return;
    }
    if ((ppu->v & 0x7000) != 0x7000) {
        ppu->v += 0x1000;
    } else {
        ppu->v &= (uint16_t)~0x7000;
        uint16_t y = (ppu->v & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            ppu->v ^= 0x0800;
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        ppu->v = (uint16_t)((ppu->v & ~0x03E0) | (y << 5));
    }
}

const uint32_t *ppu_framebuffer(const PPU *ppu) {
    return ppu->framebuffer;
}

bool ppu_frame_ready(const PPU *ppu) {
    return ppu->frame_complete;
}

void ppu_clear_frame_ready(PPU *ppu) {
    ppu->frame_complete = false;
}

uint8_t ppu_palette_read(const PPU *ppu, uint16_t index) {
    index &= 0x1F;
    if (index == 0x10 || index == 0x14 || index == 0x18 || index == 0x1C) {
        index -= 0x10;
    }
    return ppu->palette_ram[index & 0x1F];
}

void ppu_palette_write(PPU *ppu, uint16_t index, uint8_t value) {
    index &= 0x1F;
    if (index == 0x10 || index == 0x14 || index == 0x18 || index == 0x1C) {
        index -= 0x10;
    }
    ppu->palette_ram[index & 0x1F] = value;
}

void ppu_oam_dma_write(PPU *ppu, uint8_t index, uint8_t value) {
    ppu->oam[index] = value;
}
