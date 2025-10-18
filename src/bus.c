#include "bus.h"

#include <string.h>

#include "apu.h"
#include "cartridge.h"
#include "controller.h"
#include "mapper.h"
#include "nes.h"
#include "nes_internal.h"
#include "ppu.h"
#include "util.h"

uint8_t nes_cpu_read(struct NES *nes_ptr, uint16_t addr) {
    NES *nes = (NES *)nes_ptr;

    if (addr < 0x2000) {
        return nes->cpu_ram[addr & 0x07FF];
    }

    if (addr < 0x4000) {
        uint16_t reg = 0x2000 | (addr & 0x0007);
        return ppu_read_register(&nes->ppu, reg);
    }

    if (addr >= 0x4000 && addr <= 0x4013) {
        /* APU registers are write-only; return open bus */
        return 0;
    }

    switch (addr) {
    case 0x4014:
        return 0; /* write-only */
    case 0x4015:
        return apu_read_status(&nes->apu);
    case 0x4016: {
        uint8_t value = controller_read(&nes->controllers[0]);
        value |= 0x40; /* upper bits high */
        return value;
    }
    case 0x4017: {
        uint8_t value = controller_read(&nes->controllers[1]);
        value |= 0x40;
        return value;
    }
    default:
        break;
    }

    if (addr >= 0x4018 && addr < 0x4020) {
        return 0;
    }

    if (nes->cart.mapper_impl && nes->cart.mapper_impl->prg_read) {
        return nes->cart.mapper_impl->prg_read(nes->cart.mapper_impl, addr);
    }
    return 0;
}

static void perform_dma(NES *nes, uint8_t page) {
    uint16_t base = (uint16_t)(page << 8);
    for (uint16_t i = 0; i < 256; ++i) {
        uint8_t value = nes_cpu_read((struct NES *)nes, (uint16_t)(base + i));
        ppu_oam_dma_write(&nes->ppu, (uint8_t)i, value);
    }
    nes->cpu.stall_cycles += 513 + (nes->cpu.cycles & 1);
}

void nes_cpu_write(struct NES *nes_ptr, uint16_t addr, uint8_t value) {
    NES *nes = (NES *)nes_ptr;

    if (addr < 0x2000) {
        nes->cpu_ram[addr & 0x07FF] = value;
        return;
    }

    if (addr < 0x4000) {
        uint16_t reg = 0x2000 | (addr & 0x0007);
        ppu_write_register(&nes->ppu, reg, value);
        return;
    }

    if (addr >= 0x4000 && addr <= 0x4013) {
        apu_write_register(&nes->apu, addr, value);
        return;
    }

    switch (addr) {
    case 0x4014:
        perform_dma(nes, value);
        return;
    case 0x4015:
        apu_write_register(&nes->apu, addr, value);
        return;
    case 0x4016:
        controller_write(&nes->controllers[0], value);
        controller_write(&nes->controllers[1], value);
        return;
    case 0x4017:
        apu_write_register(&nes->apu, addr, value);
        return;
    default:
        break;
    }

    if (addr >= 0x4018 && addr < 0x4020) {
        return;
    }

    if (nes->cart.mapper_impl && nes->cart.mapper_impl->prg_write) {
        nes->cart.mapper_impl->prg_write(nes->cart.mapper_impl, addr, value);
    }
}

uint8_t nes_ppu_read(struct NES *nes_ptr, uint16_t addr) {
    NES *nes = (NES *)nes_ptr;
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        if (nes->cart.mapper_impl && nes->cart.mapper_impl->chr_read) {
            return nes->cart.mapper_impl->chr_read(nes->cart.mapper_impl, addr);
        }
        return 0;
    }

    if (addr < 0x3F00) {
        uint16_t mirrored = addr & 0x0FFF;
        uint16_t table = mirrored % 0x1000;
        uint16_t offset = table & 0x03FF;
        unsigned nt_index = (table >> 10) & 0x03;
        unsigned resolved = 0;
        switch (nes->cart.mirror) {
        case MIRROR_HORIZONTAL:
            resolved = (nt_index & 0x01) * 0x0400;
            break;
        case MIRROR_VERTICAL:
            resolved = ((nt_index >> 1) * 0x0400);
            break;
        case MIRROR_FOUR_SCREEN:
            resolved = nt_index * 0x0400;
            break;
        case MIRROR_SINGLE_SCREEN_LOWER:
            resolved = 0;
            break;
        case MIRROR_SINGLE_SCREEN_UPPER:
            resolved = 0x0400;
            break;
        }
        resolved = (resolved + offset) % sizeof(nes->ppu.vram);
        return nes->ppu.vram[resolved];
    }

    return ppu_palette_read(&nes->ppu, addr & 0x1F);
}

void nes_ppu_write(struct NES *nes_ptr, uint16_t addr, uint8_t value) {
    NES *nes = (NES *)nes_ptr;
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        if (nes->cart.mapper_impl && nes->cart.mapper_impl->chr_write) {
            nes->cart.mapper_impl->chr_write(nes->cart.mapper_impl, addr, value);
        }
        return;
    }

    if (addr < 0x3F00) {
        uint16_t mirrored = addr & 0x0FFF;
        uint16_t table = mirrored % 0x1000;
        uint16_t offset = table & 0x03FF;
        unsigned nt_index = (table >> 10) & 0x03;
        unsigned resolved = 0;
        switch (nes->cart.mirror) {
        case MIRROR_HORIZONTAL:
            resolved = (nt_index & 0x01) * 0x0400;
            break;
        case MIRROR_VERTICAL:
            resolved = ((nt_index >> 1) * 0x0400);
            break;
        case MIRROR_FOUR_SCREEN:
            resolved = nt_index * 0x0400;
            break;
        case MIRROR_SINGLE_SCREEN_LOWER:
            resolved = 0;
            break;
        case MIRROR_SINGLE_SCREEN_UPPER:
            resolved = 0x0400;
            break;
        }
        resolved = (resolved + offset) % sizeof(nes->ppu.vram);
        nes->ppu.vram[resolved] = value;
        return;
    }

    ppu_palette_write(&nes->ppu, addr & 0x1F, value);
}

void nes_mapper_cpu_step(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    if (nes->cart.mapper_impl && nes->cart.mapper_impl->cpu_step) {
        nes->cart.mapper_impl->cpu_step(nes->cart.mapper_impl);
    }
}

void nes_mapper_signal_scanline(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    if (nes->cart.mapper_impl && nes->cart.mapper_impl->notify_scanline) {
        nes->cart.mapper_impl->notify_scanline(nes->cart.mapper_impl);
    }
}

bool nes_mapper_irq_pending(struct NES *nes_ptr) {
    NES *nes = (NES *)nes_ptr;
    if (nes->cart.mapper_impl && nes->cart.mapper_impl->irq_pending) {
        return nes->cart.mapper_impl->irq_pending(nes->cart.mapper_impl);
    }
    return false;
}
