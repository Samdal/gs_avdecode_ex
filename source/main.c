#include <gs/gs.h>
#include <gs/util/gs_idraw.h>

#define GS_AVDECODE_IMPL
#include "gs_avdecode.h"

static gs_immediate_draw_t gsi;
static gs_command_buffer_t cb;

static const char* filename;

static gs_avdecode_context_t video;
 gs_asset_texture_t tex;

static gs_avdecode_pthread_t pvideo = {.loop = 1};
static gs_asset_texture_t ptex;

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
        gs_avdecode_request_upload_to_texture(&cb, &video, &tex);

        gsi_texture(&gsi, tex.hndl);
        gsi_rectvd(&gsi, gs_v2s(0.0f), fb, gs_v2s(0.f), gs_v2s(1.f), GS_COLOR_WHITE, GS_GRAPHICS_PRIMITIVE_TRIANGLES);

        static int paused;
        if (gs_platform_key_pressed(GS_KEYCODE_SPACE)) {
                static int toggle;
                toggle = !toggle;
                if (toggle) {
                        int check = AVDECODE_RUNNING;
                        paused = atomic_compare_exchange_strong(&pvideo.state, &check, AVDECODE_STOP);
                } else {
                        int check = AVDECODE_DONE;
                        int play = atomic_compare_exchange_strong(&pvideo.state, &check, AVDECODE_START);
                        if (play) paused = 0;
                }
        }

        if (pvideo.new_frame == AVDECODE_FRAME_COMPLETE) {
                gs_avdecode_try_request_upload_to_texture(&cb, &pvideo, &ptex);
        } else if (pvideo.state == AVDECODE_DONE && !paused) {
#if 1
                pvideo.state = AVDECODE_DIE;
                // while(pvideo.state != AVDECODE_DEAD) ;
                gs_quit();
#else
                gs_avdecode_seek(&pvideo.video, INT64_MIN, AVDECODE_SEEK_BACKWARD);
                pvideo.state = AVDECODE_START;
#endif
        }

        gsi_texture(&gsi, ptex.hndl);
        gsi_rectvd(&gsi, gs_v2(fb.x/2, fb.y/2), gs_v2(fb.x/2, fb.y/2), gs_v2s(0.f), gs_v2s(1.f), GS_COLOR_WHITE, GS_GRAPHICS_PRIMITIVE_TRIANGLES);

        gsi_renderpass_submit(&gsi, &cb, gs_v4(0, 0, fb.x, fb.y), gs_color(10, 10, 10, 255));
        gs_graphics_command_buffer_submit(&cb);
}

void app_init()
{
        cb = gs_command_buffer_new();
        gsi = gs_immediate_draw_new();

        av_log_set_level(AV_LOG_QUIET); // remove stdout messages

        int res = gs_avdecode_init(filename, &video, NULL, &tex);

        int res2 = gs_avdecode_pthread_play_video(&pvideo, filename, 1, NULL, &ptex);

        if (res || res2) {
                gs_println("Unable to initialize video '%s' (error code %d)", filename, res);
                exit(1);
        }
}

void app_shutdown()
{
        gs_avdecode_destroy(&video, &tex);
        gs_avdecode_destroy(&pvideo.video, &ptex);

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
