#include <gs/gs.h>
#include <gs/util/gs_idraw.h>
#define GS_AVDECODE_IMPL
#include "gs_avdecode.h"

static gs_avdecode_ctx_t video;
static gs_asset_texture_t tex  = {0};
static gs_immediate_draw_t gsi = {0};
static gs_command_buffer_t cb  = {0};

static const char* filename;

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
    gsi_rectvd(&gsi, gs_v2s(0.0f), fb, gs_v2s(0.f), gs_v2s(1.f), GS_COLOR_WHITE, GS_GRAPHICS_PRIMITIVE_TRIANGLES);

    int res = gs_avdecode_next_frame(&video);
    if (res) gs_quit();

    gs_graphics_texture_request_update(&cb, video.tex.hndl, &video.tex.desc);
    gsi_texture(&gsi, video.tex.hndl);
    gsi_rectvd(&gsi, gs_v2s(0.0f), fb, gs_v2s(0.f), gs_v2s(1.f), GS_COLOR_WHITE, GS_GRAPHICS_PRIMITIVE_TRIANGLES);


    gsi_renderpass_submit(&gsi, &cb, gs_v4(0, 0, fb.x, fb.y), gs_color(10, 10, 10, 255));
    gs_graphics_command_buffer_submit(&cb);
}

void app_init()
{
    cb = gs_command_buffer_new();
    gsi = gs_immediate_draw_new();

    int res = gs_avdecode_init(filename, &video, NULL);

    if (res) {
                gs_println("Unable to initialize video '%s' (error code %d)", filename, res);
                exit(1);
    }
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
                        .frame_rate = 30,
                },
                .init = app_init,
                .update = app_update,
        };
}
