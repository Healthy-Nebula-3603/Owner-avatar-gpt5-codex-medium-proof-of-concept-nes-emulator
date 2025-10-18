#include "apu.h"

#include <math.h>
#include <string.h>

#include <SDL_atomic.h>

#include "bus.h"
#include "nes_internal.h"

#define CPU_CLOCK_NTSC 1789773.0
#define SAMPLE_RATE_DEFAULT 48000.0

static const uint8_t length_table[32] = {
    10, 254, 20,  2,  40,  4,  80,  6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

static const uint16_t noise_period_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160,
    202, 254, 380, 508, 762, 1016, 2034, 4068
};

static const uint16_t dmc_period_table[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106, 85, 72, 54
};

static const uint8_t pulse_duty_table[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
};

static const uint8_t triangle_sequence[32] = {
    15, 14, 13, 12, 11, 10, 9, 8,
    7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15
};

static void envelope_clock(Envelope *env) {
    if (env->start) {
        env->start = false;
        env->decay_level = 15;
        env->divider = env->period;
    } else {
        if (env->divider == 0) {
            env->divider = env->period;
            if (env->decay_level > 0) {
                env->decay_level--;
            } else if (env->loop) {
                env->decay_level = 15;
            }
        } else {
            env->divider--;
        }
    }
}

static uint8_t envelope_output(const Envelope *env) {
    return env->constant_volume ? env->volume : env->decay_level;
}

static void length_clock(LengthCounter *length) {
    if (length->enabled && length->value > 0) {
        length->value--;
    }
}

static void sweep_clock(PulseChannel *pulse, uint8_t channel_index) {
    SweepUnit *sweep = &pulse->sweep;
    if (sweep->divider == 0) {
        if (sweep->enabled && sweep->shift > 0 && pulse->timer_reload >= 8) {
            uint16_t delta = pulse->timer_reload >> sweep->shift;
            uint16_t target;
            if (sweep->negate) {
                target = (uint16_t)(pulse->timer_reload - delta - (channel_index == 0 ? 1 : 0));
            } else {
                target = (uint16_t)(pulse->timer_reload + delta);
            }
            if (target < 0x800) {
                pulse->timer_reload = target;
            }
        }
        sweep->divider = sweep->period;
        sweep->reload = false;
    } else if (sweep->reload) {
        sweep->divider = sweep->period;
        sweep->reload = false;
    } else {
        sweep->divider--;
    }
}

static bool sweep_mutes(const PulseChannel *pulse, uint8_t channel_index) {
    const SweepUnit *sweep = &pulse->sweep;
    uint16_t delta = pulse->timer_reload >> sweep->shift;
    uint16_t target = pulse->timer_reload;
    if (sweep->negate) {
        target -= delta + (channel_index == 0 ? 1 : 0);
    } else {
        target += delta;
    }
    return pulse->timer_reload < 8 || target > 0x7FF;
}

static void triangle_clock_linear(TriangleChannel *triangle) {
    if (triangle->linear_reload_flag) {
        triangle->linear_counter = triangle->linear_reload;
    } else if (triangle->linear_counter > 0) {
        triangle->linear_counter--;
    }
    if (!triangle->control) {
        triangle->linear_reload_flag = false;
    }
}

static void dmc_reload_sample(DMCChannel *dmc, struct NES *nes) {
    if (dmc->bytes_remaining == 0) {
        return;
    }
    dmc->sample_buffer = nes_cpu_read(nes, dmc->current_address);
    dmc->current_address++;
    if (dmc->current_address == 0x0000) {
        dmc->current_address = 0x8000;
    }
    dmc->bytes_remaining--;
    dmc->sample_buffer_filled = true;
    if (dmc->bytes_remaining == 0 && dmc->loop) {
        dmc->current_address = dmc->address;
        dmc->bytes_remaining = dmc->length;
    } else if (dmc->bytes_remaining == 0 && dmc->irq_enabled) {
        dmc->irq_pending = true;
    }
}

