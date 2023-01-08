#ifndef GS_AVDECODE_H_
#define GS_AVDECODE_H_

/*
 * Header only style....
 * #define GS_AVDECODE_IMPL
 * before including header in one and only one source file
 * to implement declared functions.
 *
 * requirers linking with ffmpeg stuff.
 * on gcc/clang that would for example be
 *       -lavcodec -lavformat -lavcodec -lswresample -lswscale -lavutil
 *
 *
 *  requires at least Lavc55.38.100 for mutex locking.
 *       https://stackoverflow.com/a/39786484
 */


#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#include <libavformat/avformat.h>

#include <libswscale/swscale.h>

#include <gs/gs.h>

#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

_Static_assert(LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 38, 100), "FFmpeg major version too low: " LIBAVCODEC_IDENT " needs Lavc55.38.100");

typedef struct gs_avdecode_ctx_s {
        const char* src_filename;
        int width, height;
        int img_sz;
        int video_stream_idx, audio_stream_idx;
        int read_next_packet;
        int alpha;
        enum AVPixelFormat pix_fmt;
        float frametime;

        AVFormatContext *fmt_ctx;
        AVCodecContext *video_dec_ctx, *audio_dec_ctx;
        AVStream *video_stream, *audio_stream;
        AVFrame *frame;
        AVPacket *pkt;
        struct SwsContext * sws;

        void* img[1];
} gs_avdecode_ctx_t;

// return zero on success
// TODO: specify weather <0 is error or if its just non-zero
// TODO: gs_avdecode_rewind
extern int gs_avdecode_init(const char* path, gs_avdecode_ctx_t* ctx, const gs_graphics_texture_desc_t* desc, gs_asset_texture_t* out);
extern int gs_avdecode_next_frame(gs_avdecode_ctx_t* ctx); // -1 if all frames read
extern void gs_avdecode_destroy(gs_avdecode_ctx_t* ctx, gs_asset_texture_t* tex);


// Multi-threading usage:
// Atomic integers are used instead of locks (for performance reasons)
//
// if new_frame is 0, then the "main" thread(s) shall not access anything
// if new_frame is -1 then the "decoder" thread shall not access anything
// if new_frame is 1, then a frame is complete and either thread can do a cmpxchg to aquire it.
//
// when the decoder thread exits it sets done to 1
typedef struct gs_avdecode_pthread_s {
        pthread_t thread;
        pthread_attr_t attr;

        gs_avdecode_ctx_t video;

        _Atomic int new_frame;
        _Atomic int loop; // TODO...
        _Atomic int done;
} gs_avdecode_pthread_t;
extern int gs_avdecode_pthread_play_video(gs_avdecode_pthread_t* ctxp, const char* path,
                                          const gs_graphics_texture_desc_t* desc, gs_asset_texture_t* out);
extern void gs_avdecode_pthread_destroy(gs_avdecode_pthread_t* ctxp, gs_asset_texture_t* tex);

#define gs_avdecode_aquire_m(_ctxp, ...)                                \
        do {                                                            \
                int __check = 1;                                        \
                if (atomic_compare_exchange_strong(&(_ctxp)->new_frame, &__check, -1)) { \
                        __VA_ARGS__                                     \
                        (_ctxp)->new_frame = 0;                         \
                }                                                       \
        } while(0)




#ifdef GS_AVDECODE_IMPL

