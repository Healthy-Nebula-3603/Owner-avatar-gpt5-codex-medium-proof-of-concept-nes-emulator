#include "cpu.h"

#include <limits.h>
#include <string.h>

#include "bus.h"
#include "nes_internal.h"
#include "util.h"

enum {
    FLAG_C = 1 << 0,
    FLAG_Z = 1 << 1,
    FLAG_I = 1 << 2,
    FLAG_D = 1 << 3,
    FLAG_B = 1 << 4,
    FLAG_U = 1 << 5,
    FLAG_V = 1 << 6,
    FLAG_N = 1 << 7,
};

typedef enum {
    ADDR_IMP,
    ADDR_ACC,
    ADDR_IMM,
    ADDR_ZP,
    ADDR_ZPX,
    ADDR_ZPY,
    ADDR_REL,
    ADDR_ABS,
    ADDR_ABX,
    ADDR_ABY,
    ADDR_IND,
    ADDR_INX,
    ADDR_INY
} AddressMode;

static inline uint8_t cpu_read(CPU6502 *cpu, uint16_t addr) {
    return nes_cpu_read(cpu->nes, addr);
}

static inline void cpu_write(CPU6502 *cpu, uint16_t addr, uint8_t value) {
    nes_cpu_write(cpu->nes, addr, value);
}

static inline void set_flag(CPU6502 *cpu, uint8_t mask, bool value) {
    if (value) {
        cpu->status |= mask;
    } else {
        cpu->status &= (uint8_t)~mask;
    }
}

static inline bool get_flag(const CPU6502 *cpu, uint8_t mask) {
    return (cpu->status & mask) != 0;
}

static inline void update_zn(CPU6502 *cpu, uint8_t value) {
    set_flag(cpu, FLAG_Z, value == 0);
    set_flag(cpu, FLAG_N, (value & 0x80) != 0);
}

static inline uint8_t pull_byte(CPU6502 *cpu) {
    cpu->sp = (uint8_t)(cpu->sp + 1);
    return cpu_read(cpu, (uint16_t)(0x0100 | cpu->sp));
}

static inline void push_byte(CPU6502 *cpu, uint8_t value) {
    cpu_write(cpu, (uint16_t)(0x0100 | cpu->sp), value);
    cpu->sp = (uint8_t)(cpu->sp - 1);
}

static inline uint8_t fetch_byte(CPU6502 *cpu) {
    uint8_t value = cpu_read(cpu, cpu->pc);
    cpu->pc = (uint16_t)(cpu->pc + 1);
    return value;
}

static inline uint16_t fetch_word(CPU6502 *cpu) {
    uint8_t lo = fetch_byte(cpu);
    uint8_t hi = fetch_byte(cpu);
    return (uint16_t)(lo | (hi << 8));
}

