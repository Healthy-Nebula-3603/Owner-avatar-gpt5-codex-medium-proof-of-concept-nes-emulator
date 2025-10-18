#include "mapper.h"

#include <stdlib.h>
#include <string.h>

#include "nes_internal.h"
#include "util.h"

#define CHR_ADDR_MASK(cart) ((cart)->chr_rom_size - 1)
#define PRG_ADDR_MASK(cart) ((cart)->prg_rom_size - 1)

typedef struct MapperNROM {
    uint8_t prg_bank_count;
} MapperNROM;

typedef struct MapperUXROM {
    uint8_t bank_select;
    uint8_t bank_mask;
} MapperUXROM;

typedef struct MapperCNROM {
    uint8_t chr_bank;
    uint8_t chr_mask;
} MapperCNROM;

typedef struct MapperMMC1 {
    uint8_t shift_reg;
    uint8_t shift_count;
    uint8_t control;
    uint8_t prg_bank;
    uint8_t chr_bank0;
    uint8_t chr_bank1;
} MapperMMC1;

typedef struct MapperMMC3 {
    uint8_t bank_select;
    uint8_t bank_data[8];
    uint8_t prg_mode;
    uint8_t chr_mode;
    uint8_t irq_latch;
    uint8_t irq_counter;
    bool irq_reload;
    bool irq_enable;
    bool irq_pending;
} MapperMMC3;

static uint8_t prg_read_ram(Cartridge *cart, uint16_t addr) {
    if (cart->prg_ram && cart->prg_ram_size > 0) {
        return cart->prg_ram[(addr - 0x6000) % cart->prg_ram_size];
    }
    return 0;
}

static void prg_write_ram(Cartridge *cart, uint16_t addr, uint8_t value) {
    if (cart->prg_ram && cart->prg_ram_size > 0) {
        cart->prg_ram[(addr - 0x6000) % cart->prg_ram_size] = value;
    }
}

/* -------------------- NROM -------------------- */

static uint8_t nrom_prg_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    if (addr >= 0x8000) {
        uint32_t offset = addr - 0x8000;
        if (cart->prg_rom_size == 0x4000) {
            offset %= 0x4000;
        } else {
            offset %= cart->prg_rom_size;
        }
        return cart->prg_rom[offset];
    }
    if (addr >= 0x6000) {
        return prg_read_ram(cart, addr);
    }
    return 0;
}

static void nrom_prg_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        prg_write_ram(mapper->cart, addr, value);
    }
    NES_UNUSED(value);
}

static uint8_t nrom_chr_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    if (cart->chr_rom_size == 0) {
        return 0;
    }
    return cart->chr_rom[addr % cart->chr_rom_size];
}

static void nrom_chr_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    Cartridge *cart = mapper->cart;
    if (cart->has_chr_ram && cart->chr_rom) {
        cart->chr_rom[addr % cart->chr_rom_size] = value;
    }
    NES_UNUSED(value);
}

static void mapper_default_destroy(Mapper *mapper) {
    free(mapper->impl);
    free(mapper);
}

static Mapper *mapper_create_nrom(Cartridge *cart) {
    Mapper *mapper = calloc(1, sizeof(Mapper));
    if (!mapper) {
        return NULL;
    }
    MapperNROM *state = calloc(1, sizeof(MapperNROM));
    if (!state) {
        free(mapper);
        return NULL;
    }
    state->prg_bank_count = (uint8_t)(cart->prg_rom_size / 0x4000);
    if (state->prg_bank_count == 0) {
        state->prg_bank_count = 1;
    }
    mapper->cart = cart;
    mapper->impl = state;
    mapper->prg_read = nrom_prg_read;
    mapper->prg_write = nrom_prg_write;
    mapper->chr_read = nrom_chr_read;
    mapper->chr_write = nrom_chr_write;
    mapper->destroy = mapper_default_destroy;
    return mapper;
}

/* -------------------- UxROM -------------------- */