static int
_gs_avdecode_decode_packet(gs_avdecode_ctx_t* ctx, AVCodecContext *dec, const AVPacket *pkt, int new_pkt)
{
        int ret = 0;

        if (new_pkt) {
                // submit the packet to the decoder
                ret = avcodec_send_packet(dec, pkt);
                if (ret < 0) {
                        fprintf(stderr, "gs_avdecode.h: Error submitting a packet for decoding (%s)\n", av_err2str(ret));
                        return ret;
                }
        }

        // get all the available frames from the decoder
        int valid_frame = 1;
        do {
                ret = avcodec_receive_frame(dec, ctx->frame);
                if (ret < 0) {
                        // those two return values are special and mean there is no output
                        // frame available, but there were no errors during decoding
                        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                                return 0;

                        fprintf(stderr, "gs_avdecode.h: Error during decoding (%s)\n", av_err2str(ret));
                        return ret;
                }
                if (dec->codec->type == AVMEDIA_TYPE_VIDEO) {
                        if (ctx->frame->width != ctx->width   ||
                            ctx->frame->height != ctx->height ||
                            ctx->frame->format != ctx->pix_fmt) {
                                // No support for variable size videos...
                                // To do so you would have to reallocate img
                                valid_frame = 0;
                        } else {
                                sws_scale(ctx->sws,
                                          (const uint8_t **)(ctx->frame->data), ctx->frame->linesize,
                                          0, ctx->height, (uint8_t**)ctx->img, (int[1]){ctx->width * (3 + ctx->alpha)}
                                        );
                        }
                }

                av_frame_unref(ctx->frame);
        } while (!valid_frame);

        return ret;
}

static int
_gs_avdecode_open_codec_context(
        gs_avdecode_ctx_t* ctx,
        int *stream_idx,
        AVCodecContext **dec_ctx,
        AVFormatContext *fmt_ctx,
        int* alpha,
        enum AVMediaType type
) {
        int ret, stream_index;
        AVStream *st;
        const AVCodec *dec = NULL;

        ret = av_find_best_stream(ctx->fmt_ctx, type, -1, -1, NULL, 0);
        if (ret < 0) {
                fprintf(stderr, "gs_avdecode.h: Could not find %s stream in input file '%s'\n",
                        av_get_media_type_string(type), ctx->src_filename);
                return ret;
        } else {
                stream_index = ret;
                st = ctx->fmt_ctx->streams[stream_index];

                int a = 0;
                if (alpha) {
                        AVDictionaryEntry* tag = NULL;
                        tag = av_dict_get(st->metadata, "ALPHA_MODE", tag, 0);
                        a = tag && atoi(tag->value);
                        *alpha = a;
                }

                // find decoder for the stream
                // use libvpx for transparent video
                dec = a ? (
                        AV_CODEC_ID_VP9 == st->codecpar->codec_id
                        ? avcodec_find_decoder_by_name("libvpx-vp9")
                        : avcodec_find_decoder_by_name("libvpx-vp8")
                        ) : avcodec_find_decoder(st->codecpar->codec_id);
                if (!dec) {
                        fprintf(stderr, "gs_avdecode.h: Failed to find %s codec\n",
                                av_get_media_type_string(type));
                        return AVERROR(EINVAL);
                }

                /* Allocate a codec context for the decoder */
                *dec_ctx = avcodec_alloc_context3(dec);
                if (!*dec_ctx) {
                        fprintf(stderr, "gs_avdecode.h: Failed to allocate the %s codec context\n",
                                av_get_media_type_string(type));
                        return AVERROR(ENOMEM);
                }

                /* Copy codec parameters from input stream to output codec context */
                if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
                        fprintf(stderr, "gs_avdecode.h: Failed to copy %s codec parameters to decoder context\n",
                                av_get_media_type_string(type));
                        return ret;
                }

                // Init the decoders
                if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0) {
                        fprintf(stderr, "gs_avdecode.h: Failed to open %s codec\n",
                                av_get_media_type_string(type));
                        return ret;
                }
                *stream_idx = stream_index;
        }

        return 0;
}

