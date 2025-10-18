#include "cartridge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mapper.h"
#include "util.h"

static bool parse_header(FILE *fp, Cartridge *cart) {
    uint8_t header[16];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        return false;
    }

    if (memcmp(header, "NES\x1A", 4) != 0) {
        fprintf(stderr, "Invalid NES header magic\n");
        return false;
    }

    uint8_t prg_banks = header[4];
    uint8_t chr_banks = header[5];
    uint8_t flags6 = header[6];
    uint8_t flags7 = header[7];

    if ((flags7 & 0x0C) == 0x08) {
        fprintf(stderr, "NES 2.0 format not supported\n");
        return false;
    }

    cart->mapper = (MapperId)((flags6 >> 4) | (flags7 & 0xF0));
    cart->mirror = (flags6 & 0x01) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    if (flags6 & 0x08) {
        cart->mirror = MIRROR_FOUR_SCREEN;
    }
    cart->has_battery = (flags6 & 0x02) != 0;
    cart->prg_rom_size = prg_banks * 16 * 1024;
    cart->chr_rom_size = chr_banks * 8 * 1024;
    cart->has_chr_ram = (cart->chr_rom_size == 0);
    uint8_t prg_ram_banks = header[8];
    if (prg_ram_banks == 0) {
        prg_ram_banks = 1;
    }
    cart->prg_ram_size = prg_ram_banks * 8 * 1024;

    if (flags6 & 0x04) {
        fseek(fp, 512, SEEK_CUR);
    }

    return true;
}

static bool read_prg(FILE *fp, Cartridge *cart) {
    cart->prg_rom = malloc(cart->prg_rom_size);
    if (!cart->prg_rom) {
        return false;
    }
    if (fread(cart->prg_rom, 1, cart->prg_rom_size, fp) != cart->prg_rom_size) {
        return false;
    }
    return true;
}

static bool read_chr(FILE *fp, Cartridge *cart) {
    if (cart->has_chr_ram) {
        cart->chr_rom_size = 8 * 1024;
        cart->chr_rom = calloc(1, cart->chr_rom_size);
        return cart->chr_rom != NULL;
    }

    cart->chr_rom = malloc(cart->chr_rom_size);
    if (!cart->chr_rom) {
        return false;
    }
    if (fread(cart->chr_rom, 1, cart->chr_rom_size, fp) != cart->chr_rom_size) {
        return false;
    }
    return true;
}

bool cartridge_load(Cartridge *cart, const char *path, FILE *log_stream) {
    memset(cart, 0, sizeof(*cart));
    if (strlen(path) >= sizeof(cart->rom_path)) {
        fprintf(stderr, "ROM path too long\n");
        return false;
    }
    strncpy(cart->rom_path, path, sizeof(cart->rom_path) - 1);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open ROM %s: %s\n", path, strerror(errno));
        return false;
    }
    NES_UNUSED(log_stream);

    bool ok = parse_header(fp, cart);
    if (!ok) {
        fclose(fp);
        cartridge_unload(cart);
        return false;
    }

    if (!read_prg(fp, cart) || !read_chr(fp, cart)) {
        fclose(fp);
        cartridge_unload(cart);
        return false;
    }

    fclose(fp);

    cart->prg_ram = calloc(1, cart->prg_ram_size);
    if (!cart->prg_ram) {
        cartridge_unload(cart);
        return false;
    }

    cart->mapper_impl = mapper_create(cart);
    if (!cart->mapper_impl) {
        fprintf(stderr, "Unsupported mapper: %u\n", cart->mapper);
        cartridge_unload(cart);
        return false;
    }

    return true;
}

void cartridge_unload(Cartridge *cart) {
    if (!cart) {
        return;
    }
    if (cart->mapper_impl && cart->mapper_impl->destroy) {
        cart->mapper_impl->destroy(cart->mapper_impl);
    }
    free(cart->prg_rom);
    free(cart->chr_rom);
    free(cart->prg_ram);
    memset(cart, 0, sizeof(*cart));
}

static void sav_path(const Cartridge *cart, char *out, size_t out_size) {
    snprintf(out, out_size, "%s.sav", cart->rom_path);
}

bool cartridge_save_battery(const Cartridge *cart) {
    if (!cart->has_battery || !cart->prg_ram || cart->prg_ram_size == 0) {
        return true;
    }
    char path[sizeof(cart->rom_path) + 4];
    sav_path(cart, path, sizeof(path));
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return false;
    }
    size_t written = fwrite(cart->prg_ram, 1, cart->prg_ram_size, fp);
    fclose(fp);
    return written == cart->prg_ram_size;
}

bool cartridge_load_battery(Cartridge *cart) {
    if (!cart->has_battery || !cart->prg_ram || cart->prg_ram_size == 0) {
        return true;
    }
    char path[sizeof(cart->rom_path) + 4];
    sav_path(cart, path, sizeof(path));
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    size_t read = fread(cart->prg_ram, 1, cart->prg_ram_size, fp);
    fclose(fp);
    return read == cart->prg_ram_size;
}