static inline uint16_t read_word(CPU6502 *cpu, uint16_t addr) {
    uint8_t lo = cpu_read(cpu, addr);
    uint8_t hi = cpu_read(cpu, (uint16_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}

static uint16_t addr_mode_fetch(CPU6502 *cpu, AddressMode mode, bool *page_crossed) {
    *page_crossed = false;

    switch (mode) {
    case ADDR_IMM:
        return fetch_byte(cpu);
    case ADDR_ZP:
        return fetch_byte(cpu);
    case ADDR_ZPX: {
        uint8_t base = fetch_byte(cpu);
        return (uint8_t)(base + cpu->x);
    }
    case ADDR_ZPY: {
        uint8_t base = fetch_byte(cpu);
        return (uint8_t)(base + cpu->y);
    }
    case ADDR_ABS:
        return fetch_word(cpu);
    case ADDR_ABX: {
        uint16_t base = fetch_word(cpu);
        uint16_t addr = (uint16_t)(base + cpu->x);
        *page_crossed = ((base & 0xFF00) != (addr & 0xFF00));
        return addr;
    }
    case ADDR_ABY: {
        uint16_t base = fetch_word(cpu);
        uint16_t addr = (uint16_t)(base + cpu->y);
        *page_crossed = ((base & 0xFF00) != (addr & 0xFF00));
        return addr;
    }
    case ADDR_IND: {
        uint16_t ptr = fetch_word(cpu);
        uint16_t hi_addr = (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
        uint8_t lo = cpu_read(cpu, ptr);
        uint8_t hi = cpu_read(cpu, hi_addr);
        return (uint16_t)(lo | (hi << 8));
    }
    case ADDR_INX: {
        uint8_t zp = fetch_byte(cpu);
        uint8_t ptr = (uint8_t)(zp + cpu->x);
        uint8_t lo = cpu_read(cpu, ptr);
        uint8_t hi = cpu_read(cpu, (uint8_t)(ptr + 1));
        return (uint16_t)(lo | (hi << 8));
    }
    case ADDR_INY: {
        uint8_t zp = fetch_byte(cpu);
        uint8_t lo = cpu_read(cpu, zp);
        uint8_t hi = cpu_read(cpu, (uint8_t)(zp + 1));
        uint16_t base = (uint16_t)(lo | (hi << 8));
        uint16_t addr = (uint16_t)(base + cpu->y);
        *page_crossed = ((base & 0xFF00) != (addr & 0xFF00));
        return addr;
    }
    case ADDR_REL: {
        int8_t offset = (int8_t)fetch_byte(cpu);
        return (uint16_t)(cpu->pc + offset);
    }
    case ADDR_IMP:
    case ADDR_ACC:
    default:
        return 0;
    }
}

static uint8_t read_operand(CPU6502 *cpu, AddressMode mode, uint16_t *addr_out, bool *page_crossed) {
    *addr_out = 0;
    bool crossed = false;
    if (mode == ADDR_IMM) {
        uint8_t value = (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &crossed);
        if (page_crossed) {
            *page_crossed = false;
        }
        return value;
    }
    uint16_t addr = addr_mode_fetch(cpu, mode, &crossed);
    *addr_out = addr;
    if (page_crossed) {
        *page_crossed = crossed;
    }
    return cpu_read(cpu, addr);
}

static void branch(CPU6502 *cpu, bool condition, uint16_t target, uint8_t *cycles) {
    if (condition) {
        (*cycles)++;
        if ((cpu->pc & 0xFF00) != (target & 0xFF00)) {
            (*cycles)++;
        }
        cpu->pc = target;
    }
}

static inline uint8_t op_asl(CPU6502 *cpu, uint8_t value) {
    set_flag(cpu, FLAG_C, (value & 0x80) != 0);
    value <<= 1;
    update_zn(cpu, value);
    return value;
}

static inline uint8_t op_lsr(CPU6502 *cpu, uint8_t value) {
    set_flag(cpu, FLAG_C, (value & 0x01) != 0);
    value >>= 1;
    update_zn(cpu, value);
    return value;
}

static inline uint8_t op_rol(CPU6502 *cpu, uint8_t value) {
    bool carry = get_flag(cpu, FLAG_C);
    set_flag(cpu, FLAG_C, (value & 0x80) != 0);
    value = (uint8_t)((value << 1) | (carry ? 1 : 0));
    update_zn(cpu, value);
    return value;
}

static inline uint8_t op_ror(CPU6502 *cpu, uint8_t value) {
    bool carry = get_flag(cpu, FLAG_C);
    set_flag(cpu, FLAG_C, (value & 0x01) != 0);
    value = (uint8_t)((value >> 1) | (carry ? 0x80 : 0x00));
    update_zn(cpu, value);
    return value;
}

static inline void op_adc(CPU6502 *cpu, uint8_t value) {
    uint16_t sum = (uint16_t)cpu->a + value + (get_flag(cpu, FLAG_C) ? 1 : 0);
    set_flag(cpu, FLAG_C, sum > 0xFF);
    uint8_t result = (uint8_t)sum;
    set_flag(cpu, FLAG_V, (~(cpu->a ^ value) & (cpu->a ^ result) & 0x80) != 0);
    cpu->a = result;
    update_zn(cpu, cpu->a);
}

static inline void op_sbc(CPU6502 *cpu, uint8_t value) {
    op_adc(cpu, (uint8_t)(value ^ 0xFF));
}

static inline void op_compare(CPU6502 *cpu, uint8_t reg, uint8_t value) {
    uint16_t tmp = (uint16_t)reg - value;
    set_flag(cpu, FLAG_C, reg >= value);
    update_zn(cpu, (uint8_t)tmp);
}

static inline void op_ora(CPU6502 *cpu, uint8_t value) {
    cpu->a |= value;
    update_zn(cpu, cpu->a);
}

static inline void op_and(CPU6502 *cpu, uint8_t value) {
    cpu->a &= value;
    update_zn(cpu, cpu->a);
}

static inline void op_eor(CPU6502 *cpu, uint8_t value) {
    cpu->a ^= value;
    update_zn(cpu, cpu->a);
}

static inline void op_load(CPU6502 *cpu, uint8_t *reg, uint8_t value) {
    *reg = value;
    update_zn(cpu, *reg);
}

static void handle_nmi(CPU6502 *cpu) {
    cpu->nmi_pending = false;
    push_byte(cpu, (uint8_t)(cpu->pc >> 8));
    push_byte(cpu, (uint8_t)(cpu->pc & 0xFF));
    push_byte(cpu, (uint8_t)((cpu->status | FLAG_B) & ~FLAG_U));
    set_flag(cpu, FLAG_I, true);
    cpu->pc = read_word(cpu, 0xFFFA);
}

static void handle_irq(CPU6502 *cpu) {
    push_byte(cpu, (uint8_t)(cpu->pc >> 8));
    push_byte(cpu, (uint8_t)(cpu->pc & 0xFF));
    push_byte(cpu, (uint8_t)((cpu->status | FLAG_B) & ~FLAG_U));
    set_flag(cpu, FLAG_I, true);
    cpu->pc = read_word(cpu, 0xFFFE);
}

void cpu_reset(CPU6502 *cpu, bool hard_reset) {
    NES_UNUSED(hard_reset);
    cpu->sp = 0xFD;
    cpu->status = FLAG_U | FLAG_I;
    cpu->a = cpu->x = cpu->y = 0;
    cpu->pc = read_word(cpu, 0xFFFC);
    cpu->cycles = 0;
    cpu->stall_cycles = 0;
    cpu->nmi_pending = false;
    cpu->irq_pending = false;
}

uint8_t cpu_step(CPU6502 *cpu) {
    if (cpu->stall_cycles > 0) {
        cpu->stall_cycles--;
        cpu->cycles++;
        return 1;
    }

    if (cpu->nmi_pending) {
        handle_nmi(cpu);
        cpu->cycles += 7;
        return 7;
    }

    if (cpu->irq_pending && !get_flag(cpu, FLAG_I)) {
        handle_irq(cpu);
        cpu->cycles += 7;
        cpu->irq_pending = false;
        return 7;
    }

    uint8_t opcode = fetch_byte(cpu);
    uint8_t cycles = 0;
    uint16_t addr = 0;
    bool page_crossed = false;

    switch (opcode) {
    case 0x00: /* BRK */
        cpu->pc++;
        push_byte(cpu, (uint8_t)(cpu->pc >> 8));
        push_byte(cpu, (uint8_t)(cpu->pc & 0xFF));
        push_byte(cpu, (uint8_t)(cpu->status | FLAG_B));
        set_flag(cpu, FLAG_I, true);
        cpu->pc = read_word(cpu, 0xFFFE);
        cycles = 7;
        break;

    /* ORA */
    case 0x09:
        op_ora(cpu, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0x05:
        op_ora(cpu, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0x15:
        op_ora(cpu, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x0D:
        op_ora(cpu, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x1D:
        op_ora(cpu, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x19:
        op_ora(cpu, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x01:
        op_ora(cpu, read_operand(cpu, ADDR_INX, &addr, &page_crossed));
        cycles = 6;
        break;
    case 0x11:
        op_ora(cpu, read_operand(cpu, ADDR_INY, &addr, &page_crossed));
        cycles = 5 + (page_crossed ? 1 : 0);
        break;

    /* AND */
    case 0x29:
        op_and(cpu, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0x25:
        op_and(cpu, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0x35:
        op_and(cpu, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x2D:
        op_and(cpu, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x3D:
        op_and(cpu, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x39:
        op_and(cpu, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x21:
        op_and(cpu, read_operand(cpu, ADDR_INX, &addr, &page_crossed));
        cycles = 6;
        break;
    case 0x31:
        op_and(cpu, read_operand(cpu, ADDR_INY, &addr, &page_crossed));
        cycles = 5 + (page_crossed ? 1 : 0);
        break;

    /* EOR */
    case 0x49:
        op_eor(cpu, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0x45:
        op_eor(cpu, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0x55:
        op_eor(cpu, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x4D:
        op_eor(cpu, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x5D:
        op_eor(cpu, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x59:
        op_eor(cpu, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x41:
        op_eor(cpu, read_operand(cpu, ADDR_INX, &addr, &page_crossed));
        cycles = 6;
        break;
    case 0x51:
        op_eor(cpu, read_operand(cpu, ADDR_INY, &addr, &page_crossed));
        cycles = 5 + (page_crossed ? 1 : 0);
        break;

    /* ADC */
    case 0x69:
        op_adc(cpu, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0x65:
        op_adc(cpu, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0x75:
        op_adc(cpu, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x6D:
        op_adc(cpu, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0x7D:
        op_adc(cpu, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x79:
        op_adc(cpu, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x61:
        op_adc(cpu, read_operand(cpu, ADDR_INX, &addr, &page_crossed));
        cycles = 6;
        break;
    case 0x71:
        op_adc(cpu, read_operand(cpu, ADDR_INY, &addr, &page_crossed));
        cycles = 5 + (page_crossed ? 1 : 0);
        break;

    /* STA */
    case 0x85:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cpu_write(cpu, addr, cpu->a);
        cycles = 3;
        break;
    case 0x95:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        cpu_write(cpu, addr, cpu->a);
        cycles = 4;
        break;
    case 0x8D:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cpu_write(cpu, addr, cpu->a);
        cycles = 4;
        break;
    case 0x9D:
        addr = addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        cpu_write(cpu, addr, cpu->a);
        cycles = 5;
        break;
    case 0x99:
        addr = addr_mode_fetch(cpu, ADDR_ABY, &page_crossed);
        cpu_write(cpu, addr, cpu->a);
        cycles = 5;
        break;
    case 0x81:
        addr = addr_mode_fetch(cpu, ADDR_INX, &page_crossed);
        cpu_write(cpu, addr, cpu->a);
        cycles = 6;
        break;
    case 0x91:
        addr = addr_mode_fetch(cpu, ADDR_INY, &page_crossed);
        cpu_write(cpu, addr, cpu->a);
        cycles = 6;
        break;

    /* LDA */
    case 0xA9:
        op_load(cpu, &cpu->a, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0xA5:
        op_load(cpu, &cpu->a, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0xB5:
        op_load(cpu, &cpu->a, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xAD:
        op_load(cpu, &cpu->a, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xBD:
        op_load(cpu, &cpu->a, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0xB9:
        op_load(cpu, &cpu->a, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0xA1:
        op_load(cpu, &cpu->a, read_operand(cpu, ADDR_INX, &addr, &page_crossed));
        cycles = 6;
        break;
    case 0xB1:
        op_load(cpu, &cpu->a, read_operand(cpu, ADDR_INY, &addr, &page_crossed));
        cycles = 5 + (page_crossed ? 1 : 0);
        break;

    /* CMP */
    case 0xC9:
        op_compare(cpu, cpu->a, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0xC5:
        op_compare(cpu, cpu->a, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0xD5:
        op_compare(cpu, cpu->a, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xCD:
        op_compare(cpu, cpu->a, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xDD:
        op_compare(cpu, cpu->a, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0xD9:
        op_compare(cpu, cpu->a, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0xC1:
        op_compare(cpu, cpu->a, read_operand(cpu, ADDR_INX, &addr, &page_crossed));
        cycles = 6;
        break;
    case 0xD1:
        op_compare(cpu, cpu->a, read_operand(cpu, ADDR_INY, &addr, &page_crossed));
        cycles = 5 + (page_crossed ? 1 : 0);
        break;

    /* SBC */
    case 0xE9:
        op_sbc(cpu, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0xE5:
        op_sbc(cpu, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0xF5:
        op_sbc(cpu, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xED:
        op_sbc(cpu, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xFD:
        op_sbc(cpu, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0xF9:
        op_sbc(cpu, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0xE1:
        op_sbc(cpu, read_operand(cpu, ADDR_INX, &addr, &page_crossed));
        cycles = 6;
        break;
    case 0xF1:
        op_sbc(cpu, read_operand(cpu, ADDR_INY, &addr, &page_crossed));
        cycles = 5 + (page_crossed ? 1 : 0);
        break;

    /* BIT */
    case 0x24:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        {
            uint8_t value = cpu_read(cpu, addr);
            set_flag(cpu, FLAG_Z, (cpu->a & value) == 0);
            set_flag(cpu, FLAG_V, (value & 0x40) != 0);
            set_flag(cpu, FLAG_N, (value & 0x80) != 0);
        }
        cycles = 3;
        break;
    case 0x2C:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        {
            uint8_t value = cpu_read(cpu, addr);
            set_flag(cpu, FLAG_Z, (cpu->a & value) == 0);
            set_flag(cpu, FLAG_V, (value & 0x40) != 0);
            set_flag(cpu, FLAG_N, (value & 0x80) != 0);
        }
        cycles = 4;
        break;

    /* LDY */
    case 0xA0:
        op_load(cpu, &cpu->y, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0xA4:
        op_load(cpu, &cpu->y, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0xB4:
        op_load(cpu, &cpu->y, read_operand(cpu, ADDR_ZPX, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xAC:
        op_load(cpu, &cpu->y, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xBC:
        op_load(cpu, &cpu->y, read_operand(cpu, ADDR_ABX, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;

    /* LDX */
    case 0xA2:
        op_load(cpu, &cpu->x, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed));
        cycles = 2;
        break;
    case 0xA6:
        op_load(cpu, &cpu->x, read_operand(cpu, ADDR_ZP, &addr, &page_crossed));
        cycles = 3;
        break;
    case 0xB6:
        op_load(cpu, &cpu->x, read_operand(cpu, ADDR_ZPY, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xAE:
        op_load(cpu, &cpu->x, read_operand(cpu, ADDR_ABS, &addr, &page_crossed));
        cycles = 4;
        break;
    case 0xBE:
        op_load(cpu, &cpu->x, read_operand(cpu, ADDR_ABY, &addr, &page_crossed));
        cycles = 4 + (page_crossed ? 1 : 0);
        break;

    /* STX */
    case 0x86:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cpu_write(cpu, addr, cpu->x);
        cycles = 3;
        break;
    case 0x96:
        addr = addr_mode_fetch(cpu, ADDR_ZPY, &page_crossed);
        cpu_write(cpu, addr, cpu->x);
        cycles = 4;
        break;
    case 0x8E:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cpu_write(cpu, addr, cpu->x);
        cycles = 4;
        break;

    /* STY */
    case 0x84:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cpu_write(cpu, addr, cpu->y);
        cycles = 3;
        break;
    case 0x94:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        cpu_write(cpu, addr, cpu->y);
        cycles = 4;
        break;
    case 0x8C:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cpu_write(cpu, addr, cpu->y);
        cycles = 4;
        break;

    /* INC */
    case 0xE6:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) + 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 5;
        break;
    case 0xF6:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) + 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 6;
        break;
    case 0xEE:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) + 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 6;
        break;
    case 0xFE:
        addr = addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) + 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 7;
        break;

    /* DEC */
    case 0xC6:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) - 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 5;
        break;
    case 0xD6:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) - 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 6;
        break;
    case 0xCE:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) - 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 6;
        break;
    case 0xDE:
        addr = addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        {
            uint8_t value = (uint8_t)(cpu_read(cpu, addr) - 1);
            cpu_write(cpu, addr, value);
            update_zn(cpu, value);
        }
        cycles = 7;
        break;

    /* ASL */
    case 0x0A:
        cpu->a = op_asl(cpu, cpu->a);
        cycles = 2;
        break;
    case 0x06:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cpu_write(cpu, addr, op_asl(cpu, cpu_read(cpu, addr)));
        cycles = 5;
        break;
    case 0x16:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        cpu_write(cpu, addr, op_asl(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x0E:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cpu_write(cpu, addr, op_asl(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x1E:
        addr = addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        cpu_write(cpu, addr, op_asl(cpu, cpu_read(cpu, addr)));
        cycles = 7;
        break;

    /* LSR */
    case 0x4A:
        cpu->a = op_lsr(cpu, cpu->a);
        cycles = 2;
        break;
    case 0x46:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cpu_write(cpu, addr, op_lsr(cpu, cpu_read(cpu, addr)));
        cycles = 5;
        break;
    case 0x56:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        cpu_write(cpu, addr, op_lsr(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x4E:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cpu_write(cpu, addr, op_lsr(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x5E:
        addr = addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        cpu_write(cpu, addr, op_lsr(cpu, cpu_read(cpu, addr)));
        cycles = 7;
        break;

    /* ROL */
    case 0x2A:
        cpu->a = op_rol(cpu, cpu->a);
        cycles = 2;
        break;
    case 0x26:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cpu_write(cpu, addr, op_rol(cpu, cpu_read(cpu, addr)));
        cycles = 5;
        break;
    case 0x36:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        cpu_write(cpu, addr, op_rol(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x2E:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cpu_write(cpu, addr, op_rol(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x3E:
        addr = addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        cpu_write(cpu, addr, op_rol(cpu, cpu_read(cpu, addr)));
        cycles = 7;
        break;

    /* ROR */
    case 0x6A:
        cpu->a = op_ror(cpu, cpu->a);
        cycles = 2;
        break;
    case 0x66:
        addr = addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cpu_write(cpu, addr, op_ror(cpu, cpu_read(cpu, addr)));
        cycles = 5;
        break;
    case 0x76:
        addr = addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        cpu_write(cpu, addr, op_ror(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x6E:
        addr = addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cpu_write(cpu, addr, op_ror(cpu, cpu_read(cpu, addr)));
        cycles = 6;
        break;
    case 0x7E:
        addr = addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        cpu_write(cpu, addr, op_ror(cpu, cpu_read(cpu, addr)));
        cycles = 7;
        break;

    /* Branches */
    case 0x10: { /* BPL */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, !get_flag(cpu, FLAG_N), target, &cycles);
        break;
    }
    case 0x30: { /* BMI */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, get_flag(cpu, FLAG_N), target, &cycles);
        break;
    }
    case 0x50: { /* BVC */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, !get_flag(cpu, FLAG_V), target, &cycles);
        break;
    }
    case 0x70: { /* BVS */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, get_flag(cpu, FLAG_V), target, &cycles);
        break;
    }
    case 0x90: { /* BCC */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, !get_flag(cpu, FLAG_C), target, &cycles);
        break;
    }
    case 0xB0: { /* BCS */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, get_flag(cpu, FLAG_C), target, &cycles);
        break;
    }
    case 0xD0: { /* BNE */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, !get_flag(cpu, FLAG_Z), target, &cycles);
        break;
    }
    case 0xF0: { /* BEQ */
        uint16_t target = addr_mode_fetch(cpu, ADDR_REL, &page_crossed);
        cycles = 2;
        branch(cpu, get_flag(cpu, FLAG_Z), target, &cycles);
        break;
    }

    /* Flag operations */
    case 0x18: set_flag(cpu, FLAG_C, false); cycles = 2; break; /* CLC */
    case 0x38: set_flag(cpu, FLAG_C, true);  cycles = 2; break; /* SEC */
    case 0x58: set_flag(cpu, FLAG_I, false); cycles = 2; break; /* CLI */
    case 0x78: set_flag(cpu, FLAG_I, true);  cycles = 2; break; /* SEI */
    case 0xB8: set_flag(cpu, FLAG_V, false); cycles = 2; break; /* CLV */
    case 0xD8: set_flag(cpu, FLAG_D, false); cycles = 2; break; /* CLD */
    case 0xF8: set_flag(cpu, FLAG_D, true);  cycles = 2; break; /* SED */

    /* Register transfers */
    case 0xAA: cpu->x = cpu->a; update_zn(cpu, cpu->x); cycles = 2; break; /* TAX */
    case 0x8A: cpu->a = cpu->x; update_zn(cpu, cpu->a); cycles = 2; break; /* TXA */
    case 0xA8: cpu->y = cpu->a; update_zn(cpu, cpu->y); cycles = 2; break; /* TAY */
    case 0x98: cpu->a = cpu->y; update_zn(cpu, cpu->a); cycles = 2; break; /* TYA */
    case 0xBA: cpu->x = cpu->sp; update_zn(cpu, cpu->x); cycles = 2; break; /* TSX */
    case 0x9A: cpu->sp = cpu->x; cycles = 2; break; /* TXS */

    /* Stack operations */
    case 0x48: push_byte(cpu, cpu->a); cycles = 3; break; /* PHA */
    case 0x08: push_byte(cpu, (uint8_t)(cpu->status | FLAG_B)); cycles = 3; break; /* PHP */
    case 0x68: cpu->a = pull_byte(cpu); update_zn(cpu, cpu->a); cycles = 4; break; /* PLA */
    case 0x28: cpu->status = (uint8_t)((pull_byte(cpu) & ~FLAG_B) | FLAG_U); cycles = 4; break; /* PLP */

    /* Increment/decrement registers */
    case 0xE8: cpu->x = (uint8_t)(cpu->x + 1); update_zn(cpu, cpu->x); cycles = 2; break; /* INX */
    case 0xC8: cpu->y = (uint8_t)(cpu->y + 1); update_zn(cpu, cpu->y); cycles = 2; break; /* INY */
    case 0xCA: cpu->x = (uint8_t)(cpu->x - 1); update_zn(cpu, cpu->x); cycles = 2; break; /* DEX */
    case 0x88: cpu->y = (uint8_t)(cpu->y - 1); update_zn(cpu, cpu->y); cycles = 2; break; /* DEY */

    /* Comparison */
    case 0xC0:
        op_compare(cpu, cpu->y, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed)); cycles = 2; break; /* CPY */
    case 0xC4:
        op_compare(cpu, cpu->y, read_operand(cpu, ADDR_ZP, &addr, &page_crossed)); cycles = 3; break;
    case 0xCC:
        op_compare(cpu, cpu->y, read_operand(cpu, ADDR_ABS, &addr, &page_crossed)); cycles = 4; break;
    case 0xE0:
        op_compare(cpu, cpu->x, (uint8_t)addr_mode_fetch(cpu, ADDR_IMM, &page_crossed)); cycles = 2; break; /* CPX */
    case 0xE4:
        op_compare(cpu, cpu->x, read_operand(cpu, ADDR_ZP, &addr, &page_crossed)); cycles = 3; break;
    case 0xEC:
        op_compare(cpu, cpu->x, read_operand(cpu, ADDR_ABS, &addr, &page_crossed)); cycles = 4; break;

    /* JMP/JSR/RTS/RTI */
    case 0x4C: cpu->pc = fetch_word(cpu); cycles = 3; break; /* JMP abs */
    case 0x6C: {
        uint16_t pointer = fetch_word(cpu);
        uint16_t hi_addr = (uint16_t)((pointer & 0xFF00) | ((pointer + 1) & 0x00FF));
        uint8_t lo = cpu_read(cpu, pointer);
        uint8_t hi = cpu_read(cpu, hi_addr);
        cpu->pc = (uint16_t)(lo | (hi << 8));
        cycles = 5;
        break;
    }
    case 0x20: {
        uint16_t target = fetch_word(cpu);
        uint16_t return_addr = (uint16_t)(cpu->pc - 1);
        push_byte(cpu, (uint8_t)(return_addr >> 8));
        push_byte(cpu, (uint8_t)(return_addr & 0xFF));
        cpu->pc = target;
        cycles = 6;
        break;
    }
    case 0x60:
        cpu->pc = pull_byte(cpu);
        cpu->pc |= (uint16_t)pull_byte(cpu) << 8;
        cpu->pc = (uint16_t)(cpu->pc + 1);
        cycles = 6;
        break; /* RTS */
    case 0x40:
        cpu->status = (uint8_t)((pull_byte(cpu) & ~FLAG_B) | FLAG_U);
        cpu->pc = pull_byte(cpu);
        cpu->pc |= (uint16_t)pull_byte(cpu) << 8;
        cycles = 6;
        break; /* RTI */

    /* JSR/BRK etc already handled */

    /* Store zero (illegal NOPs) - treat as NOP with proper fetch */
    case 0xEA:
        cycles = 2;
        break;
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        cycles = 2;
        break;
    case 0x0C:
        addr_mode_fetch(cpu, ADDR_ABS, &page_crossed);
        cycles = 4;
        break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        addr_mode_fetch(cpu, ADDR_ABX, &page_crossed);
        cycles = 4 + (page_crossed ? 1 : 0);
        break;
    case 0x04: case 0x44: case 0x64:
        addr_mode_fetch(cpu, ADDR_ZP, &page_crossed);
        cycles = 3;
        break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
        addr_mode_fetch(cpu, ADDR_ZPX, &page_crossed);
        cycles = 4;
        break;

    default:
        /* Treat any remaining opcode as NOP (length guessed by addressing code above) */
        cycles = 2;
        break;
    }

    cpu->cycles += cycles;
    return cycles;
}

void cpu_trigger_nmi(CPU6502 *cpu) {
    cpu->nmi_pending = true;
}

void cpu_set_irq_line(CPU6502 *cpu, bool asserted) {
    cpu->irq_pending = asserted;
}