static void push_sample(APU *apu, float sample) {
    SDL_AtomicLock(&apu->sample_lock);
    uint32_t mask = (uint32_t)(sizeof(apu->sample_buffer) / sizeof(apu->sample_buffer[0]) - 1);
    uint32_t next = (apu->sample_write + 1) & mask;
    if (next == apu->sample_read) {
        SDL_AtomicUnlock(&apu->sample_lock);
        return;
    }
    apu->sample_buffer[apu->sample_write] = sample;
    apu->sample_write = next;
    SDL_AtomicUnlock(&apu->sample_lock);
}

static float mix_output(const APU *apu) {
    float p1 = 0.0f;
    float p2 = 0.0f;
    const PulseChannel *pulse1 = &apu->pulses[0];
    const PulseChannel *pulse2 = &apu->pulses[1];
    if (pulse1->enabled && pulse1->length.value > 0 && !sweep_mutes(pulse1, 0)) {
        uint8_t duty = pulse_duty_table[pulse1->duty][pulse1->duty_step];
        if (duty) {
            p1 = (float)envelope_output(&pulse1->envelope);
        }
    }
    if (pulse2->enabled && pulse2->length.value > 0 && !sweep_mutes(pulse2, 1)) {
        uint8_t duty = pulse_duty_table[pulse2->duty][pulse2->duty_step];
        if (duty) {
            p2 = (float)envelope_output(&pulse2->envelope);
        }
    }
    float pulse_sum = p1 + p2;
    float pulse_out = 0.0f;
    if (pulse_sum > 0.0f) {
        pulse_out = 95.88f / ((8128.0f / pulse_sum) + 100.0f);
    }

    float triangle_out = 0.0f;
    const TriangleChannel *tri = &apu->triangle;
    if (tri->enabled && tri->length.value > 0 && tri->linear_counter > 0) {
        triangle_out = (float)triangle_sequence[tri->sequence_step];
    }

    float noise_out = 0.0f;
    const NoiseChannel *noise = &apu->noise;
    if (noise->enabled && noise->length.value > 0) {
        float amp = (float)envelope_output(&noise->envelope);
        if ((noise->shift_register & 0x01) == 0) {
            noise_out = amp;
        }
    }

    float dmc_out = 0.0f;
    const DMCChannel *dmc = &apu->dmc;
    if (dmc->enabled) {
        dmc_out = (float)dmc->output;
    }

    float tnd = (triangle_out / 8227.0f) + (noise_out / 12241.0f) + (dmc_out / 22638.0f);
    float tnd_out = 0.0f;
    if (tnd > 0.0f) {
        tnd_out = 159.79f / ((1.0f / tnd) + 100.0f);
    }

    return pulse_out + tnd_out;
}

void apu_init(struct NES *nes, APU *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->nes = nes;
    apu->clock_rate = CPU_CLOCK_NTSC;
    apu->sample_rate = SAMPLE_RATE_DEFAULT;
    apu->sample_timer = 0.0;
}

void apu_reset(APU *apu, bool hard_reset) {
    memset(&apu->pulses, 0, sizeof(apu->pulses));
    memset(&apu->triangle, 0, sizeof(apu->triangle));
    memset(&apu->noise, 0, sizeof(apu->noise));
    memset(&apu->dmc, 0, sizeof(apu->dmc));
    apu->cycle = 0;
    apu->frame_step = 0;
    apu->frame_sequence_position = 0;
    apu->five_step_sequence = false;
    apu->frame_irq_inhibit = false;
    apu->frame_irq_pending = false;
    apu->sample_write = 0;
    apu->sample_read = 0;
    apu->sample_timer = 0.0;
    apu->sample_lock = 0;
    apu->pulses[0].envelope.period = 0;
    apu->pulses[1].envelope.period = 0;
    apu->noise.shift_register = 1;
    apu->dmc.timer_reload = dmc_period_table[0];
    apu->dmc.address = 0xC000;
    apu->dmc.length = 1;
    if (hard_reset) {
        apu->dmc.output = 0;
    }
}

static void clock_envelopes(APU *apu) {
    envelope_clock(&apu->pulses[0].envelope);
    envelope_clock(&apu->pulses[1].envelope);
    envelope_clock(&apu->noise.envelope);
    triangle_clock_linear(&apu->triangle);
}

