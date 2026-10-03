/*
 * audio - the sound output (/dev/audio): its state, the volume, a test tone, WAV files.
 *
 *     audio                        rate, volume, frames played, underruns
 *     audio -v N                   volume 0..100 (headphones and speakers)
 *     audio tone [HZ [MS]]         a sine tone (default 440 Hz, 1000 ms)
 *     audio play FILE.wav          16-bit PCM, mono or stereo, 8000..48000 Hz
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/audio.h>

/* @play: for writing (one program at a time), else read-only (status, volume) */
static int open_audio(int play)
{
    int fd = open(AUDIO_DEVICE, play ? O_WRONLY : O_RDONLY);
    if (fd < 0)
        perror(AUDIO_DEVICE);
    return fd;
}

/* sin(2 pi phase / 2^32) * 32767: a parabola through 0, pi/2 and pi, corrected (error
 * below 0.1 %) */
static int16_t sine(uint32_t phase)
{
    int32_t t = (int32_t)phase >> 16; /* -32768..32767 for -pi..pi */
    int32_t at = t < 0 ? -t : t;
    int32_t y = 4 * t - ((t * at) >> 13); /* 4t - 4t|t| in Q15 */
    int32_t ay = y < 0 ? -y : y;
    y += (((y * ay) >> 15) - y) * 225 / 1000;
    if (y > 32767)
        y = 32767;
    if (y < -32767)
        y = -32767;
    return (int16_t)y;
}

static int tone(int fd, uint32_t hz, uint32_t ms)
{
    uint32_t rate = 48000;
    ioctl(fd, AUDIO_IOC_SET_RATE, rate);
    ioctl(fd, AUDIO_IOC_SET_CHANNELS, 1);
    uint32_t step = (uint32_t)(((uint64_t)hz << 32) / rate), phase = 0;
    uint32_t frames = rate / 1000u * ms;
    int16_t buf[480];
    while (frames) {
        uint32_t n = frames < 480 ? frames : 480;
        for (uint32_t i = 0; i < n; i++) {
            buf[i] = (int16_t)(sine(phase) / 4); /* -12 dB */
            phase += step;
        }
        if (write(fd, buf, n * 2u) < 0) {
            perror("audio");
            return 1;
        }
        frames -= n;
    }
    ioctl(fd, AUDIO_IOC_DRAIN, 0);
    return 0;
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int play(int fd, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 1;
    }
    uint8_t h[12], ck[8], fmt[16];
    uint32_t rate = 0, channels = 0, bits = 0, format = 0;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        printf("audio: %s is not a WAV file\n", path);
        fclose(f);
        return 1;
    }
    for (;;) {
        if (fread(ck, 1, 8, f) != 8) {
            printf("audio: %s has no sound data\n", path);
            fclose(f);
            return 1;
        }
        uint32_t size = le32(ck + 4);
        if (!memcmp(ck, "fmt ", 4) && size >= 16) {
            if (fread(fmt, 1, 16, f) != 16)
                break;
            format = fmt[0] | fmt[1] << 8;
            channels = fmt[2] | fmt[3] << 8;
            rate = le32(fmt + 4);
            bits = fmt[14] | fmt[15] << 8;
            fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            break;
        } else {
            fseek(f, (long)(size + (size & 1)), SEEK_CUR);
        }
    }
    if (format != 1 || bits != 16 || (channels != 1 && channels != 2)) {
        printf("audio: only 16-bit PCM, mono or stereo (this one: format %lu, %lu bits, %lu channels)\n",
               (unsigned long)format, (unsigned long)bits, (unsigned long)channels);
        fclose(f);
        return 1;
    }
    if (ioctl(fd, AUDIO_IOC_SET_RATE, rate) < 0) {
        printf("audio: %lu Hz is not possible\n", (unsigned long)rate);
        fclose(f);
        return 1;
    }
    ioctl(fd, AUDIO_IOC_SET_CHANNELS, channels);
    printf("audio: %s, %lu Hz, %s\n", path, (unsigned long)rate, channels == 1 ? "mono" : "stereo");
    static uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        if (write(fd, buf, n) < 0) {
            perror("audio");
            break;
        }
    fclose(f);
    ioctl(fd, AUDIO_IOC_DRAIN, 0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "-v")) {
        int fd = open_audio(0);
        if (fd < 0)
            return 1;
        int r = ioctl(fd, AUDIO_IOC_SET_VOLUME, (uint32_t)atoi(argv[2]));
        close(fd);
        return r < 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "tone")) {
        int fd = open_audio(1);
        if (fd < 0)
            return 1;
        int r = tone(fd, argc > 2 ? (uint32_t)atoi(argv[2]) : 440u, argc > 3 ? (uint32_t)atoi(argv[3]) : 1000u);
        close(fd);
        return r;
    }
    if (argc >= 3 && !strcmp(argv[1], "play")) {
        int fd = open_audio(1);
        if (fd < 0)
            return 1;
        int r = play(fd, argv[2]);
        close(fd);
        return r;
    }
    if (argc > 1) {
        printf("usage: audio [-v 0..100 | tone [HZ [MS]] | play FILE.wav]\n");
        return 2;
    }
    int fd = open_audio(0);
    if (fd < 0)
        return 1;
    struct audio_stats st;
    uint32_t vol = 0;
    ioctl(fd, AUDIO_IOC_GET_STATS, &st);
    ioctl(fd, AUDIO_IOC_GET_VOLUME, &vol);
    close(fd);
    printf("audio: %lu Hz, volume %lu, buffer %lu frames, played %lu, underruns %lu, FIFO errors %lu\n",
           (unsigned long)st.rate, (unsigned long)vol, (unsigned long)st.buffer, (unsigned long)st.played,
           (unsigned long)st.underruns, (unsigned long)st.fifo_errors);
    return 0;
}
