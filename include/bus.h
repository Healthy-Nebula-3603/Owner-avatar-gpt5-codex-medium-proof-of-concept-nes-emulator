#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

struct NES;
struct CPU6502;
struct PPU;
struct APU;
struct Mapper;

uint8_t nes_cpu_read(struct NES *nes, uint16_t addr);
void nes_cpu_write(struct NES *nes, uint16_t addr, uint8_t value);

uint8_t nes_ppu_read(struct NES *nes, uint16_t addr);
void nes_ppu_write(struct NES *nes, uint16_t addr, uint8_t value);

void nes_mapper_cpu_step(struct NES *nes);
void nes_mapper_signal_scanline(struct NES *nes);
bool nes_mapper_irq_pending(struct NES *nes);

#endif /* BUS_H */