static void clock_length_sweep(APU *apu) {
    if (!apu->pulses[0].envelope.loop) {
        length_clock(&apu->pulses[0].length);
    }
    if (!apu->pulses[1].envelope.loop) {
        length_clock(&apu->pulses[1].length);
    }
    if (!apu->triangle.control) {
        length_clock(&apu->triangle.length);
    }
    if (!apu->noise.envelope.loop) {
        length_clock(&apu->noise.length);
    }

    sweep_clock(&apu->pulses[0], 0);
    sweep_clock(&apu->pulses[1], 1);
}

static void clock_frame_counter(APU *apu, uint32_t cycles) {
    static const uint32_t step_targets_4[4] = {7457, 7457, 7456, 7458};
    static const uint32_t step_targets_5[5] = {7457, 7457, 7456, 7458, 7458};

    apu->frame_step += cycles;

    if (!apu->five_step_sequence) {
        while (apu->frame_step >= step_targets_4[apu->frame_sequence_position]) {
            apu->frame_step -= step_targets_4[apu->frame_sequence_position];
            switch (apu->frame_sequence_position) {
            case 0:
            case 2:
                clock_envelopes(apu);
                clock_length_sweep(apu);
                break;
            case 1:
                clock_envelopes(apu);
                break;
            case 3:
                clock_envelopes(apu);
                if (!apu->frame_irq_inhibit) {
                    apu->frame_irq_pending = true;
                }
                break;
            }
            apu->frame_sequence_position = (apu->frame_sequence_position + 1) & 0x03;
        }
    } else {
        while (apu->frame_step >= step_targets_5[apu->frame_sequence_position]) {
            apu->frame_step -= step_targets_5[apu->frame_sequence_position];
            switch (apu->frame_sequence_position) {
            case 0:
            case 1:
                clock_envelopes(apu);
                break;
            case 2:
                clock_envelopes(apu);
                clock_length_sweep(apu);
                break;
            case 3:
                clock_envelopes(apu);
                clock_length_sweep(apu);
                break;
            case 4:
                clock_envelopes(apu);
                break;
            }
            apu->frame_sequence_position = (uint8_t)((apu->frame_sequence_position + 1) % 5);
        }
    }
}

static void clock_pulse(PulseChannel *pulse, uint8_t index) {
    if (!pulse->enabled || pulse->length.value == 0 || sweep_mutes(pulse, index)) {
        return;
    }
    if (pulse->timer == 0) {
        pulse->timer = pulse->timer_reload;
        pulse->duty_step = (pulse->duty_step + 1) & 0x07;
    } else {
        pulse->timer--;
    }
}

static void clock_triangle(TriangleChannel *triangle) {
    if (!triangle->enabled || triangle->length.value == 0 || triangle->linear_counter == 0 || triangle->timer_reload < 2) {
        return;
    }
    if (triangle->timer == 0) {
        triangle->timer = triangle->timer_reload;
        triangle->sequence_step = (triangle->sequence_step + 1) & 0x1F;
    } else {
        triangle->timer--;
    }
}

static void clock_noise(NoiseChannel *noise) {
    if (!noise->enabled || noise->length.value == 0) {
        return;
    }
    if (noise->timer == 0) {
        noise->timer = noise_period_table[noise->period];
        uint16_t feedback = (uint16_t)(((noise->shift_register & 0x01) ^ ((noise->shift_register >> (noise->mode ? 6 : 1)) & 0x01)));
        noise->shift_register = (uint16_t)((noise->shift_register >> 1) | (feedback << 14));
    } else {
        noise->timer--;
    }
}

static void clock_dmc(DMCChannel *dmc, struct NES *nes) {
    if (!dmc->enabled) {
        return;
    }
    if (dmc->timer == 0) {
        dmc->timer = dmc->timer_reload;
        if (dmc->bits_remaining == 0) {
            if (!dmc->sample_buffer_filled) {
                dmc_reload_sample(dmc, nes);
            }
            if (!dmc->sample_buffer_filled) {
                return;
            }
            dmc->shift_register = dmc->sample_buffer;
            dmc->bits_remaining = 8;
            dmc->sample_buffer_filled = false;
            dmc_reload_sample(dmc, nes);
        }
        if (dmc->bits_remaining > 0) {
            if (dmc->shift_register & 0x01) {
                if (dmc->output <= 125) {
                    dmc->output += 2;
                }
            } else if (dmc->output >= 2) {
                dmc->output -= 2;
            }
            dmc->shift_register >>= 1;
            dmc->bits_remaining--;
        }
    } else {
        dmc->timer--;
    }
}