static uint8_t uxrom_prg_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    MapperUXROM *state = mapper->impl;
    if (addr >= 0xC000) {
        uint32_t offset = (cart->prg_rom_size - 0x4000) + (addr - 0xC000);
        return cart->prg_rom[offset % cart->prg_rom_size];
    }
    if (addr >= 0x8000) {
        uint8_t bank = state->bank_select & state->bank_mask;
        uint32_t offset = bank * 0x4000 + (addr - 0x8000);
        return cart->prg_rom[offset % cart->prg_rom_size];
    }
    if (addr >= 0x6000) {
        return prg_read_ram(cart, addr);
    }
    return 0;
}

static void uxrom_prg_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    MapperUXROM *state = mapper->impl;
    if (addr >= 0x8000) {
        state->bank_select = value & state->bank_mask;
        return;
    }
    if (addr >= 0x6000) {
        prg_write_ram(mapper->cart, addr, value);
    }
}

static uint8_t uxrom_chr_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    if (cart->chr_rom_size == 0) {
        return 0;
    }
    return cart->chr_rom[addr % cart->chr_rom_size];
}

static void uxrom_chr_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    Cartridge *cart = mapper->cart;
    if (cart->has_chr_ram && cart->chr_rom) {
        cart->chr_rom[addr % cart->chr_rom_size] = value;
    }
    NES_UNUSED(value);
}

static Mapper *mapper_create_uxrom(Cartridge *cart) {
    Mapper *mapper = calloc(1, sizeof(Mapper));
    if (!mapper) return NULL;
    MapperUXROM *state = calloc(1, sizeof(MapperUXROM));
    if (!state) {
        free(mapper);
        return NULL;
    }
    state->bank_mask = (uint8_t)((cart->prg_rom_size / 0x4000) - 1);
    mapper->cart = cart;
    mapper->impl = state;
    mapper->prg_read = uxrom_prg_read;
    mapper->prg_write = uxrom_prg_write;
    mapper->chr_read = uxrom_chr_read;
    mapper->chr_write = uxrom_chr_write;
    mapper->destroy = mapper_default_destroy;
    return mapper;
}

/* -------------------- CNROM -------------------- */

static uint8_t cnrom_prg_read(Mapper *mapper, uint16_t addr) {
    if (addr >= 0x8000) {
        return mapper->cart->prg_rom[(addr - 0x8000) % mapper->cart->prg_rom_size];
    }
    if (addr >= 0x6000) {
        return prg_read_ram(mapper->cart, addr);
    }
    return 0;
}

static void cnrom_prg_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    MapperCNROM *state = mapper->impl;
    if (addr >= 0x8000) {
        state->chr_bank = value & state->chr_mask;
        return;
    }
    if (addr >= 0x6000) {
        prg_write_ram(mapper->cart, addr, value);
    }
}

static uint8_t cnrom_chr_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    MapperCNROM *state = mapper->impl;
    uint32_t bank_offset = (state->chr_bank * 0x2000) % cart->chr_rom_size;
    return cart->chr_rom[(bank_offset + addr) % cart->chr_rom_size];
}

static void cnrom_chr_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    Cartridge *cart = mapper->cart;
    if (cart->has_chr_ram && cart->chr_rom) {
        MapperCNROM *state = mapper->impl;
        uint32_t bank_offset = (state->chr_bank * 0x2000) % cart->chr_rom_size;
        cart->chr_rom[(bank_offset + addr) % cart->chr_rom_size] = value;
    }
    NES_UNUSED(value);
}

static Mapper *mapper_create_cnrom(Cartridge *cart) {
    Mapper *mapper = calloc(1, sizeof(Mapper));
    if (!mapper) return NULL;
    MapperCNROM *state = calloc(1, sizeof(MapperCNROM));
    if (!state) {
        free(mapper);
        return NULL;
    }
    state->chr_mask = (uint8_t)((cart->chr_rom_size / 0x2000) - 1);
    mapper->cart = cart;
    mapper->impl = state;
    mapper->prg_read = cnrom_prg_read;
    mapper->prg_write = cnrom_prg_write;
    mapper->chr_read = cnrom_chr_read;
    mapper->chr_write = cnrom_chr_write;
    mapper->destroy = mapper_default_destroy;
    return mapper;
}

/* -------------------- MMC1 -------------------- */

