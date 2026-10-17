/*
 * nes_sound.c - the sound of the NES from the state of the core's APU. core/apu.c keeps the
 * registers, the envelopes, sweeps, length and linear counters and the DMC; this file plays
 * the channels. Compiled in nes_fast.c right after core/apu.c, whose tables it uses.
 *
 * Instead of clocking the four channel timers every CPU cycle, it advances them in steps up
 * to the moments that matter: the end of an output sample, and every change the APU makes
 * (a register write, a new DMC level, the frame sequencer: the APU calls nes_sound_sync()
 * first). Each output sample is the average of the channels over its CPU cycles (a box
 * filter against aliasing), mixed with the linear approximation of the NES mixer and
 * high-passed at 90 Hz like the console's output.
 *
 * Differences to the core's own (unused) channel timers: the noise timer runs at the CPU
 * clock (its period table is in CPU cycles; the core clocked it at half that, an octave too
 * low), the triangle holds its level at ultrasonic periods instead of dropping to zero, and
 * a disabled triangle keeps its last level (no clicks).
 */
#include <stdbool.h>
#include <string.h>
#include "nes_sound.h"

#define OUT_MAX 2048

struct sound {
    bool on;
    APU *apu;
    uint32_t last;              /* CPU cycle the channels are made up to */
    uint32_t step_q16;          /* CPU cycles per output sample */
    uint32_t frac;              /* fraction of a cycle for the next sample */
    uint32_t len, left;         /* cycles of the current output sample, not made yet */
    int32_t count[4];           /* cycles to the next step: pulse 1, pulse 2, triangle, noise */
    uint32_t acc_pulse, acc_tri, acc_noise, acc_dmc; /* level x cycles */
    uint32_t hp_a;              /* high pass: y = a (y + x - x') in 1/65536 */
    int32_t hp_x, hp_y;
    int16_t out[OUT_MAX];
    int n;
};

static struct sound s_snd;

static uint32_t run_pulse(Pulse *p, int32_t *count, uint32_t n)
{
    uint32_t period = 2u * ((uint32_t)p->t.period + 1u);
    uint32_t step = p->t.step & 7u;
    uint32_t vol = 0;
    if (p->enabled && p->l.counter && !p->mute)
        vol = p->const_volume ? (uint32_t)p->envelope.period : (uint32_t)p->envelope.step;
    int32_t c = *count;
    uint32_t acc = 0;
    if (!vol) {
        /* silent: only the phase moves on */
        if ((uint32_t)c > n) {
            c -= (int32_t)n;
        } else {
            uint32_t rest = n - (uint32_t)c;
            step = (step + 1u + rest / period) & 7u;
            c = (int32_t)(period - rest % period);
        }
    } else {
        const uint8_t *d = duty[p->duty & 3u];
        while ((uint32_t)c <= n) {
            acc += d[step] * (uint32_t)c;
            n -= (uint32_t)c;
            step = (step + 1u) & 7u;
            c = (int32_t)period;
        }
        acc += d[step] * n;
        c -= (int32_t)n;
        acc *= vol;
    }
    p->t.step = step;
    *count = c;
    return acc;
}

static uint32_t run_triangle(Triangle *t, int32_t *count, uint32_t n)
{
    uint32_t step = t->sequencer.step & 31u;
    /* it steps only while both counters run; below period 2 it would be ultrasonic */
    if (!t->l.counter || !t->linear_counter || t->sequencer.period < 2)
        return tri_sequence[step] * n;
    uint32_t period = (uint32_t)t->sequencer.period + 1u;
    int32_t c = *count;
    uint32_t acc = 0;
    while ((uint32_t)c <= n) {
        acc += tri_sequence[step] * (uint32_t)c;
        n -= (uint32_t)c;
        step = (step + 1u) & 31u;
        c = (int32_t)period;
    }
    acc += tri_sequence[step] * n;
    *count = c - (int32_t)n;
    t->sequencer.step = step;
    return acc;
}