void apu_step(APU *apu, uint32_t cpu_cycles) {
    for (uint32_t i = 0; i < cpu_cycles; ++i) {
        clock_pulse(&apu->pulses[0], 0);
        clock_pulse(&apu->pulses[1], 1);
        clock_triangle(&apu->triangle);
        clock_noise(&apu->noise);
        clock_dmc(&apu->dmc, apu->nes);

        apu->cycle++;
        clock_frame_counter(apu, 1);

        apu->sample_timer += apu->sample_rate;
        if (apu->sample_timer >= apu->clock_rate) {
            apu->sample_timer -= apu->clock_rate;
            float sample = mix_output(apu);
            apu->sample_output = sample;
            push_sample(apu, sample);
        }
    }
}

uint8_t apu_read_status(APU *apu) {
    uint8_t status = 0;
    if (apu->pulses[0].length.value > 0) status |= 0x01;
    if (apu->pulses[1].length.value > 0) status |= 0x02;
    if (apu->triangle.length.value > 0) status |= 0x04;
    if (apu->noise.length.value > 0) status |= 0x08;
    if (apu->dmc.bytes_remaining > 0) status |= 0x10;
    if (apu->frame_irq_pending) status |= 0x40;
    if (apu->dmc.irq_pending) status |= 0x80;

    apu->frame_irq_pending = false;
    apu->dmc.irq_pending = false;
    return status;
}

static void write_pulse(APU *apu, uint16_t addr, uint8_t value) {
    PulseChannel *pulse = &apu->pulses[(addr >> 2) & 0x01];
    switch (addr & 0x0003) {
    case 0:
        pulse->duty = (value >> 6) & 0x03;
        pulse->envelope.loop = (value & 0x20) != 0;
        pulse->envelope.constant_volume = (value & 0x10) != 0;
        pulse->envelope.volume = value & 0x0F;
        pulse->envelope.period = value & 0x0F;
        break;
    case 1:
        pulse->sweep.enabled = (value & 0x80) != 0;
        pulse->sweep.period = ((value >> 4) & 0x07) + 1;
        pulse->sweep.negate = (value & 0x08) != 0;
        pulse->sweep.shift = value & 0x07;
        pulse->sweep.reload = true;
        break;
    case 2:
        pulse->timer_reload = (uint16_t)((pulse->timer_reload & 0x0700) | value);
        break;
    case 3:
        pulse->timer_reload = (uint16_t)(((value & 0x07) << 8) | (pulse->timer_reload & 0x00FF));
        pulse->length.value = length_table[value >> 3];
        pulse->envelope.start = true;
        pulse->duty_step = 0;
        break;
    }
}

static void write_triangle(APU *apu, uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0x4008:
        apu->triangle.control = (value & 0x80) != 0;
        apu->triangle.linear_reload = value & 0x7F;
        break;
    case 0x400A:
        apu->triangle.timer_reload = (uint16_t)((apu->triangle.timer_reload & 0x0700) | value);
        break;
    case 0x400B:
        apu->triangle.timer_reload = (uint16_t)(((value & 0x07) << 8) | (apu->triangle.timer_reload & 0x00FF));
        apu->triangle.length.value = length_table[value >> 3];
        apu->triangle.linear_reload_flag = true;
        break;
    default:
        break;
    }
}

static void write_noise(APU *apu, uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0x400C:
        apu->noise.envelope.loop = (value & 0x20) != 0;
        apu->noise.envelope.constant_volume = (value & 0x10) != 0;
        apu->noise.envelope.volume = value & 0x0F;
        apu->noise.envelope.period = value & 0x0F;
        break;
    case 0x400E:
        apu->noise.mode = (value & 0x80) ? 1 : 0;
        apu->noise.period = value & 0x0F;
        break;
    case 0x400F:
        apu->noise.length.value = length_table[value >> 3];
        apu->noise.envelope.start = true;
        break;
    default:
        break;
    }
}