static void mmc1_update_control(Cartridge *cart, MapperMMC1 *state) {
    switch (state->control & 0x03) {
    case 0:
        cart->mirror = MIRROR_SINGLE_SCREEN_LOWER;
        break;
    case 1:
        cart->mirror = MIRROR_SINGLE_SCREEN_UPPER;
        break;
    case 2:
        cart->mirror = MIRROR_VERTICAL;
        break;
    case 3:
    default:
        cart->mirror = MIRROR_HORIZONTAL;
        break;
    }
}

static uint8_t mmc1_prg_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    MapperMMC1 *state = mapper->impl;
    uint32_t bank_16k_count = cart->prg_rom_size / 0x4000;
    if (addr >= 0x8000) {
        uint8_t mode = (state->control >> 2) & 0x03;
        uint32_t offset = 0;
        switch (mode) {
        case 0:
        case 1: {
            uint8_t bank = (state->prg_bank & 0x0E) % (bank_16k_count ? bank_16k_count : 1);
            offset = bank * 0x4000 + (addr - 0x8000);
            break;
        }
        case 2:
            if (addr < 0xC000) {
                offset = addr - 0x8000;
            } else {
                uint8_t bank = (state->prg_bank & 0x0F) % (bank_16k_count ? bank_16k_count : 1);
                offset = bank * 0x4000 + (addr - 0xC000);
            }
            break;
        case 3:
        default:
            if (addr < 0xC000) {
                uint8_t bank = (state->prg_bank & 0x0F) % (bank_16k_count ? bank_16k_count : 1);
                offset = bank * 0x4000 + (addr - 0x8000);
            } else {
                offset = cart->prg_rom_size - 0x4000 + (addr - 0xC000);
            }
            break;
        }
        return cart->prg_rom[offset % cart->prg_rom_size];
    }
    if (addr >= 0x6000) {
        return prg_read_ram(cart, addr);
    }
    return 0;
}

static void mmc1_prg_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    Cartridge *cart = mapper->cart;
    MapperMMC1 *state = mapper->impl;
    if (addr < 0x8000) {
        if (addr >= 0x6000) {
            prg_write_ram(cart, addr, value);
        }
        return;
    }

    if (value & 0x80) {
        state->shift_reg = 0x10;
        state->shift_count = 0;
        state->control |= 0x0C;
        mmc1_update_control(cart, state);
        return;
    }

    state->shift_reg = (state->shift_reg >> 1) | ((value & 0x01) << 4);
    state->shift_count++;

    if (state->shift_count == 5) {
        uint16_t region = addr & 0x6000;
        if (region == 0x0000) {
            state->control = state->shift_reg & 0x1F;
            mmc1_update_control(cart, state);
        } else if (region == 0x2000) {
            state->chr_bank0 = state->shift_reg & 0x1F;
        } else if (region == 0x4000) {
            state->chr_bank1 = state->shift_reg & 0x1F;
        } else {
            state->prg_bank = state->shift_reg & 0x0F;
        }
        state->shift_reg = 0x10;
        state->shift_count = 0;
    }
}

static uint8_t mmc1_chr_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    MapperMMC1 *state = mapper->impl;
    if (!cart->chr_rom || cart->chr_rom_size == 0) {
        return 0;
    }
    if (state->control & 0x10) {
        uint32_t bank_count = cart->chr_rom_size / 0x1000;
        uint32_t bank = (addr < 0x1000) ? state->chr_bank0 : state->chr_bank1;
        if (bank_count > 0) {
            bank %= bank_count;
        }
        uint32_t offset = bank * 0x1000 + (addr & 0x0FFF);
        return cart->chr_rom[offset % cart->chr_rom_size];
    }
    uint32_t bank = (state->chr_bank0 & 0x1E);
    uint32_t bank_count = cart->chr_rom_size / 0x2000;
    if (bank_count > 0) {
        bank %= bank_count;
    }
    uint32_t offset = bank * 0x1000 + addr;
    return cart->chr_rom[offset % cart->chr_rom_size];
}