static uint32_t run_noise(Noise *z, int32_t *count, uint32_t n)
{
    if (!z->enabled || !z->l.counter)
        return 0; /* the shift register rests while it cannot be heard */
    uint32_t vol = z->const_volume ? (uint32_t)z->envelope.period : (uint32_t)z->envelope.step;
    if (!vol)
        return 0;
    uint32_t period = z->timer.period > 0 ? (uint32_t)z->timer.period : 1u;
    uint32_t shift = z->shift, tap = z->mode ? 6u : 1u;
    int32_t c = *count;
    uint32_t acc = 0;
    while ((uint32_t)c <= n) {
        if (!(shift & 1u))
            acc += (uint32_t)c;
        n -= (uint32_t)c;
        shift = (shift >> 1) | (((shift ^ (shift >> tap)) & 1u) << 14);
        c = (int32_t)period;
    }
    if (!(shift & 1u))
        acc += n;
    *count = c - (int32_t)n;
    z->shift = (uint16_t)shift;
    return acc * vol;
}

static void make(uint32_t n)
{
    APU *a = s_snd.apu;
    s_snd.acc_pulse += run_pulse(&a->pulse1, &s_snd.count[0], n) + run_pulse(&a->pulse2, &s_snd.count[1], n);
    s_snd.acc_tri += run_triangle(&a->triangle, &s_snd.count[2], n);
    s_snd.acc_noise += run_noise(&a->noise, &s_snd.count[3], n);
    s_snd.acc_dmc += (uint32_t)a->dmc.counter * n;
}

static void emit(void)
{
    /* the NES mixer, linear (1.0 = 100000): pulses 0.00752 per step, triangle 0.00851,
     * noise 0.00494, DMC 0.00335 */
    int32_t x = (int32_t)((752u * s_snd.acc_pulse + 851u * s_snd.acc_tri + 494u * s_snd.acc_noise +
                           335u * s_snd.acc_dmc) / s_snd.len);
    s_snd.acc_pulse = s_snd.acc_tri = s_snd.acc_noise = s_snd.acc_dmc = 0;
    s_snd.hp_y = (int32_t)(((int64_t)s_snd.hp_a * (s_snd.hp_y + x - s_snd.hp_x)) >> 16);
    s_snd.hp_x = x;
    int32_t v = s_snd.hp_y * 2 / 5; /* 1.0 -> 40000 */
    if (v > 32767)
        v = 32767;
    if (v < -32768)
        v = -32768;
    if (s_snd.n < OUT_MAX)
        s_snd.out[s_snd.n++] = (int16_t)v;
    s_snd.frac += s_snd.step_q16;
    s_snd.len = s_snd.frac >> 16;
    s_snd.frac &= 0xffffu;
    s_snd.left = s_snd.len;
}

void nes_sound_sync(void)
{
    if (!s_snd.on)
        return;
    uint32_t now = g_apu_now; /* the APU runs behind the CPU (apu_run) */
    uint32_t n = now - s_snd.last;
    if (!n)
        return;
    s_snd.last = now;
    while (n >= s_snd.left) {
        make(s_snd.left);
        n -= s_snd.left;
        emit();
    }
    if (n) {
        make(n);
        s_snd.left -= n;
    }
}

void nes_sound_set_step(uint32_t cycles_q16)
{
    if (cycles_q16 >= (8u << 16)) /* at least 8 cycles per sample */
        s_snd.step_q16 = cycles_q16;
}

void nes_sound_start(APU *apu, uint32_t rate)
{
    memset(&s_snd, 0, sizeof(s_snd));
    s_snd.apu = apu;
    s_snd.last = g_apu_now;
    s_snd.step_q16 = (uint32_t)(1789773ull * 65536u / rate);
    s_snd.frac = s_snd.step_q16 & 0xffffu;
    s_snd.len = s_snd.left = s_snd.step_q16 >> 16;
    s_snd.hp_a = (uint32_t)(65536ull * rate / (rate + 565u)); /* 1 / (1 + 2 pi 90 Hz / rate) */
    s_snd.on = true;
}

void nes_sound_stop(void)
{
    s_snd.on = false;
    s_snd.n = 0;
}

int nes_sound_take(int16_t *out, int max)
{
    int n = s_snd.n < max ? s_snd.n : max;
    memcpy(out, s_snd.out, (size_t)n * sizeof(out[0]));
    s_snd.n = 0;
    return n;
}