static void write_dmc(APU *apu, uint16_t addr, uint8_t value) {
    DMCChannel *dmc = &apu->dmc;
    switch (addr) {
    case 0x4010:
        dmc->irq_enabled = (value & 0x80) != 0;
        dmc->loop = (value & 0x40) != 0;
        dmc->timer_reload = dmc_period_table[value & 0x0F];
        if (!dmc->irq_enabled) {
            dmc->irq_pending = false;
        }
        break;
    case 0x4011:
        dmc->output = value & 0x7F;
        break;
    case 0x4012:
        dmc->address = (uint16_t)(0xC000 + (value << 6));
        dmc->current_address = dmc->address;
        break;
    case 0x4013:
        dmc->length = (uint16_t)(value << 4) + 1;
        dmc->bytes_remaining = dmc->length;
        break;
    default:
        break;
    }
}

void apu_write_register(APU *apu, uint16_t addr, uint8_t value) {
    if (addr >= 0x4000 && addr <= 0x4007) {
        write_pulse(apu, addr, value);
        return;
    }
    switch (addr) {
    case 0x4008:
    case 0x400A:
    case 0x400B:
        write_triangle(apu, addr, value);
        return;
    case 0x400C:
    case 0x400E:
    case 0x400F:
        write_noise(apu, addr, value);
        return;
    case 0x4010:
    case 0x4011:
    case 0x4012:
    case 0x4013:
        write_dmc(apu, addr, value);
        return;
    case 0x4015: {
        if (!(value & 0x01)) apu->pulses[0].length.value = 0;
        if (!(value & 0x02)) apu->pulses[1].length.value = 0;
        if (!(value & 0x04)) apu->triangle.length.value = 0;
        if (!(value & 0x08)) apu->noise.length.value = 0;
        apu->pulses[0].enabled = (value & 0x01) != 0;
        apu->pulses[1].enabled = (value & 0x02) != 0;
        apu->triangle.enabled = (value & 0x04) != 0;
        apu->noise.enabled = (value & 0x08) != 0;
        apu->dmc.enabled = (value & 0x10) != 0;
        if (apu->dmc.enabled && apu->dmc.bytes_remaining == 0) {
            apu->dmc.bytes_remaining = apu->dmc.length;
            apu->dmc.current_address = apu->dmc.address;
        }
        if (!apu->dmc.enabled) {
            apu->dmc.bytes_remaining = 0;
        }
        break;
    }
    case 0x4017: {
        apu->five_step_sequence = (value & 0x80) != 0;
        apu->frame_irq_inhibit = (value & 0x40) != 0;
        if (apu->frame_irq_inhibit) {
            apu->frame_irq_pending = false;
        }
        apu->frame_step = 0;
        apu->frame_sequence_position = 0;
        if (apu->five_step_sequence) {
            clock_envelopes(apu);
            clock_length_sweep(apu);
        }
        break;
    }
    default:
        break;
    }
}

float apu_sample_output(APU *apu) {
    SDL_SpinLock *lock = &apu->sample_lock;
    SDL_AtomicLock(lock);
    uint32_t mask = (uint32_t)(sizeof(apu->sample_buffer) / sizeof(apu->sample_buffer[0]) - 1);
    uint32_t available = (apu->sample_write - apu->sample_read) & mask;
    if (available == 0) {
        SDL_AtomicUnlock(lock);
        return apu->sample_output;
    }
    float sample = apu->sample_buffer[apu->sample_read];
    apu->sample_read = (apu->sample_read + 1) & mask;
    SDL_AtomicUnlock(lock);
    return sample;
}

bool apu_irq_pending(const APU *apu) {
    return apu->frame_irq_pending || apu->dmc.irq_pending;
}

size_t apu_samples_available(const APU *apu) {
    SDL_SpinLock *lock = (SDL_SpinLock *)&apu->sample_lock;
    SDL_AtomicLock(lock);
    size_t mask = (sizeof(apu->sample_buffer) / sizeof(apu->sample_buffer[0])) - 1;
    size_t value = (size_t)((apu->sample_write - apu->sample_read) & mask);
    SDL_AtomicUnlock(lock);
    return value;
}
