#ifndef CPU_H
#define CPU_H

#include <stdbool.h>
#include <stdint.h>

struct NES;

typedef struct CPU6502 {
    uint16_t pc;
    uint8_t sp;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t status;
    uint64_t cycles;
    uint64_t stall_cycles;
    bool nmi_pending;
    bool irq_pending;
    struct NES *nes;
} CPU6502;

void cpu_reset(CPU6502 *cpu, bool hard_reset);
uint8_t cpu_step(CPU6502 *cpu);
void cpu_trigger_nmi(CPU6502 *cpu);
void cpu_set_irq_line(CPU6502 *cpu, bool asserted);

#endif /* CPU_H */
