#ifndef MAPPER_H
#define MAPPER_H

#include <stdbool.h>
#include <stdint.h>

#include "cartridge.h"

typedef struct Mapper Mapper;

typedef uint8_t (*mapper_read_fn)(Mapper *, uint16_t addr);
typedef void (*mapper_write_fn)(Mapper *, uint16_t addr, uint8_t value);
typedef void (*mapper_step_fn)(Mapper *);
typedef void (*mapper_scanline_fn)(Mapper *);
typedef void (*mapper_reset_fn)(Mapper *, bool hard_reset);
typedef bool (*mapper_irq_fn)(Mapper *);

struct Mapper {
    Cartridge *cart;
    mapper_read_fn prg_read;
    mapper_write_fn prg_write;
    mapper_read_fn chr_read;
    mapper_write_fn chr_write;
    mapper_step_fn cpu_step;
    mapper_scanline_fn notify_scanline;
    mapper_reset_fn reset;
    mapper_irq_fn irq_pending;
    void (*destroy)(Mapper *);
    void *impl;
};

Mapper *mapper_create(Cartridge *cart);

#endif /* MAPPER_H */
