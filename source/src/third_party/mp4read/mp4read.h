/****************************************************************************
    MP4 input module (instance-based wrapper for libZFork)

    Based on faad2 frontend/mp4read by Krzysztof Nikiel (GPLv3).
****************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t len;
    uint32_t offset;
} frame_info_t;

typedef struct
{
    uint32_t firstchunk;
    uint32_t samplesperchunk;
} slice_info_t;

typedef struct
{
    uint32_t ctime, mtime;
    uint32_t samplerate;
    uint32_t samples;
    uint32_t channels;
    uint32_t bits;
    uint32_t buffersize;
    uint32_t bitratemax;
    uint32_t bitrateavg;
    struct
    {
        frame_info_t *info;
        slice_info_t *map;
        uint32_t nsamples;
        uint32_t nsclices;
        uint32_t current;
        uint32_t maxsize;
    } frame;
    struct
    {
        uint8_t buf[10];
        uint32_t size;
    } asc;
    struct {
        uint32_t size;
        uint8_t *data;
    } bitbuf;
    struct {
        int header;
        int tags;
    } verbose;
} mp4config_t;

typedef struct Mp4ReadContext Mp4ReadContext;

Mp4ReadContext* mp4read_create(void);
void            mp4read_destroy(Mp4ReadContext* ctx);

int  mp4read_open(Mp4ReadContext* ctx, const char* name);
int  mp4read_seek(Mp4ReadContext* ctx, uint32_t framenum);
int  mp4read_frame(Mp4ReadContext* ctx);
int  mp4read_close(Mp4ReadContext* ctx);

const mp4config_t* mp4read_config(const Mp4ReadContext* ctx);

#ifdef __cplusplus
}
#endif
