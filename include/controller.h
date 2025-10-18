#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Controller {
    uint8_t shift;
    uint8_t state;
    bool strobe;
} Controller;

void controller_reset(Controller *pad);
void controller_set_state(Controller *pad, uint8_t value);
uint8_t controller_read(Controller *pad);
void controller_write(Controller *pad, uint8_t value);

#endif /* CONTROLLER_H */