static void mmc1_chr_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    Cartridge *cart = mapper->cart;
    if (!cart->has_chr_ram || !cart->chr_rom) {
        return;
    }
    MapperMMC1 *state = mapper->impl;
    if (state->control & 0x10) {
        uint32_t bank_count = cart->chr_rom_size / 0x1000;
        uint32_t bank = (addr < 0x1000) ? state->chr_bank0 : state->chr_bank1;
        if (bank_count > 0) {
            bank %= bank_count;
        }
        uint32_t offset = bank * 0x1000 + (addr & 0x0FFF);
        cart->chr_rom[offset % cart->chr_rom_size] = value;
    } else {
        uint32_t bank = (state->chr_bank0 & 0x1E);
        uint32_t bank_count = cart->chr_rom_size / 0x2000;
        if (bank_count > 0) {
            bank %= bank_count;
        }
        uint32_t offset = bank * 0x1000 + addr;
        cart->chr_rom[offset % cart->chr_rom_size] = value;
    }
}

static Mapper *mapper_create_mmc1(Cartridge *cart) {
    Mapper *mapper = calloc(1, sizeof(Mapper));
    if (!mapper) return NULL;
    MapperMMC1 *state = calloc(1, sizeof(MapperMMC1));
    if (!state) {
        free(mapper);
        return NULL;
    }
    state->shift_reg = 0x10;
    state->control = 0x0C;
    mapper->cart = cart;
    mapper->impl = state;
    mapper->prg_read = mmc1_prg_read;
    mapper->prg_write = mmc1_prg_write;
    mapper->chr_read = mmc1_chr_read;
    mapper->chr_write = mmc1_chr_write;
    mapper->destroy = mapper_default_destroy;
    mmc1_update_control(cart, state);
    return mapper;
}

/* -------------------- MMC3 -------------------- */

static void mmc3_update_prg(Cartridge *cart, MapperMMC3 *state, uint32_t *banks) {
    uint32_t bank_count = cart->prg_rom_size / 0x2000;
    uint32_t last_bank = bank_count - 1;
    uint32_t second_last = last_bank - 1;

    if (state->prg_mode == 0) {
        banks[0] = state->bank_data[6] % bank_count;
        banks[1] = state->bank_data[7] % bank_count;
        banks[2] = second_last;
        banks[3] = last_bank;
    } else {
        banks[0] = second_last;
        banks[1] = state->bank_data[7] % bank_count;
        banks[2] = state->bank_data[6] % bank_count;
        banks[3] = last_bank;
    }
}

static void mmc3_update_chr(Cartridge *cart, MapperMMC3 *state, uint32_t *banks) {
    uint32_t bank_count = cart->chr_rom_size / 0x0400;
    if (bank_count == 0) {
        memset(banks, 0, sizeof(uint32_t) * 8);
        return;
    }
    if (state->chr_mode == 0) {
        banks[0] = (state->bank_data[0] & 0xFE) % (bank_count / 2);
        banks[1] = banks[0] + 1;
        banks[2] = (state->bank_data[1] & 0xFE) % (bank_count / 2);
        banks[3] = banks[2] + 1;
        banks[4] = state->bank_data[2] % bank_count;
        banks[5] = state->bank_data[3] % bank_count;
        banks[6] = state->bank_data[4] % bank_count;
        banks[7] = state->bank_data[5] % bank_count;
    } else {
        banks[4] = (state->bank_data[0] & 0xFE) % (bank_count / 2);
        banks[5] = banks[4] + 1;
        banks[6] = (state->bank_data[1] & 0xFE) % (bank_count / 2);
        banks[7] = banks[6] + 1;
        banks[0] = state->bank_data[2] % bank_count;
        banks[1] = state->bank_data[3] % bank_count;
        banks[2] = state->bank_data[4] % bank_count;
        banks[3] = state->bank_data[5] % bank_count;
    }
}

static uint8_t mmc3_prg_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    MapperMMC3 *state = mapper->impl;
    static uint32_t prg_banks[4];
    mmc3_update_prg(cart, state, prg_banks);

    if (addr >= 0x8000) {
        uint32_t index = (addr - 0x8000) / 0x2000;
        uint32_t offset = (addr & 0x1FFF);
        uint32_t bank = prg_banks[index] % (cart->prg_rom_size / 0x2000);
        return cart->prg_rom[(bank * 0x2000 + offset) % cart->prg_rom_size];
    }
    if (addr >= 0x6000) {
        return prg_read_ram(cart, addr);
    }
    return 0;
}

