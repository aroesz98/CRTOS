/*
 * crtos/audio.h - sound output, /dev/audio (sai-imxrt.ko: the SAI and the board's codec).
 *
 * write() takes signed 16-bit little-endian samples, one per channel and frame (mono: the
 * same on both sides; stereo: left, right). It waits while the device's buffer is full
 * (O_NONBLOCK: -EAGAIN, or the part that fitted). Playback starts with the first write
 * and goes on with silence when the buffer runs dry; the output stops a moment after the
 * last writer closed the device and everything was played. One program at a time writes:
 * another open() for writing fails with -EBUSY. Each such open starts with 48000 Hz, 2
 * channels. Opened read-only, the device only answers the GET ioctls and the volume (a mixer
 * or a status program next to the one that plays).
 */
#ifndef CRTOS_AUDIO_H
#define CRTOS_AUDIO_H

#include <stdint.h>
#include <crtos/ioctl.h>

#define AUDIO_DEVICE        "/dev/audio"

/* 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100 or 48000 Hz; drops what is queued */
#define AUDIO_IOC_SET_RATE      _IO('A', 1)
#define AUDIO_IOC_GET_RATE      _IOR('A', 2, uint32_t)
/* 1 or 2 */
#define AUDIO_IOC_SET_CHANNELS  _IO('A', 3)
/* frames written but not played yet (the buffer and the transmitter) */
#define AUDIO_IOC_GET_QUEUED    _IOR('A', 4, uint32_t)
/* frames that fit into the buffer now */
#define AUDIO_IOC_GET_SPACE     _IOR('A', 5, uint32_t)
/* 0 (off) .. 100 for the headphones and the speaker outputs; kept across opens */
#define AUDIO_IOC_SET_VOLUME    _IO('A', 6)
#define AUDIO_IOC_GET_VOLUME    _IOR('A', 7, uint32_t)
/* waits until everything written was played */
#define AUDIO_IOC_DRAIN         _IO('A', 8)
/* drops what is queued (silence from now on) */
#define AUDIO_IOC_FLUSH         _IO('A', 9)

struct audio_stats {
    uint32_t rate;
    uint32_t buffer;            /* frames the buffer holds */
    uint32_t played;            /* frames played since the device was loaded */
    uint32_t underruns;         /* times the buffer ran dry while playing */
    uint32_t fifo_errors;       /* the transmitter found its FIFO empty (interrupt too late) */
};
#define AUDIO_IOC_GET_STATS     _IOR('A', 10, struct audio_stats)

#endif
