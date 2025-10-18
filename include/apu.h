#ifndef APU_H
#define APU_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct NES;

typedef struct Envelope {
    bool start;
    bool constant_volume;
    bool loop;
    uint8_t volume;
    uint8_t divider;
    uint8_t period;
    uint8_t decay_level;
} Envelope;

typedef struct LengthCounter {
    bool enabled;
    uint8_t value;
} LengthCounter;

typedef struct SweepUnit {
    bool enabled;
    bool negate;
    bool reload;
    uint8_t period;
    uint8_t divider;
    uint8_t shift;
} SweepUnit;

typedef struct PulseChannel {
    bool enabled;
    uint8_t duty;
    uint8_t duty_step;
    uint16_t timer;
    uint16_t timer_reload;
    Envelope envelope;
    LengthCounter length;
    SweepUnit sweep;
} PulseChannel;

typedef struct TriangleChannel {
    bool enabled;
    uint16_t timer;
    uint16_t timer_reload;
    uint8_t sequence_step;
    uint8_t linear_counter;
    uint8_t linear_reload;
    bool control;
    bool linear_reload_flag;
    LengthCounter length;
} TriangleChannel;

typedef struct NoiseChannel {
    bool enabled;
    uint16_t shift_register;
    uint8_t mode;
    uint8_t period;
    uint16_t timer;
    Envelope envelope;
    LengthCounter length;
} NoiseChannel;

typedef struct DMCChannel {
    bool enabled;
    bool loop;
    bool irq_enabled;
    bool irq_pending;
    uint8_t output;
    uint8_t shift_register;
    uint8_t bits_remaining;
    uint16_t address;
    uint16_t current_address;
    uint16_t length;
    uint16_t bytes_remaining;
    uint16_t timer;
    uint16_t timer_reload;
    bool sample_buffer_filled;
    uint8_t sample_buffer;
} DMCChannel;

typedef struct APU {
    struct NES *nes;
    uint64_t cycle;
    uint32_t frame_step;
    uint8_t frame_sequence_position;
    bool five_step_sequence;
    bool frame_irq_inhibit;
    bool frame_irq_pending;

    PulseChannel pulses[2];
    TriangleChannel triangle;
    NoiseChannel noise;
    DMCChannel dmc;

    double clock_rate;
    double sample_rate;
    double sample_timer;

    float sample_output;
    float sample_buffer[8192];
    uint32_t sample_write;
    uint32_t sample_read;
    int sample_lock;
} APU;

void apu_init(struct NES *nes, APU *apu);
void apu_reset(APU *apu, bool hard_reset);
void apu_step(APU *apu, uint32_t cpu_cycles);
uint8_t apu_read_status(APU *apu);
void apu_write_register(APU *apu, uint16_t addr, uint8_t value);
float apu_sample_output(APU *apu);
bool apu_irq_pending(const APU *apu);
size_t apu_samples_available(const APU *apu);


#endif /* APU_H */
