#include "controller.h"

void controller_reset(Controller *pad) {
    pad->shift = 0;
    pad->state = 0;
    pad->strobe = false;
}

void controller_set_state(Controller *pad, uint8_t value) {
    pad->state = value;
}

uint8_t controller_read(Controller *pad) {
    uint8_t value = 0x40;
    value |= (pad->shift & 0x01);
    if (!pad->strobe) {
        pad->shift = (pad->shift >> 1) | 0x80;
    }
    return value;
}

void controller_write(Controller *pad, uint8_t value) {
    bool strobe = (value & 0x01) != 0;
    if (pad->strobe && !strobe) {
        pad->shift = pad->state;
    } else if (strobe) {
        pad->shift = pad->state;
    }
    pad->strobe = strobe;
}
