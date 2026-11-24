#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "gfx.h"
#include "mouse.h"
#include "types.h"

#define FB_WIDTH 1280u
#define FB_HEIGHT 720u
#define FB_PITCH (FB_WIDTH * 4u + 32u)
#define FB_WORDS ((FB_PITCH / 4u) * FB_HEIGHT)

static u32 test_framebuffer[FB_WORDS];
volatile u8 denos_test_vga[65536];
static u8 fake_ports[65536];
static u64 fake_ticks;

void denos_test_outb(u16 port, u8 value) { fake_ports[port] = value; }
u8 denos_test_inb(u16 port) { return fake_ports[port]; }
void denos_test_outw(u16 port, u16 value) { (void)port; (void)value; }
u16 denos_test_inw(u16 port) { (void)port; return 0; }
void serial_video_reset(void) {}
int serial_try_getkey(int *key) { (void)key; return 0; }
int serial_getkey(void) { return GFX_KEY_ESCAPE; }
u64 timer_ticks(void) { return ++fake_ticks; }

static int fail(const char *message) {
    fprintf(stderr, "framebuffer test: %s\n", message);
    return 1;
}

int main(void) {
    memset(test_framebuffer, 0xA5, sizeof(test_framebuffer));
    if (gfx_configure_framebuffer((u64)(usize)test_framebuffer, FB_PITCH + 1u,
                                  FB_WIDTH, FB_HEIGHT, 32, 1,
                                  16, 8, 8, 8, 0, 8))
        return fail("unaligned framebuffer pitch must be rejected");
    if (!gfx_configure_framebuffer((u64)(usize)test_framebuffer, FB_PITCH,
                                   FB_WIDTH, FB_HEIGHT, 32, 1,
                                   16, 8, 8, 8, 0, 8))
        return fail("valid 1280x720 RGB framebuffer was rejected");
    if (!gfx_start()) return fail("graphics start failed");
    if (gfx_width() != 640 || gfx_height() != 360)
        return fail("logical canvas must be 640x360");
    if (gfx_display_width() != 1280 || gfx_display_height() != 720 ||
        !gfx_using_framebuffer())
        return fail("physical framebuffer metadata is incorrect");

    gfx_clear(gfx_rgb(255, 0, 0));
    if (test_framebuffer[0] != 0x00FF0000u ||
        test_framebuffer[(FB_HEIGHT - 1u) * (FB_PITCH / 4u) + FB_WIDTH - 1u] != 0x00FF0000u)
        return fail("RGB output did not fill the full physical display");
    if (test_framebuffer[FB_WIDTH] != 0xA5A5A5A5u)
        return fail("framebuffer row padding was overwritten");

    /* One logical pixel maps to a 2x2 block, and partial output must leave
       unrelated physical pixels untouched. */
    memset(test_framebuffer, 0xA5, sizeof(test_framebuffer));
    gfx_pixel(10, 10, gfx_rgb(0, 255, 0));
    if (test_framebuffer[20u * (FB_PITCH / 4u) + 20u] != 0x0000FF00u ||
        test_framebuffer[21u * (FB_PITCH / 4u) + 21u] != 0x0000FF00u)
        return fail("logical pixels were not scaled to the full 2x2 output block");
    if (test_framebuffer[22u * (FB_PITCH / 4u) + 20u] != 0xA5A5A5A5u)
        return fail("a one-pixel edit wrote beyond its 2x2 physical footprint");
    if (test_framebuffer[0] != 0xA5A5A5A5u)
        return fail("a one-pixel edit should not rewrite an unrelated framebuffer region");

    /* gfx_rgb must keep full 24-bit precision; the legacy palette is not the
       storage format used by the normal RGB desktop. */
    gfx_clear(gfx_rgb(234, 89, 177));
    if (test_framebuffer[0] != 0x00EA59B1u)
        return fail("true-color RGB values were unexpectedly quantized");

    /* QOI wallpaper blits use the same true-color surface and preserve the
       caller's source pitch. A 2x2 image becomes four 2x2 physical blocks. */
    {
        static const u32 source_pixels[6] = {
            0x00FF0000u, 0x0000FF00u, 0x12345678u,
            0x000000FFu, 0x00FFFFFFu, 0x87654321u
        };
        gfx_clear(gfx_rgb(0, 0, 0));
        gfx_blit_rgb32(30, 40, 2, 2, source_pixels, 3);
        if (test_framebuffer[80u * (FB_PITCH / 4u) + 60u] != 0x00FF0000u ||
            test_framebuffer[80u * (FB_PITCH / 4u) + 62u] != 0x0000FF00u ||
            test_framebuffer[82u * (FB_PITCH / 4u) + 60u] != 0x000000FFu ||
            test_framebuffer[82u * (FB_PITCH / 4u) + 62u] != 0x00FFFFFFu)
            return fail("RGB32 image blit did not preserve source pixels and pitch");
        if (test_framebuffer[0] != 0x00000000u)
            return fail("RGB32 image blit rewrote pixels outside its dirty rectangle");
    }

    /* The anti-aliased font blends intermediate edge shades into a true-color
       surface, rather than rendering only a hard-edged one-bit glyph. */
    gfx_clear(gfx_rgb(18, 20, 28));
    gfx_text_ui(20, 20, "A", gfx_rgb(255, 255, 255));
    {
        int visible = 0, antialiased = 0;
        for (u32 y = 40; y < 72; y++) {
            for (u32 x = 40; x < 64; x++) {
                u32 pixel = test_framebuffer[y * (FB_PITCH / 4u) + x] & 0x00FFFFFFu;
                if (pixel != 0x0012141Cu) visible++;
                if (pixel != 0x0012141Cu && pixel != 0x00FFFFFFu) antialiased++;
            }
        }
        if (visible < 40 || antialiased < 4)
            return fail("UI font did not produce visible anti-aliased glyph edges");
    }

    /* RGBA blitting correctly blends partial alpha, preserves transparent
       pixels, and replaces fully opaque pixels. */
    {
        static const u32 rgba[3] = { 0x80FF0000u, 0x0000FF00u, 0xFFFF0000u };
        gfx_clear(gfx_rgb(0, 0, 255));
        gfx_blit_rgba32(50, 60, 3, 1, rgba, 3);
        u32 mixed = (u32)gfx_get_pixel(50, 60);
        u32 untouched = (u32)gfx_get_pixel(51, 60);
        u32 opaque = (u32)gfx_get_pixel(52, 60);
        if (((mixed >> 16) & 255u) < 126u || ((mixed >> 16) & 255u) > 129u ||
            (mixed & 255u) < 126u || (mixed & 255u) > 129u)
            return fail("RGBA alpha blend did not mix source and destination channels");
        if (untouched != 0x000000FFu || opaque != 0x00FF0000u)
            return fail("RGBA blit mishandled transparent or opaque pixels");
    }

    /* Scaled RGB/RGBA blits use nearest-neighbor sampling and alpha composition. */
    {
        static const u32 scaled[4] = { 0x00FF0000u, 0x0000FF00u,
                                       0x000000FFu, 0x00FFFFFFu };
        gfx_clear(gfx_rgb(0, 0, 0));
        gfx_blit_scaled_rgb32(70, 70, 2, 2, 4, 4, scaled, 2);
        if ((u32)gfx_get_pixel(70, 70) != 0x00FF0000u ||
            (u32)gfx_get_pixel(73, 70) != 0x0000FF00u ||
            (u32)gfx_get_pixel(70, 73) != 0x000000FFu ||
            (u32)gfx_get_pixel(73, 73) != 0x00FFFFFFu)
            return fail("scaled RGB image sampling did not map source quadrants");
        static const u32 alpha_scaled[1] = { 0x80FF0000u };
        gfx_clear(gfx_rgb(0, 0, 255));
        gfx_blit_scaled_rgba32(80, 80, 1, 1, 3, 2, alpha_scaled, 1);
        u32 scaled_blend = (u32)gfx_get_pixel(82, 81);
        if (((scaled_blend >> 16) & 255u) < 126u ||
            ((scaled_blend >> 16) & 255u) > 129u ||
            (scaled_blend & 255u) < 126u || (scaled_blend & 255u) > 129u)
            return fail("scaled RGBA image did not blend partial alpha correctly");
    }

    /* The expanded primitive set draws rounded rectangles, ellipses, and
       gradients on the same logical RGB surface. */
    gfx_clear(gfx_rgb(0, 0, 0));
    gfx_fill_rounded_rect(90, 90, 40, 30, 7, gfx_rgb(0, 255, 0));
    if ((u32)gfx_get_pixel(105, 105) != 0x0000FF00u ||
        (u32)gfx_get_pixel(90, 90) != 0x00000000u)
        return fail("rounded rectangle fill or corner clipping failed");
    gfx_fill_ellipse(160, 110, 12, 7, gfx_rgb(255, 255, 0));
    if ((u32)gfx_get_pixel(160, 110) != 0x00FFFF00u ||
        (u32)gfx_get_pixel(178, 110) != 0x00000000u)
        return fail("filled ellipse geometry or clipping failed");
    gfx_gradient_rect(190, 90, 12, 10, gfx_rgb(0, 0, 0), gfx_rgb(255, 255, 255), 1);
    if ((u32)gfx_get_pixel(195, 90) != 0x00000000u ||
        (u32)gfx_get_pixel(195, 99) != 0x00FFFFFFu)
        return fail("vertical gradient endpoints were not preserved");
    gfx_rounded_rect(220, 90, 28, 22, 5, gfx_rgb(255, 0, 255));
    if ((u32)gfx_get_pixel(220, 90) == 0x00FF00FFu ||
        (u32)gfx_get_pixel(234, 90) != 0x00FF00FFu)
        return fail("rounded rectangle outline geometry failed");

    gfx_stop();
    puts("framebuffer tests: PASS");
    return 0;
}
