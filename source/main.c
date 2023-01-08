#include <gs/gs.h>
#include <gs/util/gs_idraw.h>
#define GS_AVDECODE_IMPL
#include "gs_avdecode.h"

static gs_immediate_draw_t gsi;
static gs_command_buffer_t cb;

static const char* filename;

static gs_asset_texture_t tex;
static gs_asset_texture_t ptex;
static gs_avdecode_ctx_t video;
static gs_avdecode_pthread_t pvideo;
static pthread_t video_thread;

void app_update()
{
        const gs_vec2 fb = gs_platform_framebuffer_sizev(gs_platform_main_window());

        gsi_defaults(&gsi);
        gsi_camera2D(&gsi, fb.x, fb.y);
        const float t = gs_platform_elapsed_time() * 0.0001f;

        gsi_camera3D(&gsi, fb.x, fb.y);
        gsi_rotatev(&gsi, gs_deg2rad(90.f), GS_ZAXIS); gsi_rotatev(&gsi, t, GS_YAXIS);
        gsi_sphere(&gsi, 0.f, 0.f, 0.f, 1.f, 50, 150, 200, 50, GS_GRAPHICS_PRIMITIVE_LINES);

        gsi_defaults(&gsi);
        gsi_camera2D(&gsi, fb.x, fb.y);

        int res = gs_avdecode_next_frame(&video);
        memcpy(*tex.desc.data, *video.img, video.img_sz);
        gs_graphics_texture_request_update(&cb, tex.hndl, &tex.desc);

        gsi_texture(&gsi, tex.hndl);
        gsi_rectvd(&gsi, gs_v2s(0.0f), fb, gs_v2s(0.f), gs_v2s(1.f), GS_COLOR_WHITE, GS_GRAPHICS_PRIMITIVE_TRIANGLES);

        if (!pvideo.done) {
                gs_avdecode_aquire_m(&pvideo,
                        memcpy(*ptex.desc.data, *pvideo.video.img, pvideo.video.img_sz);
                        gs_graphics_texture_request_update(&cb, ptex.hndl, &ptex.desc);
                );

                gsi_texture(&gsi, ptex.hndl);
                gsi_rectvd(&gsi, gs_v2(fb.x/2, fb.y/2), gs_v2(fb.x/2, fb.y/2), gs_v2s(0.f), gs_v2s(1.f), GS_COLOR_WHITE, GS_GRAPHICS_PRIMITIVE_TRIANGLES);
        } else if (pvideo.done > 0) {
                gs_avdecode_pthread_destroy(&pvideo, &video_thread, &ptex);
                pvideo.done = -1;
        }

        gsi_renderpass_submit(&gsi, &cb, gs_v4(0, 0, fb.x, fb.y), gs_color(10, 10, 10, 255));
        gs_graphics_command_buffer_submit(&cb);
}

void app_init()
{
        cb = gs_command_buffer_new();
        gsi = gs_immediate_draw_new();

        int res = gs_avdecode_init(filename, &video, NULL, &tex);

        int res2 = gs_avdecode_pthread_play_video(&pvideo, &video_thread, filename, NULL, &ptex);

        if (res) {
                gs_println("Unable to initialize video '%s' (error code %d)", filename, res);
                exit(1);
        }
}

void app_shutdown()
{
        gs_avdecode_destroy(&video, &tex);

        gs_immediate_draw_free(&gsi);
        gs_command_buffer_free(&cb);
}

gs_app_desc_t
gs_main(int32_t argc, char** argv)
{
        if (argc != 2) {
                gs_println("----\nInvalid amount of arguments!\nUsage: ./App your-video");
                exit(1);
        }

        filename = strdup(argv[1]);;

        return (gs_app_desc_t) {
                .window = {
                        .width = 800,
                        .height = 600,
                },
                .init = app_init,
                .update = app_update,
                .shutdown = app_shutdown,
        };
}