static void mmc3_prg_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    Cartridge *cart = mapper->cart;
    MapperMMC3 *state = mapper->impl;
    switch (addr & 0xE001) {
    case 0x8000:
        state->bank_select = value & 0x07;
        state->prg_mode = (value >> 6) & 0x01;
        state->chr_mode = (value >> 7) & 0x01;
        break;
    case 0x8001:
        state->bank_data[state->bank_select & 0x07] = value;
        break;
    case 0xA000:
        cart->mirror = (value & 0x01) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        break;
    case 0xC000:
        state->irq_latch = value;
        break;
    case 0xC001:
        state->irq_reload = true;
        break;
    case 0xE000:
        state->irq_enable = false;
        state->irq_pending = false;
        break;
    case 0xE001:
        state->irq_enable = true;
        break;
    default:
        if (addr >= 0x6000 && addr < 0x8000) {
            prg_write_ram(cart, addr, value);
        }
        break;
    }
}

static uint8_t mmc3_chr_read(Mapper *mapper, uint16_t addr) {
    Cartridge *cart = mapper->cart;
    if (cart->chr_rom_size == 0) {
        return 0;
    }
    MapperMMC3 *state = mapper->impl;
    static uint32_t chr_banks[8];
    mmc3_update_chr(cart, state, chr_banks);
    uint32_t index = addr / 0x0400;
    uint32_t offset = addr & 0x03FF;
    uint32_t bank = chr_banks[index] % (cart->chr_rom_size / 0x0400);
    return cart->chr_rom[(bank * 0x0400 + offset) % cart->chr_rom_size];
}

static void mmc3_chr_write(Mapper *mapper, uint16_t addr, uint8_t value) {
    Cartridge *cart = mapper->cart;
    if (!cart->has_chr_ram || !cart->chr_rom) {
        return;
    }
    MapperMMC3 *state = mapper->impl;
    static uint32_t chr_banks[8];
    mmc3_update_chr(cart, state, chr_banks);
    uint32_t index = addr / 0x0400;
    uint32_t offset = addr & 0x03FF;
    uint32_t bank = chr_banks[index] % (cart->chr_rom_size / 0x0400);
    cart->chr_rom[(bank * 0x0400 + offset) % cart->chr_rom_size] = value;
}

static void mmc3_scanline(Mapper *mapper) {
    MapperMMC3 *state = mapper->impl;
    if (state->irq_counter == 0 || state->irq_reload) {
        state->irq_counter = state->irq_latch ? state->irq_latch : 0xFF;
        state->irq_reload = false;
    } else {
        state->irq_counter--;
    }
    if (state->irq_counter == 0 && state->irq_enable) {
        state->irq_pending = true;
    }
}

static bool mmc3_irq_pending(Mapper *mapper) {
    MapperMMC3 *state = mapper->impl;
    return state->irq_pending;
}

static Mapper *mapper_create_mmc3(Cartridge *cart) {
    Mapper *mapper = calloc(1, sizeof(Mapper));
    if (!mapper) return NULL;
    MapperMMC3 *state = calloc(1, sizeof(MapperMMC3));
    if (!state) {
        free(mapper);
        return NULL;
    }
    mapper->cart = cart;
    mapper->impl = state;
    mapper->prg_read = mmc3_prg_read;
    mapper->prg_write = mmc3_prg_write;
    mapper->chr_read = mmc3_chr_read;
    mapper->chr_write = mmc3_chr_write;
    mapper->notify_scanline = mmc3_scanline;
    mapper->irq_pending = mmc3_irq_pending;
    mapper->destroy = mapper_default_destroy;
    return mapper;
}

Mapper *mapper_create(Cartridge *cart) {
    switch (cart->mapper) {
    case MAPPER_NROM:
        return mapper_create_nrom(cart);
    case MAPPER_MMC1:
        return mapper_create_mmc1(cart);
    case MAPPER_UXROM:
        return mapper_create_uxrom(cart);
    case MAPPER_CNROM:
        return mapper_create_cnrom(cart);
    case MAPPER_MMC3:
        return mapper_create_mmc3(cart);
    default:
        return NULL;
    }
}