int
gs_avdecode_init(const char* path, gs_avdecode_ctx_t* ctx, const gs_graphics_texture_desc_t* desc, gs_asset_texture_t* out)
{
        if (!ctx) return 2;
        *ctx =  (gs_avdecode_ctx_t){0};
        ctx->video_stream_idx = -1;
        ctx->audio_stream_idx = -1;
        ctx->src_filename = path;
        ctx->read_next_packet = 1;

        /* open input file, and allocate format context */
        if (avformat_open_input(&ctx->fmt_ctx, ctx->src_filename, NULL, NULL) < 0) {
                fprintf(stderr, "gs_avdecode.h: Could not open source file %s", ctx->src_filename);
                return 1;
        }

        /* retrieve stream information */
        if (avformat_find_stream_info(ctx->fmt_ctx, NULL) < 0) {
                fprintf(stderr, "gs_avdecode.h: Could not find stream information");
                avformat_close_input(&ctx->fmt_ctx);
                return 1;
        }

        int ret = 0;
        if (_gs_avdecode_open_codec_context(ctx, &ctx->video_stream_idx, &ctx->video_dec_ctx, ctx->fmt_ctx, &ctx->alpha, AVMEDIA_TYPE_VIDEO) >= 0) {
                ctx->video_stream = ctx->fmt_ctx->streams[ctx->video_stream_idx];
                ctx->frametime = (float)ctx->video_stream->r_frame_rate.den / (float)ctx->video_stream->r_frame_rate.num;

                ctx->width = ctx->video_dec_ctx->width;
                ctx->height = ctx->video_dec_ctx->height;
                ctx->pix_fmt = ctx->video_dec_ctx->pix_fmt;
                if (ctx->alpha && ctx->pix_fmt == AV_PIX_FMT_YUV420P)
                        ctx->pix_fmt = AV_PIX_FMT_YUVA420P;
                ctx->sws = sws_getContext(ctx->width, ctx->height, ctx->pix_fmt,
                                          ctx->width, ctx->height, ctx->alpha ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24,
                                          SWS_BICUBIC, 0, 0, 0);
        }

        if (_gs_avdecode_open_codec_context(ctx, &ctx->audio_stream_idx, &ctx->audio_dec_ctx, ctx->fmt_ctx, NULL, AVMEDIA_TYPE_AUDIO) >= 0) {
                ctx->audio_stream = ctx->fmt_ctx->streams[ctx->audio_stream_idx];
        }

        // dump input information to stderr
        av_dump_format(ctx->fmt_ctx, 0, ctx->src_filename, 0);

        if (!ctx->audio_stream && !ctx->video_stream) {
                fprintf(stderr, "gs_avdecode.h: Could not find audio or video stream in the input, aborting\n");
                ret = 1;
                goto end;
        }

        ctx->frame = av_frame_alloc();
        if (!ctx->frame) {
                fprintf(stderr, "gs_avdecode.h: Could not allocate frame\n");
                ret = AVERROR(ENOMEM);
                goto end;
        }

        ctx->pkt = av_packet_alloc();
        if (!ctx->pkt) {
                fprintf(stderr, "gs_avdecode.h: Could not allocate packet\n");
                ret = AVERROR(ENOMEM);
                goto end;
        }


        ctx->img_sz = ctx->width * ctx->height * (3 + ctx->alpha);
        *ctx->img = malloc(ctx->img_sz);
        memset(*ctx->img, 150, ctx->img_sz);

        ////////////////////////////////////
        // asset_texture

        gs_asset_texture_t* t = out;
        if (t) {
                if (desc) {
                        t->desc = *desc;
                } else {
                        t->desc.format = ctx->alpha ? GS_GRAPHICS_TEXTURE_FORMAT_RGBA8 : GS_GRAPHICS_TEXTURE_FORMAT_RGB8;
                        t->desc.min_filter = GS_GRAPHICS_TEXTURE_FILTER_LINEAR;
                        t->desc.mag_filter = GS_GRAPHICS_TEXTURE_FILTER_LINEAR;
                        t->desc.wrap_s = GS_GRAPHICS_TEXTURE_WRAP_REPEAT;
                        t->desc.wrap_t = GS_GRAPHICS_TEXTURE_WRAP_REPEAT;
                }
                t->desc.width = ctx->width;
                t->desc.height = ctx->height;

                *t->desc.data = malloc(ctx->img_sz);
                memset(*t->desc.data, 150, ctx->img_sz);
                t->hndl = gs_graphics_texture_create(&t->desc);
        }

        return 0;
end:
        gs_avdecode_destroy(ctx, NULL);
        return ret;
}

