#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct Mapper;

typedef enum {
    MIRROR_HORIZONTAL,
    MIRROR_VERTICAL,
    MIRROR_FOUR_SCREEN,
    MIRROR_SINGLE_SCREEN_LOWER,
    MIRROR_SINGLE_SCREEN_UPPER,
} MirrorMode;

typedef enum {
    MAPPER_NROM = 0,
    MAPPER_MMC1 = 1,
    MAPPER_UXROM = 2,
    MAPPER_CNROM = 3,
    MAPPER_MMC3 = 4,
    MAPPER_UNSUPPORTED = 0xFF
} MapperId;

typedef struct Cartridge {
    uint8_t *prg_rom;
    uint8_t *chr_rom;
    uint8_t *prg_ram;
    uint32_t prg_rom_size;
    uint32_t chr_rom_size;
    uint32_t prg_ram_size;
    bool has_chr_ram;
    bool has_battery;
    MirrorMode mirror;
    MapperId mapper;
    struct Mapper *mapper_impl;
    char rom_path[512];
} Cartridge;

bool cartridge_load(Cartridge *cart, const char *path, FILE *log_stream);
void cartridge_unload(Cartridge *cart);

bool cartridge_save_battery(const Cartridge *cart);
bool cartridge_load_battery(Cartridge *cart);

#endif /* CARTRIDGE_H */