int
gs_avdecode_next_frame(gs_avdecode_ctx_t* ctx)
{
        int skip_packet;
        int ret;
        do {
                skip_packet = 0;
                ret = 0;
                int new = 0;
                if (ctx->read_next_packet) {
                        if (av_read_frame(ctx->fmt_ctx, ctx->pkt) < 0) return -1;
                        ctx->read_next_packet = 0;
                        new = 1;
                }

                // check if the packet belongs to a stream we are interested in, otherwise skip it
                if (ctx->pkt->stream_index == ctx->video_stream_idx) {
                        ret = _gs_avdecode_decode_packet(ctx, ctx->video_dec_ctx, ctx->pkt, new);
                } else if (ctx->pkt->stream_index == ctx->audio_stream_idx) {
                        ret = _gs_avdecode_decode_packet(ctx, ctx->audio_dec_ctx, ctx->pkt, new);
                        skip_packet = 1;
                } else {
                        skip_packet = 1;
                }
                if (ret >= 0) {
                        av_packet_unref(ctx->pkt);
                        ctx->read_next_packet = 1;
                }
        } while (skip_packet);

        return ret;
}

void
gs_avdecode_destroy(gs_avdecode_ctx_t* ctx, gs_asset_texture_t* tex)
{
        if (!ctx) return;
        // flush the decoders
        if (ctx->video_dec_ctx)
                _gs_avdecode_decode_packet(ctx, ctx->video_dec_ctx, NULL, 1);
        if (ctx->audio_dec_ctx)
                _gs_avdecode_decode_packet(ctx, ctx->audio_dec_ctx, NULL, 1);

        avcodec_free_context(&ctx->video_dec_ctx);
        avcodec_free_context(&ctx->audio_dec_ctx);
        avformat_close_input(&ctx->fmt_ctx);
        av_packet_free(&ctx->pkt);
        sws_freeContext(ctx->sws);
        av_frame_free(&ctx->frame);

        if (tex) {
                gs_graphics_texture_destroy(tex->hndl);
                free(*tex->desc.data);
        }
}

static void*
_gs_avdecode_pthread_player(void* data)
{
        gs_avdecode_pthread_t* ctxp = data;
        gs_avdecode_ctx_t* ctx = &ctxp->video;
        ctxp->new_frame = 0;

        const float frametime = ctx->frametime * 1e3;
        int frames = 0;

        const float t_start = gs_platform_elapsed_time();

        int res = 0;
        int prerendered = 0;
        const float dt = 0.05;
        for (;;) {
                if (!prerendered && ctxp->new_frame == 0) {
                        res = gs_avdecode_next_frame(ctx);
                        prerendered = 1;
                }

                const float diff = gs_platform_elapsed_time() - t_start;

                if (diff + dt < frametime * frames
                    // also wait for the first frame, this prevents possible jitter
                    || (frames == 1 && ctxp->new_frame)) {
                        gs_platform_sleep(dt);
                        continue;
                }

                int locked = 0;
                if (ctxp->new_frame == 0) {
                        locked = 1;
                } else {
                        int check = 1;
                        locked = atomic_compare_exchange_strong(&ctxp->new_frame, &check, 0);
                }
                if (!locked)
                        continue;

                if (!prerendered) res = gs_avdecode_next_frame(ctx);

                ctxp->new_frame = 1;
                frames++;

                prerendered = 0;

                if (res < 0) break;
        }

        ctxp->done = 1;

        return NULL;
}

int
gs_avdecode_pthread_play_video(gs_avdecode_pthread_t* ctxp, const char* path,
                               const gs_graphics_texture_desc_t* desc, gs_asset_texture_t* out)
{
        if (!ctxp) return 2;
        *ctxp = (gs_avdecode_pthread_t){0};

        pthread_attr_init(&ctxp->attr);
        pthread_attr_setdetachstate(&ctxp->attr, PTHREAD_CREATE_DETACHED);
        int res = gs_avdecode_init(path, &ctxp->video, desc, out);
        if (res) return res;

        pthread_create(&ctxp->thread, &ctxp->attr, &_gs_avdecode_pthread_player, ctxp);
        // TODO: error code from pthread functions as well

        return res;
}

void
gs_avdecode_pthread_destroy(gs_avdecode_pthread_t* ctxp, gs_asset_texture_t* tex)
{

        pthread_attr_destroy(&ctxp->attr);
        gs_avdecode_destroy(&ctxp->video, tex);
}

#endif // GS_AVDECODE_IMPL

#endif // GS_AVDECODE_H_
