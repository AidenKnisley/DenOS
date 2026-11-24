#include "gfx.h"
#include "io.h"
#include "serial.h"
#include "types.h"
#include "timer.h"
#include "mouse.h"
#include "font_ui.h"

#define GFX_RGB_TAG 0x01000000u
#define GFX_PALETTE_TAG 0x80000000u
#define GFX_MAX_W 640
/* Full-detail logical desktop; VGA fallback activates 320x200 within this stride. */
#define GFX_MAX_H 360
static i32 gfx_screen_width = 320;
static i32 gfx_screen_height = 200;
#define GFX_W gfx_screen_width
#define GFX_H gfx_screen_height
#define GFX_STRIDE GFX_MAX_W
#ifdef DENOS_GRAPHICS_TESTING
extern volatile u8 denos_test_vga[65536];
#define VGA_MEMORY denos_test_vga
#define GFX_FONT_MEMORY denos_test_vga
#else
#define VGA_MEMORY ((volatile u8 *)0xA0000)
#define GFX_FONT_MEMORY ((volatile u8 *)0xA0000)
#endif

#define GFX_MAX_WINDOWS 4
#define GFX_MAX_DESKTOP_ICONS 8
#define GFX_EVENT_QUEUE_SIZE 32
#define GFX_DRAG_NONE 0
#define GFX_DRAG_MOVE 1
#define GFX_DRAG_RESIZE 2
#define GFX_CONTENT_X 6
#define GFX_CONTENT_Y 24
#define GFX_TASKBAR_HEIGHT 18

typedef struct gfx_window_state {
    int used;
    int minimized;
    int id;
    i64 x, y, width, height;
    char title[48];
    /* Each window owns a maximum-size 32-bit true-color client buffer.
       Coordinates exposed to applications remain screen coordinates; the
       target origin maps them into this local surface. */
    u32 surface[GFX_MAX_W * GFX_MAX_H];
} gfx_window_state_t;

typedef struct gfx_desktop_icon_state {
    int used;
    int id;
    i64 x, y;
    u32 color;
    char label[24];
} gfx_desktop_icon_state_t;

typedef struct gfx_event_record {
    i32 type;
    i32 key;
    i32 x;
    i32 y;
    i32 buttons;
    i32 window_id;
    i32 code;
} gfx_event_record_t;

static int graphics_active;
static u32 desktop_color = GFX_PALETTE_TAG | BLUE;
static gfx_window_state_t gfx_windows[GFX_MAX_WINDOWS];
static int gfx_window_order[GFX_MAX_WINDOWS]; /* back to front */
static int gfx_window_count_value;
static int gfx_active_slot = -1;
static int gfx_next_window_id = 1;
static int gfx_window_dragging;
static int gfx_drag_mode;
static i64 gfx_drag_offset_x, gfx_drag_offset_y;
static i64 gfx_drag_start_mouse_x, gfx_drag_start_mouse_y;
static i64 gfx_drag_start_width, gfx_drag_start_height;
static int gfx_drag_slot = -1;
static u32 gfx_back_buffer[GFX_MAX_W * GFX_MAX_H];
static u32 desktop_snapshot[GFX_MAX_W * GFX_MAX_H];
static int desktop_snapshot_valid;
static int desktop_active;
static gfx_desktop_icon_state_t desktop_icons[GFX_MAX_DESKTOP_ICONS];
static int desktop_icon_count;
static int desktop_next_icon_id = 1;
static int desktop_clicked_id;
static volatile u32 *gfx_draw_target;
static i32 gfx_draw_origin_x, gfx_draw_origin_y;
static mouse_state_t gfx_mouse_state;
static u8 gfx_mouse_pressed_state;
static u8 gfx_mouse_released_state;
static int gfx_mouse_event_pending;
static int gfx_click_pending;
static i32 gfx_click_x, gfx_click_y;
static int gfx_rendering;
static int gfx_draw_depth;
static int gfx_frame_depth;
static int gfx_frame_dirty;
/* Bounding box for ordinary drawing changes. Small edits are copied only to
   the affected display region; structural window/desktop changes still use a
   full-scene present. Bounds are half-open logical coordinates. */
static int gfx_dirty_valid;
static i32 gfx_dirty_left, gfx_dirty_top, gfx_dirty_right, gfx_dirty_bottom;
static gfx_event_record_t gfx_event_queue[GFX_EVENT_QUEUE_SIZE];
static unsigned int gfx_event_head;
static unsigned int gfx_event_tail;
static unsigned int gfx_event_count;
static gfx_event_record_t gfx_current_event;
static int gfx_pending_event_type;
static void render_desktop_scene(void);
static void render_desktop_scene_damage(i32 left, i32 top, i32 right, i32 bottom);
static void cursor_restore(void);
static void cursor_draw(void);
static void gfx_present_now(void);
static void gfx_mark_dirty(i64 left, i64 top, i64 right, i64 bottom);
static void gfx_request_scene_redraw(void);
/* VGA stores text glyphs in plane 2. Mode 13h reuses that memory, so preserve
   it and put it back before returning the machine to text mode. */
static u8 saved_font_plane[65536];
static int font_plane_saved;
static int gfx_framebuffer_configured;
static int gfx_framebuffer_active;
static volatile u32 *gfx_framebuffer;
static u32 gfx_framebuffer_pitch;
static u32 gfx_framebuffer_width;
static u32 gfx_framebuffer_height;
static u8 gfx_red_position, gfx_red_size;
static u8 gfx_green_position, gfx_green_size;
static u8 gfx_blue_position, gfx_blue_size;
static u8 gfx_rgb_palette[256][3];
static u32 gfx_palette_color[256];
static i32 gfx_cursor_last_x = -1;
static i32 gfx_cursor_last_y = -1;
static int gfx_cursor_last_valid;
/* Coalesce multiple PS/2 packets into one desktop composition per input pump. */
static int gfx_input_pumping;
static int gfx_scene_redraw_pending;


/* Standard VGA register groups for 320x200x256 (mode 13h). */
static const u8 mode13_seq[] = {0x03, 0x01, 0x0F, 0x00, 0x0E};
static const u8 mode13_crtc[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF
};
static const u8 mode13_gc[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
};
static const u8 mode13_ac[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00
};

/* Standard 80x25 color text mode (mode 03h), used when the app exits. */
static const u8 text_seq[] = {0x03, 0x00, 0x03, 0x00, 0x02};
static const u8 text_crtc[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};
static const u8 text_gc[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
};
static const u8 text_ac[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0C, 0x00, 0x0F, 0x08
};

static void write_registers(u8 misc, const u8 *seq, const u8 *crtc,
                            const u8 *gc, const u8 *ac) {
    outb(0x3C2, misc);

    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01); /* Sequencer reset while registers are changed. */
    for (u8 i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, seq[i]);
    }
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03); /* Release sequencer reset. */

    outb(0x3D4, 0x03);
    outb(0x3D5, (u8)(crtc[3] | 0x80)); /* Unlock CRTC registers 0-7. */
    outb(0x3D4, 0x11);
    outb(0x3D5, (u8)(crtc[0x11] & 0x7F));
    for (u8 i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    for (u8 i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, gc[i]);
    }

    /* Attribute Controller uses the 3DA status read to reset its flip-flop. */
    for (u8 i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, i < 20 ? ac[i] : 0x00);
    }
    (void)inb(0x3DA);
    outb(0x3C0, 0x20); /* Display enabled. */
}

static void select_font_plane(void) {
    /* Map plane 2 at A0000 so its original bytes can be copied safely. */
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x04); /* Sequencer map mask: plane 2. */
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x06); /* Disable chain/odd-even addressing. */
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x02); /* Read map select: plane 2. */
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x00); /* Read/write mode 0. */
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x04); /* A0000 64 KiB memory window. */
    outb(0x3CE, 0x08);
    outb(0x3CF, 0xFF); /* Allow writes to every bit. */
}

static void save_text_font(void) {
    volatile u8 *plane = GFX_FONT_MEMORY;
    select_font_plane();
    for (u32 i = 0; i < 65536u; i++) saved_font_plane[i] = plane[i];
    font_plane_saved = 1;
}

static void restore_text_font(void) {
    if (!font_plane_saved) return;
    volatile u8 *plane = GFX_FONT_MEMORY;
    select_font_plane();
    for (u32 i = 0; i < 65536u; i++) plane[i] = saved_font_plane[i];
    font_plane_saved = 0;
}

static void build_palette(void) {
    static const u8 dos[16][3] = {
        {0,0,0}, {0,0,170}, {0,170,0}, {0,170,170},
        {170,0,0}, {170,0,170}, {170,85,0}, {170,170,170},
        {85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
        {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
    };
    for (int i = 0; i < 16; i++)
        for (int channel = 0; channel < 3; channel++)
            gfx_rgb_palette[i][channel] = dos[i][channel];
    /* A compact 6x6x6 RGB cube occupies indices 16..231. */
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                int index = 16 + r * 36 + g * 6 + b;
                gfx_rgb_palette[index][0] = (u8)(r * 51);
                gfx_rgb_palette[index][1] = (u8)(g * 51);
                gfx_rgb_palette[index][2] = (u8)(b * 51);
            }
        }
    }
    for (int i = 0; i < 24; i++) {
        u8 gray = (u8)(8 + i * 10);
        gfx_rgb_palette[232 + i][0] = gray;
        gfx_rgb_palette[232 + i][1] = gray;
        gfx_rgb_palette[232 + i][2] = gray;
    }
    for (int i = 0; i < 256; i++) {
        gfx_palette_color[i] = ((u32)gfx_rgb_palette[i][0] << 16) |
                               ((u32)gfx_rgb_palette[i][1] << 8) |
                               (u32)gfx_rgb_palette[i][2];
    }
}

static void set_palette(void) {
    build_palette();
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        outb(0x3C9, (u8)(gfx_rgb_palette[i][0] >> 2));
        outb(0x3C9, (u8)(gfx_rgb_palette[i][1] >> 2));
        outb(0x3C9, (u8)(gfx_rgb_palette[i][2] >> 2));
    }
}

static u32 color_value(i64 color) {
    /* Keep palette colors tagged so legacy VGA mode can preserve exact
       indices. Already-resolved internal colors must remain idempotent. */
    if (color >= 0 && (((u64)color & GFX_PALETTE_TAG) != 0))
        return (u32)color;
    /* True-color values carry a separate tag and retain all 24 bits. */
    if (color >= 0 && (((u64)color & GFX_RGB_TAG) != 0))
        return (u32)color & 0x00FFFFFFu;
    if (color < 0 || color > 255) return GFX_PALETTE_TAG;
    return GFX_PALETTE_TAG | (u32)(u8)color;
}

static u32 gfx_color_to_rgb(u32 color) {
    if (color & GFX_PALETTE_TAG)
        return gfx_palette_color[color & 255u];
    return color & 0x00FFFFFFu;
}

long gfx_rgb(long red, long green, long blue) {
    if (red < 0) red = 0;
    if (red > 255) red = 255;
    if (green < 0) green = 0;
    if (green > 255) green = 255;
    if (blue < 0) blue = 0;
    if (blue > 255) blue = 255;
    return (long)(GFX_RGB_TAG | ((u32)red << 16) | ((u32)green << 8) | (u32)blue);
}

int gfx_configure_framebuffer(u64 address, u32 pitch, u32 width, u32 height,
                              u8 bits_per_pixel, u8 framebuffer_type,
                              u8 red_position, u8 red_mask_size,
                              u8 green_position, u8 green_mask_size,
                              u8 blue_position, u8 blue_mask_size) {
    u64 bytes;
#ifndef DENOS_GRAPHICS_TESTING
    const u64 address_limit = 0x100000000ULL;
    if (graphics_active || address == 0 || address >= address_limit ||
#else
    if (graphics_active || address == 0 ||
#endif
        width < 1280u || height < 720u || width > 4096u || height > 2160u ||
        bits_per_pixel != 32u || framebuffer_type != 1u ||
        pitch < width * 4u || (pitch & 3u) != 0u ||
        red_mask_size != 8u || green_mask_size != 8u || blue_mask_size != 8u ||
        red_position + red_mask_size > 32u ||
        green_position + green_mask_size > 32u || blue_position + blue_mask_size > 32u ||
        (red_position < green_position + green_mask_size &&
         green_position < red_position + red_mask_size) ||
        (red_position < blue_position + blue_mask_size &&
         blue_position < red_position + red_mask_size) ||
        (green_position < blue_position + blue_mask_size &&
         blue_position < green_position + green_mask_size))
        return 0;
    bytes = (u64)pitch * height;
#ifndef DENOS_GRAPHICS_TESTING
    if (bytes == 0 || address + bytes > address_limit || address + bytes < address)
        return 0;
#else
    if (bytes == 0 || address + bytes < address) return 0;
#endif
    /* The boot code identity-maps physical memory below 4 GiB. */
    gfx_framebuffer = (volatile u32 *)(usize)address;
    gfx_framebuffer_pitch = pitch;
    gfx_framebuffer_width = width;
    gfx_framebuffer_height = height;
    gfx_red_position = red_position;
    gfx_red_size = red_mask_size;
    gfx_green_position = green_position;
    gfx_green_size = green_mask_size;
    gfx_blue_position = blue_position;
    gfx_blue_size = blue_mask_size;
    gfx_framebuffer_configured = 1;
    return 1;
}

static u32 pack_component(u8 value, u8 position, u8 size) {
    u32 maximum;
    if (size >= 8u) maximum = 255u;
    else maximum = (1u << size) - 1u;
    return (((u32)value * maximum + 127u) / 255u) << position;
}

static u32 pack_rgb(u32 color) {
    u32 rgb = gfx_color_to_rgb(color);
    u8 red = (u8)(rgb >> 16);
    u8 green = (u8)(rgb >> 8);
    u8 blue = (u8)rgb;
    if (gfx_red_position == 16u && gfx_green_position == 8u && gfx_blue_position == 0u)
        return rgb & 0x00FFFFFFu;
    return pack_component(red, gfx_red_position, gfx_red_size) |
           pack_component(green, gfx_green_position, gfx_green_size) |
           pack_component(blue, gfx_blue_position, gfx_blue_size);
}

static u8 gfx_color_to_vga(u32 color) {
    if (color & GFX_PALETTE_TAG) return (u8)(color & 255u);
    u32 rgb = gfx_color_to_rgb(color);
    u32 red = (rgb >> 16) & 255u;
    u32 green = (rgb >> 8) & 255u;
    u32 blue = rgb & 255u;
    u32 r = (red * 5u + 127u) / 255u;
    u32 g = (green * 5u + 127u) / 255u;
    u32 b = (blue * 5u + 127u) / 255u;
    return (u8)(16u + r * 36u + g * 6u + b);
}

static void gfx_fill_buffer(u32 *buffer, u32 color) {
    /* A flat contiguous loop is easier for GCC to vectorize than nested
       per-row loops and covers only the active logical surface. */
    u32 count = (u32)GFX_STRIDE * (u32)GFX_H;
    for (u32 i = 0; i < count; i++) buffer[i] = color;
}

static void gfx_copy_buffer(u32 *destination, const u32 *source) {
    u32 count = (u32)GFX_STRIDE * (u32)GFX_H;
    for (u32 i = 0; i < count; i++) destination[i] = source[i];
}

static int cursor_shape_pixel(i32 x, i32 y, u32 *color) {
    static const char shape[12][9] = {
        "1.......", "11......", "121.....", "1221....",
        "12221...", "122221..", "1222221.", "12211111",
        "121.1...", "11..1...", "1...1...", "....11.."
    };
    i32 cx = x - gfx_mouse_state.x;
    i32 cy = y - gfx_mouse_state.y;
    if (!mouse_is_available() || cx < 0 || cx >= 8 || cy < 0 || cy >= 12)
        return 0;
    if (shape[cy][cx] == '1') { if (color) *color = color_value(WHITE); return 1; }
    if (shape[cy][cx] == '2') { if (color) *color = color_value(BLACK); return 1; }
    return 0;
}

static void gfx_write_physical_pixel(i32 logical_x, i32 logical_y, u32 color) {
    if (logical_x < 0 || logical_x >= GFX_W || logical_y < 0 || logical_y >= GFX_H)
        return;
    if (gfx_framebuffer_active && gfx_framebuffer) {
        u32 pitch_pixels = gfx_framebuffer_pitch / 4u;
        u32 packed = pack_rgb(color);
        if (gfx_framebuffer_width == (u32)GFX_W * 2u &&
            gfx_framebuffer_height == (u32)GFX_H * 2u) {
            u32 x = (u32)logical_x * 2u;
            u32 y = (u32)logical_y * 2u;
            volatile u32 *row0 = gfx_framebuffer + y * pitch_pixels;
            volatile u32 *row1 = row0 + pitch_pixels;
            row0[x] = packed; row0[x + 1u] = packed;
            row1[x] = packed; row1[x + 1u] = packed;
            return;
        }
        u32 x0 = (u32)(((u64)(u32)logical_x * gfx_framebuffer_width) / (u32)GFX_W);
        u32 y0 = (u32)(((u64)(u32)logical_y * gfx_framebuffer_height) / (u32)GFX_H);
        u32 x1 = (u32)((((u64)(u32)(logical_x + 1) * gfx_framebuffer_width) + (u32)GFX_W - 1u) / (u32)GFX_W);
        u32 y1 = (u32)((((u64)(u32)(logical_y + 1) * gfx_framebuffer_height) + (u32)GFX_H - 1u) / (u32)GFX_H);
        for (u32 py = y0; py < y1 && py < gfx_framebuffer_height; py++) {
            volatile u32 *row = gfx_framebuffer + py * pitch_pixels;
            for (u32 px = x0; px < x1 && px < gfx_framebuffer_width; px++)
                row[px] = packed;
        }
    } else {
        VGA_MEMORY[(u32)logical_y * 320u + (u32)logical_x] = gfx_color_to_vga(color);
    }
}

static void gfx_output_logical_region(i32 x, i32 y, i32 width, i32 height) {
    i32 left = x < 0 ? 0 : x;
    i32 top = y < 0 ? 0 : y;
    i32 right = x + width;
    i32 bottom = y + height;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (left >= right || top >= bottom) return;
    for (i32 py = top; py < bottom; py++) {
        u32 row = (u32)py * GFX_STRIDE;
        for (i32 px = left; px < right; px++)
            gfx_write_physical_pixel(px, py, gfx_back_buffer[row + (u32)px]);
    }
}

static void gfx_draw_cursor_now(void) {
    if (!mouse_is_available()) {
        gfx_cursor_last_valid = 0;
        return;
    }
    for (i32 y = 0; y < 12; y++) {
        for (i32 x = 0; x < 8; x++) {
            u32 color;
            i32 px = gfx_mouse_state.x + x;
            i32 py = gfx_mouse_state.y + y;
            if (cursor_shape_pixel(px, py, &color))
                gfx_write_physical_pixel(px, py, color);
        }
    }
    gfx_cursor_last_x = gfx_mouse_state.x;
    gfx_cursor_last_y = gfx_mouse_state.y;
    gfx_cursor_last_valid = 1;
}

static void gfx_output_cursor_update(void) {
    if (!graphics_active) return;
    /* Restore only the previous 8x12 cursor footprint from the back buffer.
       Mouse movement no longer causes a full 1280x720 framebuffer copy. */
    if (gfx_cursor_last_valid) {
        gfx_output_logical_region(gfx_cursor_last_x, gfx_cursor_last_y, 8, 12);
        gfx_cursor_last_valid = 0;
    }
    gfx_draw_cursor_now();
}

static void gfx_output_buffer(void) {
    if (!graphics_active) return;
    if (gfx_framebuffer_active && gfx_framebuffer) {
        u32 pitch_pixels = gfx_framebuffer_pitch / 4u;
        if (gfx_framebuffer_width == (u32)GFX_W * 2u &&
            gfx_framebuffer_height == (u32)GFX_H * 2u) {
            /* Native 2x presentation: one logical pixel becomes a 2x2 block.
               All rendering is true-color; no palette conversion occurs for
               the common 0xAARRGGBB framebuffer layout. */
            for (u32 sy = 0; sy < (u32)GFX_H; sy++) {
                const u32 *source = gfx_back_buffer + sy * GFX_STRIDE;
                volatile u32 *row0 = gfx_framebuffer + (sy * 2u) * pitch_pixels;
                volatile u32 *row1 = row0 + pitch_pixels;
                for (u32 sx = 0; sx < (u32)GFX_W; sx++) {
                    u32 pixel = pack_rgb(source[sx]);
                    u32 dx = sx * 2u;
                    row0[dx] = pixel; row0[dx + 1u] = pixel;
                    row1[dx] = pixel; row1[dx + 1u] = pixel;
                }
            }
        } else {
            u32 y_step = (u32)((((u64)(u32)GFX_H << 16) +
                                gfx_framebuffer_height - 1u) / gfx_framebuffer_height);
            u32 y_fixed = 0;
            for (u32 y = 0; y < gfx_framebuffer_height; y++) {
                u32 sy = y_fixed >> 16;
                u32 x_step = (u32)((((u64)(u32)GFX_W << 16) +
                                    gfx_framebuffer_width - 1u) / gfx_framebuffer_width);
                u32 x_fixed = 0;
                volatile u32 *row = gfx_framebuffer + y * pitch_pixels;
                const u32 *source = gfx_back_buffer + sy * GFX_STRIDE;
                for (u32 x = 0; x < gfx_framebuffer_width; x++) {
                    row[x] = pack_rgb(source[x_fixed >> 16]);
                    x_fixed += x_step;
                }
                y_fixed += y_step;
            }
        }
    } else {
        for (i32 y = 0; y < GFX_H; y++) {
            u32 row = (u32)y * GFX_STRIDE;
            u32 destination = (u32)y * 320u;
            for (i32 x = 0; x < GFX_W; x++)
                VGA_MEMORY[destination + (u32)x] = gfx_color_to_vga(gfx_back_buffer[row + (u32)x]);
        }
    }
    gfx_cursor_last_valid = 0;
    gfx_draw_cursor_now();
}

static void gfx_set_draw_target(volatile u32 *target, i32 origin_x, i32 origin_y) {
    gfx_draw_target = target ? target : gfx_back_buffer;
    gfx_draw_origin_x = origin_x;
    gfx_draw_origin_y = origin_y;
}

/* Keep the drawing destination synchronized with focus even when a frame is
   open and the compositor must defer the visible redraw until gfx_end_frame. */
static void gfx_update_draw_target(void) {
    if (gfx_active_slot >= 0 && gfx_active_slot < GFX_MAX_WINDOWS &&
        gfx_windows[gfx_active_slot].used) {
        gfx_window_state_t *active = &gfx_windows[gfx_active_slot];
        gfx_set_draw_target(active->surface,
                            (i32)(active->x + GFX_CONTENT_X),
                            (i32)(active->y + GFX_CONTENT_Y));
    } else if (desktop_active && desktop_snapshot_valid) {
        gfx_set_draw_target(desktop_snapshot, 0, 0);
    } else {
        gfx_set_draw_target(gfx_back_buffer, 0, 0);
    }
}

static void gfx_target_pixel(i64 x, i64 y, u32 color) {
    i64 local_x = x - gfx_draw_origin_x;
    i64 local_y = y - gfx_draw_origin_y;
    if (!gfx_draw_target || local_x < 0 || local_x >= GFX_W ||
        local_y < 0 || local_y >= GFX_H) return;
    u32 target_index = (u32)local_y * GFX_STRIDE + (u32)local_x;
    gfx_draw_target[target_index] = color;
    if (!gfx_rendering) gfx_mark_dirty(x, y, x + 1, y + 1);
    /* Drawing always targets RAM. A complete frame is composited and copied
       to VGA only at a present boundary, so the visible screen never exposes
       a half-rendered collection of application/window updates. */
    if (!gfx_rendering) gfx_frame_dirty = 1;
}

static void gfx_target_fill(i64 x, i64 y, i64 width, i64 height, u32 color) {
    if (!gfx_draw_target || width <= 0 || height <= 0) return;
    /* Convert screen coordinates to the selected client surface once, then
       write clipped spans directly. This avoids a function call and multiple
       bounds checks for every pixel in a rectangle. */
    i64 left = x - gfx_draw_origin_x;
    i64 top = y - gfx_draw_origin_y;
    i64 right = left + width;
    i64 bottom = top + height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (left >= right || top >= bottom) return;
    for (i64 py = top; py < bottom; py++) {
        volatile u32 *destination = gfx_draw_target + (u32)py * GFX_STRIDE + (u32)left;
        for (i64 px = left; px < right; px++) *destination++ = color;
    }
    if (!gfx_rendering) {
        gfx_mark_dirty(left + gfx_draw_origin_x, top + gfx_draw_origin_y,
                       right + gfx_draw_origin_x, bottom + gfx_draw_origin_y);
        gfx_frame_dirty = 1;
    }
}

static void cursor_restore(void) {
    /* The pointer is composited as an overlay during each present. Keeping it
       out of client buffers means redraws never need to repair saved pixels. */
}

static void cursor_draw(void) {
    if (!graphics_active || gfx_frame_depth > 0 || gfx_rendering) return;
    gfx_output_cursor_update();
}

static void gfx_queue_event(int type, int key, int x, int y,
                            int buttons, int window_id, int code) {
    if (type == GFX_EVENT_NONE) return;
    if (gfx_event_count >= GFX_EVENT_QUEUE_SIZE) {
        /* A full queue drops its oldest event; the newest input remains useful
           and the ring never overwrites unread memory. */
        gfx_event_head = (gfx_event_head + 1u) % GFX_EVENT_QUEUE_SIZE;
        gfx_event_count--;
    }
    gfx_event_record_t *event = &gfx_event_queue[gfx_event_tail];
    event->type = type;
    event->key = key;
    event->x = x;
    event->y = y;
    event->buttons = buttons;
    event->window_id = window_id;
    event->code = code;
    gfx_event_tail = (gfx_event_tail + 1u) % GFX_EVENT_QUEUE_SIZE;
    gfx_event_count++;
}

static int gfx_pop_event(void) {
    if (!gfx_event_count) return GFX_EVENT_NONE;
    gfx_current_event = gfx_event_queue[gfx_event_head];
    gfx_event_head = (gfx_event_head + 1u) % GFX_EVENT_QUEUE_SIZE;
    gfx_event_count--;
    return gfx_current_event.type;
}

static void gfx_copy_back_buffer(void) {
    gfx_output_buffer();
}

static void gfx_dirty_reset(void) {
    gfx_dirty_valid = 0;
    gfx_dirty_left = GFX_W;
    gfx_dirty_top = GFX_H;
    gfx_dirty_right = 0;
    gfx_dirty_bottom = 0;
}

static void gfx_mark_dirty(i64 left, i64 top, i64 right, i64 bottom) {
    if (right <= left || bottom <= top) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (left >= right || top >= bottom) return;
    if (!gfx_dirty_valid) {
        gfx_dirty_left = (i32)left;
        gfx_dirty_top = (i32)top;
        gfx_dirty_right = (i32)right;
        gfx_dirty_bottom = (i32)bottom;
        gfx_dirty_valid = 1;
        return;
    }
    if (left < gfx_dirty_left) gfx_dirty_left = (i32)left;
    if (top < gfx_dirty_top) gfx_dirty_top = (i32)top;
    if (right > gfx_dirty_right) gfx_dirty_right = (i32)right;
    if (bottom > gfx_dirty_bottom) gfx_dirty_bottom = (i32)bottom;
}

static void gfx_output_dirty_region(i32 left, i32 top, i32 right, i32 bottom) {
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (left >= right || top >= bottom) return;

    /* A larger rectangle is cheaper through the sequential full-frame path.
       Keep sparse edits on the region path, especially one-pixel animation. */
    i32 width = right - left;
    i32 height = bottom - top;
    if ((i64)width * height >= ((i64)GFX_W * GFX_H) / 3) {
        gfx_output_buffer();
        return;
    }

    if (gfx_framebuffer_active && gfx_framebuffer &&
        gfx_framebuffer_width == (u32)GFX_W * 2u &&
        gfx_framebuffer_height == (u32)GFX_H * 2u) {
        u32 pitch_pixels = gfx_framebuffer_pitch / 4u;
        for (i32 sy = top; sy < bottom; sy++) {
            const u32 *source = gfx_back_buffer + (u32)sy * GFX_STRIDE;
            volatile u32 *row0 = gfx_framebuffer + ((u32)sy * 2u) * pitch_pixels;
            volatile u32 *row1 = row0 + pitch_pixels;
            for (i32 sx = left; sx < right; sx++) {
                u32 pixel = pack_rgb(source[sx]);
                u32 dx = (u32)sx * 2u;
                row0[dx] = pixel; row0[dx + 1u] = pixel;
                row1[dx] = pixel; row1[dx + 1u] = pixel;
            }
        }
    } else {
        gfx_output_logical_region(left, top, width, height);
    }
    /* Re-apply the pointer over a changed region. If the region did not touch
       it, this only rewrites its tiny 8x12 footprint, never the full display. */
    gfx_draw_cursor_now();
}

static void gfx_present_now(void) {
    if (!graphics_active || gfx_frame_depth > 0) return;
    int partial = gfx_dirty_valid;
    i32 left = partial ? gfx_dirty_left : 0;
    i32 top = partial ? gfx_dirty_top : 0;
    i32 right = partial ? gfx_dirty_right : GFX_W;
    i32 bottom = partial ? gfx_dirty_bottom : GFX_H;
    gfx_frame_dirty = 0;
    gfx_dirty_reset();
    if (desktop_active || gfx_window_count_value > 0) {
        if (partial) render_desktop_scene_damage(left, top, right, bottom);
        else render_desktop_scene();
    } else if (partial) {
        gfx_output_dirty_region(left, top, right, bottom);
    } else {
        gfx_copy_back_buffer();
    }
}

static void gfx_draw_begin(void) {
    if (gfx_rendering) return;
    if (gfx_draw_depth == 0) cursor_restore();
    gfx_draw_depth++;
}

static void gfx_draw_end(void) {
    if (gfx_rendering || gfx_draw_depth <= 0) return;
    gfx_draw_depth--;
    if (gfx_draw_depth == 0 && gfx_frame_depth == 0 && gfx_frame_dirty)
        gfx_present_now();
}

static void gfx_queue_focus(int slot) {
    int id = (slot >= 0 && slot < GFX_MAX_WINDOWS && gfx_windows[slot].used)
        ? gfx_windows[slot].id : 0;
    gfx_queue_event(GFX_EVENT_FOCUS, 0, gfx_mouse_state.x, gfx_mouse_state.y,
                    gfx_mouse_state.buttons, id, id);
}

static int window_slot_from_id(long id) {
    if (id <= 0) return -1;
    for (int i = 0; i < GFX_MAX_WINDOWS; i++)
        if (gfx_windows[i].used && (long)gfx_windows[i].id == id) return i;
    return -1;
}

static int top_visible_window(void) {
    for (int order = gfx_window_count_value - 1; order >= 0; order--) {
        int slot = gfx_window_order[order];
        if (slot >= 0 && slot < GFX_MAX_WINDOWS && gfx_windows[slot].used &&
            !gfx_windows[slot].minimized) return slot;
    }
    return -1;
}

static void save_desktop(void) {
    gfx_copy_buffer(desktop_snapshot, gfx_back_buffer);
    desktop_snapshot_valid = 1;
}

static void draw_desktop_icon(const gfx_desktop_icon_state_t *icon) {
    if (!icon || !icon->used) return;
    long x = (long)icon->x;
    long y = (long)icon->y;
    gfx_fill_rect(x + 7, y, 20, 19, icon->color);
    gfx_rect(x + 7, y, 20, 19, WHITE);
    gfx_line(x + 10, y + 4, x + 23, y + 4, LIGHT_GRAY);
    gfx_line(x + 10, y + 8, x + 23, y + 8, LIGHT_GRAY);
    gfx_text(x, y + 23, icon->label, WHITE);
}

static void draw_taskbar(void) {
    gfx_fill_rect(0, GFX_H - GFX_TASKBAR_HEIGHT, GFX_W, GFX_TASKBAR_HEIGHT, DARK_GRAY);
    gfx_line(0, GFX_H - GFX_TASKBAR_HEIGHT, GFX_W - 1,
             GFX_H - GFX_TASKBAR_HEIGHT, WHITE);
    gfx_button(3, GFX_H - 15, 36, 12, "DENOS", 0);
    for (int order = 0; order < gfx_window_count_value; order++) {
        int slot = gfx_window_order[order];
        if (slot < 0 || slot >= GFX_MAX_WINDOWS || !gfx_windows[slot].used) continue;
        long x = 43 + order * 68;
        if (x + 65 > GFX_W - 3) break;
        char label[11];
        int n = 0;
        while (gfx_windows[slot].title[n] && n < 9) {
            label[n] = gfx_windows[slot].title[n];
            n++;
        }
        label[n] = 0;
        gfx_button(x, GFX_H - 15, 64, 12, label,
                   gfx_active_slot == slot);
    }
}

static void gfx_fill_rounded_box(long x, long y, long width, long height,
                                 long radius, long color) {
    if (width <= 0 || height <= 0) return;
    if (radius < 1) {
        gfx_fill_rect(x, y, width, height, color);
        return;
    }
    if (radius * 2 > width) radius = width / 2;
    if (radius * 2 > height) radius = height / 2;
    gfx_fill_rect(x + radius, y, width - radius * 2, height, color);
    gfx_fill_rect(x, y + radius, width, height - radius * 2, color);
    gfx_fill_circle(x + radius, y + radius, radius, color);
    gfx_fill_circle(x + width - radius - 1, y + radius, radius, color);
    gfx_fill_circle(x + radius, y + height - radius - 1, radius, color);
    gfx_fill_circle(x + width - radius - 1, y + height - radius - 1, radius, color);
}

static void draw_window_frame(int slot) {
    gfx_window_state_t *window = &gfx_windows[slot];
    long x = (long)window->x;
    long y = (long)window->y;
    long w = (long)window->width;
    long h = (long)window->height;
    long shadow = gfx_rgb(13, 10, 18);
    long body = gfx_rgb(250, 250, 252);
    long border = gfx_rgb(204, 204, 204);
    long title = gfx_active_slot == slot ? gfx_rgb(255, 102, 0)
                                         : gfx_rgb(102, 51, 102);
    long muted = gfx_rgb(240, 240, 240);

    /* Rounded, layered window chrome: restrained shadow, bright application
       surface, accent title bar, and compact traffic-light controls. */
    gfx_fill_rounded_box(x + 2, y + 3, w, h, 7, shadow);
    gfx_fill_rounded_box(x, y, w, h, 7, body);
    gfx_rect(x + 1, y + 1, w - 2, h - 2, border);
    gfx_fill_rounded_box(x + 1, y + 1, w - 2, 24, 6, title);
    gfx_fill_rect(x + 1, y + 13, w - 2, 12, title);
    gfx_line(x + 2, y + 24, x + w - 3, y + 24, border);
    char display_title[32];
    long title_capacity = (w - 66) / GFX_UI_FONT_ADVANCE;
    if (title_capacity < 1) title_capacity = 1;
    if (title_capacity > (long)sizeof(display_title) - 1)
        title_capacity = (long)sizeof(display_title) - 1;
    long title_length = 0;
    while (title_length < title_capacity && window->title[title_length]) {
        display_title[title_length] = window->title[title_length];
        title_length++;
    }
    display_title[title_length] = 0;
    gfx_text_ui_bold(x + 10, y + 5, display_title,
                     gfx_active_slot == slot ? WHITE : muted);

    if (desktop_active) {
        gfx_fill_circle(x + w - 29, y + 11, 4, gfx_rgb(255, 204, 51));
        gfx_fill_rect(x + w - 31, y + 11, 5, 1, gfx_rgb(51, 51, 51));
    }
    gfx_fill_circle(x + w - 14, y + 11, 4, gfx_rgb(255, 51, 51));
    gfx_line(x + w - 16, y + 9, x + w - 12, y + 13, WHITE);
    gfx_line(x + w - 12, y + 9, x + w - 16, y + 13, WHITE);

    gfx_fill_rect(x + 4, y + 24, w - 8, h - 28, gfx_rgb(255, 255, 255));
    gfx_line(x + 4, y + h - 3, x + w - 5, y + h - 3, border);
    gfx_line(x + w - 9, y + h - 4, x + w - 4, y + h - 9, muted);
    gfx_line(x + w - 7, y + h - 4, x + w - 4, y + h - 7, muted);
}

static void render_desktop_scene_internal(int partial,
                                          i32 left, i32 top,
                                          i32 right, i32 bottom) {
    if (!graphics_active || gfx_rendering || gfx_frame_depth > 0) return;
    gfx_rendering = 1;
    cursor_restore();
    gfx_set_draw_target(gfx_back_buffer, 0, 0);
    if (desktop_snapshot_valid) gfx_copy_buffer(gfx_back_buffer, desktop_snapshot);
    else gfx_fill_buffer(gfx_back_buffer, desktop_color);

    /* Recompose layers in order so a dirty client update cannot punch through
       a window above it. Only the final physical copy is region-limited. */
    for (int i = 0; i < GFX_MAX_DESKTOP_ICONS; i++)
        if (desktop_icons[i].used) draw_desktop_icon(&desktop_icons[i]);

    for (int order = 0; order < gfx_window_count_value; order++) {
        int slot = gfx_window_order[order];
        if (slot < 0 || slot >= GFX_MAX_WINDOWS || !gfx_windows[slot].used ||
            gfx_windows[slot].minimized) continue;
        gfx_window_state_t *window = &gfx_windows[slot];
        draw_window_frame(slot);
        long content_width = (long)window->width - 12;
        long content_height = (long)window->height - 30;
        long screen_x = (long)window->x + GFX_CONTENT_X;
        long screen_y = (long)window->y + GFX_CONTENT_Y;
        for (long py = 0; py < content_height; py++) {
            u32 destination = (u32)(screen_y + py) * GFX_STRIDE + (u32)screen_x;
            u32 source = (u32)py * GFX_STRIDE;
            for (long px = 0; px < content_width; px++)
                gfx_back_buffer[destination + (u32)px] = window->surface[source + (u32)px];
        }
    }
    if (desktop_active) draw_taskbar();
    gfx_update_draw_target();
    gfx_rendering = 0;
    gfx_frame_dirty = 0;
    if (partial) {
        gfx_output_dirty_region(left, top, right, bottom);
    } else {
        gfx_output_buffer();
    }
}

static void render_desktop_scene(void) {
    render_desktop_scene_internal(0, 0, 0, GFX_W, GFX_H);
}

static void gfx_request_scene_redraw(void) {
    if (gfx_input_pumping) {
        gfx_scene_redraw_pending = 1;
        return;
    }
    render_desktop_scene();
}

static void render_desktop_scene_damage(i32 left, i32 top, i32 right, i32 bottom) {
    render_desktop_scene_internal(1, left, top, right, bottom);
}

static void raise_window(int slot) {
    if (slot < 0 || slot >= GFX_MAX_WINDOWS || !gfx_windows[slot].used) return;
    int focus_changed = gfx_active_slot != slot || gfx_windows[slot].minimized;
    int found = -1;
    for (int i = 0; i < gfx_window_count_value; i++)
        if (gfx_window_order[i] == slot) found = i;
    if (found >= 0) {
        for (int i = found; i + 1 < gfx_window_count_value; i++)
            gfx_window_order[i] = gfx_window_order[i + 1];
        gfx_window_order[gfx_window_count_value - 1] = slot;
    }
    gfx_windows[slot].minimized = 0;
    gfx_active_slot = slot;
    gfx_update_draw_target();
    if (focus_changed) gfx_queue_focus(slot);
    gfx_request_scene_redraw();
}

static void close_window_slot(int slot) {
    if (slot < 0 || slot >= GFX_MAX_WINDOWS || !gfx_windows[slot].used) return;
    cursor_restore();
    int closed_id = gfx_windows[slot].id;
    int found = -1;
    for (int i = 0; i < gfx_window_count_value; i++)
        if (gfx_window_order[i] == slot) found = i;
    if (found >= 0) {
        for (int i = found; i + 1 < gfx_window_count_value; i++)
            gfx_window_order[i] = gfx_window_order[i + 1];
        gfx_window_count_value--;
    }
    gfx_windows[slot].used = 0;
    if (gfx_drag_slot == slot) {
        gfx_drag_slot = -1;
        gfx_window_dragging = 0;
        gfx_drag_mode = GFX_DRAG_NONE;
    }
    if (gfx_active_slot == slot)
        gfx_active_slot = top_visible_window();
    gfx_update_draw_target();
    gfx_queue_event(GFX_EVENT_CLOSE, 0, gfx_mouse_state.x, gfx_mouse_state.y,
                    gfx_mouse_state.buttons, closed_id, closed_id);
    if (gfx_window_count_value == 0 && !desktop_active) {
        if (desktop_snapshot_valid) {
            gfx_copy_buffer(gfx_back_buffer, desktop_snapshot);
            gfx_output_buffer();
        }
        desktop_snapshot_valid = 0;
        /* A close click may have queued a focus redraw immediately before
           destroying the last window. Do not let the deferred redraw replace
           the just-restored pre-window desktop with the default background. */
        gfx_scene_redraw_pending = 0;
        gfx_set_draw_target(gfx_back_buffer, 0, 0);
        if (gfx_active_slot >= 0) gfx_queue_focus(gfx_active_slot);
        return;
    }
    if (gfx_active_slot >= 0) gfx_queue_focus(gfx_active_slot);
    gfx_request_scene_redraw();
}

static int point_in_rect(i32 px, i32 py, i64 x, i64 y, i64 width, i64 height) {
    return px >= x && py >= y && (i64)px < x + width && (i64)py < y + height;
}

static int handle_window_mouse(const mouse_state_t *state) {
    if (!state) return 0;
    gfx_pending_event_type = GFX_EVENT_MOUSE;

    if (state->pressed & MOUSE_BUTTON_LEFT) {
        gfx_click_pending = 1;
        gfx_click_x = state->x;
        gfx_click_y = state->y;
        desktop_clicked_id = 0;

        /* Taskbar buttons restore and focus the exact window under the pointer. */
        if (desktop_active && state->y >= GFX_H - 15 && state->y < GFX_H - 3) {
            for (int order = 0; order < gfx_window_count_value; order++) {
                long button_x = 43 + order * 68;
                if (point_in_rect(state->x, state->y, button_x, GFX_H - 15, 64, 12)) {
                    int slot = gfx_window_order[order];
                    int changed = gfx_active_slot != slot || gfx_windows[slot].minimized;
                    raise_window(slot);
                    if (changed) gfx_pending_event_type = GFX_EVENT_FOCUS;
                    return GFX_EVENT_WINDOW;
                }
            }
        }

        /* Topmost windows receive pointer input first. A click raises the
           selected window, making it the keyboard focus owner as well. */
        for (int order = gfx_window_count_value - 1; order >= 0; order--) {
            int slot = gfx_window_order[order];
            if (slot < 0 || slot >= GFX_MAX_WINDOWS || !gfx_windows[slot].used ||
                gfx_windows[slot].minimized) continue;
            gfx_window_state_t *window = &gfx_windows[slot];
            if (!point_in_rect(state->x, state->y, window->x, window->y,
                               window->width, window->height)) continue;

            int focus_changed = gfx_active_slot != slot;
            raise_window(slot);
            if (focus_changed) gfx_pending_event_type = GFX_EVENT_FOCUS;

            i64 close_x = window->x + window->width - 17;
            if (point_in_rect(state->x, state->y, close_x, window->y + 5, 11, 11)) {
                gfx_pending_event_type = GFX_EVENT_CLOSE;
                close_window_slot(slot);
                return gfx_window_count_value > 0 || desktop_active
                    ? GFX_EVENT_WINDOW : GFX_KEY_ESCAPE;
            }
            if (desktop_active &&
                point_in_rect(state->x, state->y, window->x + window->width - 31,
                              window->y + 5, 11, 11)) {
                int old_id = window->id;
                window->minimized = 1;
                gfx_active_slot = top_visible_window();
                gfx_update_draw_target();
                gfx_window_dragging = 0;
                gfx_drag_mode = GFX_DRAG_NONE;
                gfx_drag_slot = -1;
                gfx_pending_event_type = GFX_EVENT_STATE;
                gfx_queue_event(GFX_EVENT_STATE, 0, state->x, state->y,
                                state->buttons, old_id, 1);
                if (gfx_active_slot >= 0) gfx_queue_focus(gfx_active_slot);
                else gfx_queue_focus(-1);
                gfx_request_scene_redraw();
                return GFX_EVENT_WINDOW;
            }

            /* The bottom-right grip starts a resize operation. The current
               size and pointer position are captured to avoid jumpy edges. */
            if (point_in_rect(state->x, state->y,
                              window->x + window->width - 12,
                              window->y + window->height - 12, 12, 12)) {
                gfx_window_dragging = 1;
                gfx_drag_mode = GFX_DRAG_RESIZE;
                gfx_drag_slot = slot;
                gfx_drag_start_mouse_x = state->x;
                gfx_drag_start_mouse_y = state->y;
                gfx_drag_start_width = window->width;
                gfx_drag_start_height = window->height;
            } else if (point_in_rect(state->x, state->y, window->x + 3,
                                     window->y + 3, window->width - 6, 18)) {
                gfx_window_dragging = 1;
                gfx_drag_mode = GFX_DRAG_MOVE;
                gfx_drag_slot = slot;
                gfx_drag_offset_x = (i64)state->x - window->x;
                gfx_drag_offset_y = (i64)state->y - window->y;
            }
            return focus_changed ? GFX_EVENT_WINDOW : GFX_EVENT_MOUSE;
        }

        int had_focus = gfx_active_slot >= 0;
        gfx_active_slot = -1;
        gfx_update_draw_target();
        gfx_window_dragging = 0;
        gfx_drag_mode = GFX_DRAG_NONE;
        gfx_drag_slot = -1;
        if (desktop_active) {
            for (int i = 0; i < GFX_MAX_DESKTOP_ICONS; i++) {
                gfx_desktop_icon_state_t *icon = &desktop_icons[i];
                if (icon->used && point_in_rect(state->x, state->y,
                                                icon->x, icon->y, 40, 36)) {
                    desktop_clicked_id = icon->id;
                    break;
                }
            }
        }
        if (had_focus) {
            gfx_pending_event_type = GFX_EVENT_FOCUS;
            gfx_queue_focus(-1);
        }
        gfx_request_scene_redraw();
        return GFX_EVENT_MOUSE;
    }

    if (gfx_window_dragging && gfx_drag_slot >= 0 &&
        gfx_windows[gfx_drag_slot].used) {
        gfx_window_state_t *window = &gfx_windows[gfx_drag_slot];
        if (state->buttons & MOUSE_BUTTON_LEFT) {
            if (gfx_drag_mode == GFX_DRAG_MOVE) {
                i64 new_x = (i64)state->x - gfx_drag_offset_x;
                i64 new_y = (i64)state->y - gfx_drag_offset_y;
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (new_x + window->width > GFX_W) new_x = GFX_W - window->width;
                i64 max_bottom = desktop_active ? GFX_H - GFX_TASKBAR_HEIGHT : GFX_H;
                if (new_y + window->height > max_bottom)
                    new_y = max_bottom - window->height;
                if (new_x != window->x || new_y != window->y) {
                    window->x = new_x;
                    window->y = new_y;
                    gfx_request_scene_redraw();
                }
            } else if (gfx_drag_mode == GFX_DRAG_RESIZE) {
                i64 new_width = gfx_drag_start_width +
                    ((i64)state->x - gfx_drag_start_mouse_x);
                i64 new_height = gfx_drag_start_height +
                    ((i64)state->y - gfx_drag_start_mouse_y);
                i64 max_width = GFX_W - window->x;
                i64 max_height = (desktop_active ? GFX_H - GFX_TASKBAR_HEIGHT : GFX_H) - window->y;
                if (new_width < GFX_WINDOW_MIN_WIDTH) new_width = GFX_WINDOW_MIN_WIDTH;
                if (new_height < GFX_WINDOW_MIN_HEIGHT) new_height = GFX_WINDOW_MIN_HEIGHT;
                if (new_width > max_width) new_width = max_width;
                if (new_height > max_height) new_height = max_height;
                if (new_width != window->width || new_height != window->height) {
                    window->width = new_width;
                    window->height = new_height;
                    gfx_pending_event_type = GFX_EVENT_RESIZE;
                    gfx_queue_event(GFX_EVENT_RESIZE, 0, state->x, state->y,
                                    state->buttons, window->id, 0);
                    gfx_request_scene_redraw();
                }
            }
        } else {
            gfx_window_dragging = 0;
            gfx_drag_mode = GFX_DRAG_NONE;
            gfx_drag_slot = -1;
        }
    }
    return 0;
}

int gfx_start(void) {
    if (graphics_active) return 1;
    gfx_framebuffer_active = gfx_framebuffer_configured ? 1 : 0;
    build_palette();
    desktop_color = color_value(BLUE);
    if (gfx_framebuffer_active) {
        gfx_screen_width = 640;
        gfx_screen_height = 360;
    } else {
        gfx_screen_width = 320;
        gfx_screen_height = 200;
        save_text_font();
        write_registers(0x63, mode13_seq, mode13_crtc, mode13_gc, mode13_ac);
        set_palette();
    }
    graphics_active = 1;
    for (int i = 0; i < GFX_MAX_WINDOWS; i++) gfx_windows[i].used = 0;
    for (int i = 0; i < GFX_MAX_DESKTOP_ICONS; i++) desktop_icons[i].used = 0;
    gfx_window_count_value = 0;
    gfx_active_slot = -1;
    gfx_next_window_id = 1;
    gfx_window_dragging = 0;
    gfx_drag_mode = GFX_DRAG_NONE;
    gfx_drag_slot = -1;
    desktop_snapshot_valid = 0;
    desktop_active = 0;
    desktop_icon_count = 0;
    desktop_next_icon_id = 1;
    desktop_clicked_id = 0;
    gfx_set_draw_target(gfx_back_buffer, 0, 0);
    gfx_draw_origin_x = 0;
    gfx_draw_origin_y = 0;
    gfx_rendering = 0;
    gfx_draw_depth = 0;
    gfx_frame_depth = 0;
    gfx_frame_dirty = 0;
    gfx_dirty_reset();
    gfx_event_head = gfx_event_tail = gfx_event_count = 0;
    gfx_pending_event_type = GFX_EVENT_NONE;
    gfx_current_event.type = GFX_EVENT_NONE;
    gfx_mouse_event_pending = 0;
    gfx_click_pending = 0;
    gfx_cursor_last_valid = 0;
    gfx_cursor_last_x = -1;
    gfx_cursor_last_y = -1;
    gfx_mouse_pressed_state = 0;
    gfx_mouse_released_state = 0;
    mouse_set_bounds(GFX_W, GFX_H);
    mouse_get_state(&gfx_mouse_state);
    gfx_clear(BLACK);
    return 1;
}

void gfx_stop(void) {
    if (!graphics_active) return;
    cursor_restore();
    write_registers(0x67, text_seq, text_crtc, text_gc, text_ac);
    if (!gfx_framebuffer_active) restore_text_font();
    /* Reapply text-mode memory mapping after direct font-plane access. */
    write_registers(0x67, text_seq, text_crtc, text_gc, text_ac);
    graphics_active = 0;
    gfx_framebuffer_active = 0;
    gfx_screen_width = 320;
    gfx_screen_height = 200;
    gfx_window_count_value = 0;
    gfx_active_slot = -1;
    gfx_window_dragging = 0;
    gfx_drag_mode = GFX_DRAG_NONE;
    gfx_drag_slot = -1;
    desktop_active = 0;
    desktop_snapshot_valid = 0;
    gfx_event_head = gfx_event_tail = gfx_event_count = 0;
    gfx_frame_depth = 0;
    gfx_draw_depth = 0;
    gfx_dirty_reset();
    gfx_set_draw_target(gfx_back_buffer, 0, 0);
    serial_video_reset();
}

/* Called by the VM after every program, including a program that faults. */
void gfx_shutdown(void) {
    gfx_stop();
}

void gfx_begin_frame(void) {
    if (!graphics_active || gfx_frame_depth >= 16) return;
    if (gfx_frame_depth == 0) cursor_restore();
    gfx_frame_depth++;
}

void gfx_end_frame(void) {
    if (!graphics_active || gfx_frame_depth <= 0) return;
    gfx_frame_depth--;
    if (gfx_frame_depth == 0 && gfx_draw_depth == 0 && gfx_frame_dirty)
        gfx_present_now();
}

void gfx_present(void) {
    if (!graphics_active || gfx_frame_depth > 0) return;
    gfx_dirty_reset();
    gfx_frame_dirty = 1;
    gfx_present_now();
}

void gfx_clear(long color) {
    if (!graphics_active) return;
    cursor_restore();
    gfx_dirty_reset(); /* Clearing a target is always a full-surface update. */
    u32 value = color_value((i64)color);
    if (gfx_active_slot >= 0 && gfx_active_slot < GFX_MAX_WINDOWS &&
        gfx_windows[gfx_active_slot].used) {
        u32 *surface = gfx_windows[gfx_active_slot].surface;
        gfx_fill_buffer(surface, value);
        gfx_frame_dirty = 1;
        if (gfx_frame_depth == 0) gfx_present_now();
    } else if (desktop_active && desktop_snapshot_valid) {
        gfx_fill_buffer(desktop_snapshot, value);
        gfx_frame_dirty = 1;
        if (gfx_frame_depth == 0) gfx_present_now();
    } else {
        gfx_fill_buffer(gfx_back_buffer, value);
        gfx_frame_dirty = 1;
        if (gfx_frame_depth == 0) gfx_present_now();
    }
}

static int reasonable_point(i64 x, i64 y) {
    return x >= -4096 && x <= 4096 && y >= -4096 && y <= 4096;
}

void gfx_pixel(long x, long y, long color) {
    if (!graphics_active || x < 0 || x >= GFX_W || y < 0 || y >= GFX_H) return;
    gfx_draw_begin();
    gfx_target_pixel(x, y, color_value((i64)color));
    gfx_draw_end();
}

void gfx_blit_rgb32(long x, long y, long width, long height,
                    const u32 *pixels, long pitch_pixels) {
    if (!graphics_active || !pixels || width <= 0 || height <= 0 ||
        pitch_pixels < width || width > 4096 || height > 4096 ||
        !reasonable_point(x, y)) return;
    i64 left = x < 0 ? 0 : x;
    i64 top = y < 0 ? 0 : y;
    i64 right = x + width;
    i64 bottom = y + height;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (right <= left || bottom <= top) return;
    gfx_draw_begin();
    i64 local_left = left - gfx_draw_origin_x;
    i64 local_top = top - gfx_draw_origin_y;
    i64 local_right = right - gfx_draw_origin_x;
    i64 local_bottom = bottom - gfx_draw_origin_y;
    if (local_left < 0) left -= local_left, local_left = 0;
    if (local_top < 0) top -= local_top, local_top = 0;
    if (local_right > GFX_W) local_right = GFX_W;
    if (local_bottom > GFX_H) local_bottom = GFX_H;
    if (local_left < local_right && local_top < local_bottom) {
        for (i64 py = local_top; py < local_bottom; py++) {
            volatile u32 *destination = gfx_draw_target + (u32)py * GFX_STRIDE + (u32)local_left;
            const u32 *source = pixels + (usize)(top - y + py - local_top) * (usize)pitch_pixels + (usize)(left - x);
            for (i64 px = local_left; px < local_right; px++) *destination++ = *source++ & 0x00FFFFFFu;
        }
        if (!gfx_rendering) {
            gfx_mark_dirty(local_left + gfx_draw_origin_x, local_top + gfx_draw_origin_y,
                           local_right + gfx_draw_origin_x, local_bottom + gfx_draw_origin_y);
            gfx_frame_dirty = 1;
        }
    }
    gfx_draw_end();
}

static u32 gfx_alpha_blend(u32 foreground, u32 background, u32 alpha) {
    u32 inverse = 255u - alpha;
    u32 red = ((((foreground >> 16) & 255u) * alpha) +
               (((background >> 16) & 255u) * inverse) + 127u) / 255u;
    u32 green = ((((foreground >> 8) & 255u) * alpha) +
                 (((background >> 8) & 255u) * inverse) + 127u) / 255u;
    u32 blue = (((foreground & 255u) * alpha) +
                ((background & 255u) * inverse) + 127u) / 255u;
    return (red << 16) | (green << 8) | blue;
}

void gfx_blit_rgba32(long x, long y, long width, long height,
                     const u32 *pixels, long pitch_pixels) {
    if (!graphics_active || !pixels || width <= 0 || height <= 0 ||
        pitch_pixels < width || width > 4096 || height > 4096 ||
        !reasonable_point(x, y)) return;
    i64 left = x < 0 ? 0 : x;
    i64 top = y < 0 ? 0 : y;
    i64 right = x + width;
    i64 bottom = y + height;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (right <= left || bottom <= top) return;
    gfx_draw_begin();
    for (i64 py = top; py < bottom; py++) {
        for (i64 px = left; px < right; px++) {
            u32 source = pixels[(usize)(py - y) * (usize)pitch_pixels + (usize)(px - x)];
            u32 alpha = source >> 24;
            if (!alpha) continue;
            i64 local_x = px - gfx_draw_origin_x;
            i64 local_y = py - gfx_draw_origin_y;
            if (!gfx_draw_target || local_x < 0 || local_x >= GFX_W ||
                local_y < 0 || local_y >= GFX_H) continue;
            u32 index = (u32)local_y * GFX_STRIDE + (u32)local_x;
            u32 output = alpha == 255u ? (source & 0x00FFFFFFu) :
                gfx_alpha_blend(source & 0x00FFFFFFu,
                                gfx_color_to_rgb(gfx_draw_target[index]), alpha);
            gfx_target_pixel(px, py, output);
        }
    }
    gfx_draw_end();
}

static int gfx_scaled_blit_valid(long x, long y, long source_width, long source_height,
                                 long destination_width, long destination_height,
                                 const u32 *pixels, long pitch_pixels) {
    return graphics_active && pixels && source_width > 0 && source_height > 0 &&
           destination_width > 0 && destination_height > 0 &&
           source_width <= 4096 && source_height <= 4096 &&
           destination_width <= 4096 && destination_height <= 4096 &&
           pitch_pixels >= source_width && reasonable_point(x, y);
}

void gfx_blit_scaled_rgb32(long x, long y, long source_width, long source_height,
                           long destination_width, long destination_height,
                           const u32 *pixels, long pitch_pixels) {
    if (!gfx_scaled_blit_valid(x, y, source_width, source_height,
                               destination_width, destination_height, pixels, pitch_pixels)) return;
    i64 left = x < 0 ? 0 : x;
    i64 top = y < 0 ? 0 : y;
    i64 right = (i64)x + destination_width;
    i64 bottom = (i64)y + destination_height;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (right <= left || bottom <= top) return;
    gfx_draw_begin();
    for (i64 py = top; py < bottom; ++py) {
        i64 local_y = py - gfx_draw_origin_y;
        if (local_y < 0 || local_y >= GFX_H) continue;
        u64 sy = (u64)(py - y) * (u64)source_height / (u64)destination_height;
        if (sy >= (u64)source_height) sy = (u64)source_height - 1u;
        for (i64 px = left; px < right; ++px) {
            i64 local_x = px - gfx_draw_origin_x;
            if (local_x < 0 || local_x >= GFX_W) continue;
            u64 sx = (u64)(px - x) * (u64)source_width / (u64)destination_width;
            if (sx >= (u64)source_width) sx = (u64)source_width - 1u;
            u32 color = pixels[(usize)sy * (usize)pitch_pixels + (usize)sx] & 0x00FFFFFFu;
            gfx_target_pixel(px, py, color);
        }
    }
    gfx_draw_end();
}

void gfx_blit_scaled_rgba32(long x, long y, long source_width, long source_height,
                            long destination_width, long destination_height,
                            const u32 *pixels, long pitch_pixels) {
    if (!gfx_scaled_blit_valid(x, y, source_width, source_height,
                               destination_width, destination_height, pixels, pitch_pixels)) return;
    i64 left = x < 0 ? 0 : x;
    i64 top = y < 0 ? 0 : y;
    i64 right = (i64)x + destination_width;
    i64 bottom = (i64)y + destination_height;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (right <= left || bottom <= top) return;
    gfx_draw_begin();
    for (i64 py = top; py < bottom; ++py) {
        i64 local_y = py - gfx_draw_origin_y;
        if (local_y < 0 || local_y >= GFX_H) continue;
        u64 sy = (u64)(py - y) * (u64)source_height / (u64)destination_height;
        if (sy >= (u64)source_height) sy = (u64)source_height - 1u;
        for (i64 px = left; px < right; ++px) {
            i64 local_x = px - gfx_draw_origin_x;
            if (local_x < 0 || local_x >= GFX_W) continue;
            u64 sx = (u64)(px - x) * (u64)source_width / (u64)destination_width;
            if (sx >= (u64)source_width) sx = (u64)source_width - 1u;
            u32 source = pixels[(usize)sy * (usize)pitch_pixels + (usize)sx];
            u32 alpha = source >> 24;
            if (!alpha) continue;
            if (alpha == 255u) gfx_target_pixel(px, py, source & 0x00FFFFFFu);
            else {
                u32 index = (u32)local_y * GFX_STRIDE + (u32)local_x;
                u32 blended = gfx_alpha_blend(source & 0x00FFFFFFu,
                                               gfx_color_to_rgb(gfx_draw_target[index]), alpha);
                gfx_target_pixel(px, py, blended);
            }
        }
    }
    gfx_draw_end();
}

long gfx_get_pixel(long x, long y) {
    if (!graphics_active || !reasonable_point(x, y) || x < 0 || x >= GFX_W ||
        y < 0 || y >= GFX_H || !gfx_draw_target) return 0;
    i64 local_x = x - gfx_draw_origin_x;
    i64 local_y = y - gfx_draw_origin_y;
    if (local_x < 0 || local_x >= GFX_W || local_y < 0 || local_y >= GFX_H) return 0;
    u32 index = (u32)local_y * GFX_STRIDE + (u32)local_x;
    return (long)gfx_color_to_rgb(gfx_draw_target[index]);
}

void gfx_line(long x1, long y1, long x2, long y2, long color) {
    if (!graphics_active || !reasonable_point(x1, y1) || !reasonable_point(x2, y2)) return;
    gfx_draw_begin();
    i64 dx = x2 >= x1 ? x2 - x1 : x1 - x2;
    i64 sx = x1 < x2 ? 1 : -1;
    i64 dy_abs = y2 >= y1 ? y2 - y1 : y1 - y2;
    i64 dy = -dy_abs;
    i64 sy = y1 < y2 ? 1 : -1;
    i64 error = dx + dy;
    u32 c = color_value((i64)color);

    for (;;) {
        if (x1 >= 0 && x1 < GFX_W && y1 >= 0 && y1 < GFX_H)
            gfx_target_pixel(x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        i64 twice = error * 2;
        if (twice >= dy) { error += dy; x1 += sx; }
        if (twice <= dx) { error += dx; y1 += sy; }
    }
    gfx_draw_end();
}

void gfx_rect(long x, long y, long width, long height, long color) {
    if (!graphics_active || !reasonable_point(x, y) || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) return;
    i64 right = x + width - 1;
    i64 bottom = y + height - 1;
    if (right < -4096 || right > 4096 || bottom < -4096 || bottom > 4096) return;
    gfx_draw_begin();
    gfx_line(x, y, right, y, color);
    gfx_line(x, bottom, right, bottom, color);
    gfx_line(x, y, x, bottom, color);
    gfx_line(right, y, right, bottom, color);
    gfx_draw_end();
}

void gfx_fill_rect(long x, long y, long width, long height, long color) {
    if (!graphics_active || !reasonable_point(x, y) || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) return;
    i64 left = x < 0 ? 0 : x;
    i64 top = y < 0 ? 0 : y;
    i64 right = x + width;
    i64 bottom = y + height;
    if (right > GFX_W) right = GFX_W;
    if (bottom > GFX_H) bottom = GFX_H;
    if (right <= left || bottom <= top) return;
    gfx_draw_begin();
    u32 c = color_value((i64)color);
    gfx_target_fill(left, top, right - left, bottom - top, c);
    gfx_draw_end();
}

void gfx_fill_rounded_rect(long x, long y, long width, long height,
                           long radius, long color) {
    if (!graphics_active || !reasonable_point(x, y) || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) return;
    if (radius < 0) radius = 0;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    gfx_draw_begin();
    gfx_fill_rounded_box(x, y, width, height, radius, color);
    gfx_draw_end();
}

void gfx_rounded_rect(long x, long y, long width, long height,
                      long radius, long color) {
    if (!graphics_active || !reasonable_point(x, y) || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) return;
    if (radius < 0) radius = 0;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    if (radius == 0) { gfx_rect(x, y, width, height, color); return; }
    gfx_draw_begin();
    i64 r2 = (i64)radius * radius;
    for (i64 dy = 0; dy <= radius; dy++) {
        i64 inset = radius;
        while (inset > 0 && inset * inset + dy * dy > r2) inset--;
        gfx_line(x + inset, y + dy, x + width - 1 - inset, y + dy, color);
        if (dy != 0 && dy != height - 1)
            gfx_line(x + inset, y + height - 1 - dy,
                     x + width - 1 - inset, y + height - 1 - dy, color);
    }
    if (height > radius * 2)
        gfx_line(x, y + radius, x, y + height - 1 - radius, color),
        gfx_line(x + width - 1, y + radius, x + width - 1,
                 y + height - 1 - radius, color);
    gfx_draw_end();
}

void gfx_ellipse(long center_x, long center_y, long radius_x, long radius_y, long color) {
    if (!graphics_active || !reasonable_point(center_x, center_y) ||
        radius_x < 0 || radius_y < 0 || radius_x > 1000 || radius_y > 1000) return;
    if (radius_x == 0) { gfx_line(center_x, center_y - radius_y, center_x, center_y + radius_y, color); return; }
    if (radius_y == 0) { gfx_line(center_x - radius_x, center_y, center_x + radius_x, center_y, color); return; }
    gfx_draw_begin();
    i64 rx2 = (i64)radius_x * radius_x;
    i64 ry2 = (i64)radius_y * radius_y;
    i64 dx = radius_x;
    for (i64 dy = 0; dy <= radius_y; dy++) {
        while (dx > 0 && dx * dx * ry2 + dy * dy * rx2 > rx2 * ry2) dx--;
        gfx_pixel(center_x - dx, center_y + dy, color);
        gfx_pixel(center_x + dx, center_y + dy, color);
        if (dy) {
            gfx_pixel(center_x - dx, center_y - dy, color);
            gfx_pixel(center_x + dx, center_y - dy, color);
        }
    }
    gfx_draw_end();
}

void gfx_fill_ellipse(long center_x, long center_y, long radius_x, long radius_y, long color) {
    if (!graphics_active || !reasonable_point(center_x, center_y) ||
        radius_x < 0 || radius_y < 0 || radius_x > 1000 || radius_y > 1000) return;
    if (radius_x == 0) { gfx_line(center_x, center_y - radius_y, center_x, center_y + radius_y, color); return; }
    if (radius_y == 0) { gfx_line(center_x - radius_x, center_y, center_x + radius_x, center_y, color); return; }
    gfx_draw_begin();
    i64 rx2 = (i64)radius_x * radius_x;
    i64 ry2 = (i64)radius_y * radius_y;
    i64 dx = radius_x;
    for (i64 dy = 0; dy <= radius_y; dy++) {
        while (dx > 0 && dx * dx * ry2 + dy * dy * rx2 > rx2 * ry2) dx--;
        gfx_line(center_x - dx, center_y + dy, center_x + dx, center_y + dy, color);
        if (dy) gfx_line(center_x - dx, center_y - dy, center_x + dx, center_y - dy, color);
    }
    gfx_draw_end();
}

void gfx_gradient_rect(long x, long y, long width, long height,
                       long first_color, long last_color, long vertical) {
    if (!graphics_active || !reasonable_point(x, y) || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) return;
    u32 first = gfx_color_to_rgb(color_value((i64)first_color));
    u32 last = gfx_color_to_rgb(color_value((i64)last_color));
    long steps = vertical ? height : width;
    if (steps <= 1) { gfx_fill_rect(x, y, width, height, (long)first); return; }
    gfx_draw_begin();
    for (long i = 0; i < steps; i++) {
        u32 r = (((first >> 16) & 255u) * (u32)(steps - 1 - i) +
                 ((last >> 16) & 255u) * (u32)i) / (u32)(steps - 1);
        u32 g = (((first >> 8) & 255u) * (u32)(steps - 1 - i) +
                 ((last >> 8) & 255u) * (u32)i) / (u32)(steps - 1);
        u32 b = ((first & 255u) * (u32)(steps - 1 - i) +
                 (last & 255u) * (u32)i) / (u32)(steps - 1);
        long color = gfx_rgb((long)r, (long)g, (long)b);
        if (vertical) gfx_fill_rect(x, y + i, width, 1, color);
        else gfx_fill_rect(x + i, y, 1, height, color);
    }
    gfx_draw_end();
}

void gfx_circle(long center_x, long center_y, long radius, long color) {
    if (!graphics_active || !reasonable_point(center_x, center_y) || radius < 0 || radius > 1000) return;
    gfx_draw_begin();
    i64 x = radius;
    i64 y = 0;
    i64 error = 1 - x;
    while (x >= y) {
        gfx_pixel(center_x + x, center_y + y, color);
        gfx_pixel(center_x + y, center_y + x, color);
        gfx_pixel(center_x - y, center_y + x, color);
        gfx_pixel(center_x - x, center_y + y, color);
        gfx_pixel(center_x - x, center_y - y, color);
        gfx_pixel(center_x - y, center_y - x, color);
        gfx_pixel(center_x + y, center_y - x, color);
        gfx_pixel(center_x + x, center_y - y, color);
        y++;
        if (error < 0) error += 2 * y + 1;
        else { x--; error += 2 * (y - x) + 1; }
    }
    gfx_draw_end();
}

void gfx_fill_circle(long center_x, long center_y, long radius, long color) {
    if (!graphics_active || !reasonable_point(center_x, center_y) || radius < 0 || radius > 200) return;
    gfx_draw_begin();
    i64 r2 = radius * radius;
    i64 dx = radius;
    /* The horizontal half-width only decreases as |dy| increases. Reusing
       it makes this O(radius) instead of restarting an O(radius) search for
       every scanline (O(radius squared)). */
    for (i64 dy = 0; dy <= radius; dy++) {
        while (dx > 0 && dx * dx + dy * dy > r2) dx--;
        gfx_line(center_x - dx, center_y + dy, center_x + dx, center_y + dy, color);
        if (dy != 0)
            gfx_line(center_x - dx, center_y - dy, center_x + dx, center_y - dy, color);
    }
    gfx_draw_end();
}

/* Compact 5x7 font: A-Z followed by 0-9. Lowercase is drawn as uppercase. */
static const u8 font5x7[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

static int glyph_index(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    return -1;
}

static u8 punctuation_row(char ch, int row) {
    static const u8 dash[7] = {0,0,0,31,0,0,0};
    static const u8 underscore[7] = {0,0,0,0,0,0,31};
    static const u8 period[7] = {0,0,0,0,0,12,12};
    static const u8 colon[7] = {0,12,12,0,12,12,0};
    static const u8 exclaim[7] = {4,4,4,4,4,0,4};
    static const u8 question[7] = {14,17,1,2,4,0,4};
    static const u8 slash[7] = {1,2,2,4,8,8,16};
    static const u8 backslash[7] = {16,8,8,4,2,2,1};
    static const u8 plus[7] = {0,4,4,31,4,4,0};
    static const u8 equal[7] = {0,0,31,0,31,0,0};
    static const u8 left_paren[7] = {2,4,8,8,8,4,2};
    static const u8 right_paren[7] = {8,4,2,2,2,4,8};
    static const u8 comma[7] = {0,0,0,0,4,4,8};
    static const u8 quote[7] = {10,10,10,0,0,0,0};
    const u8 *rows = 0;
    switch (ch) {
        case '-': rows = dash; break;
        case '_': rows = underscore; break;
        case '.': rows = period; break;
        case ':': rows = colon; break;
        case '!': rows = exclaim; break;
        case '?': rows = question; break;
        case '/': rows = slash; break;
        case '\\': rows = backslash; break;
        case '+': rows = plus; break;
        case '=': rows = equal; break;
        case '(': rows = left_paren; break;
        case ')': rows = right_paren; break;
        case ',': rows = comma; break;
        case '"': rows = quote; break;
        default: return 0;
    }
    return rows[row];
}

static void draw_text_scaled(long x, long y, const char *text, long color, long scale) {
    if (!graphics_active || !text || !reasonable_point(x, y)) return;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    u32 c = color_value((i64)color);
    for (int n = 0; text[n] && n < 256; n++) {
        char ch = text[n];
        if (ch == '\n') { x = gfx_draw_origin_x; y += 9 * scale; continue; }
        int index = glyph_index(ch);
        for (int row = 0; row < 7; row++) {
            u8 bits = index >= 0 ? font5x7[index][row] : punctuation_row(ch, row);
            for (int col = 0; col < 5; col++) {
                if (bits & (1u << (4 - col))) {
                    if (scale == 1) gfx_pixel(x + col, y + row, c);
                    else gfx_fill_rect(x + col * scale, y + row * scale,
                                       scale, scale, c);
                }
            }
        }
        x += 6 * scale;
        if (x > GFX_W) break;
    }
}

void gfx_text(long x, long y, const char *text, long color) {
    if (!graphics_active || !text) return;
    gfx_draw_begin();
    draw_text_scaled(x, y, text, color, 1);
    gfx_draw_end();
}

void gfx_text_scale(long x, long y, const char *text, long color, long scale) {
    if (!graphics_active || !text) return;
    gfx_draw_begin();
    draw_text_scaled(x, y, text, color, scale);
    gfx_draw_end();
}

/* Blend anti-aliased UI glyph coverage against the pixel already on the
   selected client surface. The 2-bit glyphs keep font data compact while
   producing smooth edges on the true-color framebuffer. */
static u32 gfx_blend_rgb(u32 foreground, u32 background, u32 coverage) {
    foreground = gfx_color_to_rgb(foreground);
    background = gfx_color_to_rgb(background);
    u32 inverse = 3u - coverage;
    u32 red = ((((foreground >> 16) & 255u) * coverage) +
               (((background >> 16) & 255u) * inverse) + 1u) / 3u;
    u32 green = ((((foreground >> 8) & 255u) * coverage) +
                 (((background >> 8) & 255u) * inverse) + 1u) / 3u;
    u32 blue = (((foreground & 255u) * coverage) +
                ((background & 255u) * inverse) + 1u) / 3u;
    return (red << 16) | (green << 8) | blue;
}

static void draw_ui_text_scaled(long x, long y, const char *text,
                                long color, long scale, int bold) {
    if (!graphics_active || !text || !reasonable_point(x, y)) return;
    if (scale < 1) scale = 1;
    if (scale > 3) scale = 3;
    u32 foreground = color_value((i64)color);
    const unsigned int (*font)[GFX_UI_FONT_HEIGHT] =
        bold ? gfx_ui_font_bold : gfx_ui_font_regular;
    i64 origin_x = x;
    for (int index = 0; text[index] && index < 512; index++) {
        unsigned char ch = (unsigned char)text[index];
        if (ch == '\n') {
            x = (long)origin_x;
            y += GFX_UI_FONT_LINE_ADVANCE * scale;
            if (y >= GFX_H) break;
            continue;
        }
        if (ch < GFX_UI_FONT_FIRST || ch >= GFX_UI_FONT_FIRST + GFX_UI_FONT_COUNT)
            ch = '?';
        unsigned int glyph = (unsigned int)(ch - GFX_UI_FONT_FIRST);
        for (int row = 0; row < GFX_UI_FONT_HEIGHT; row++) {
            unsigned int bits = font[glyph][row];
            for (int column = 0; column < GFX_UI_FONT_WIDTH; column++) {
                unsigned int coverage = (bits >> (column * 2)) & 3u;
                if (!coverage) continue;
                for (int sy = 0; sy < scale; sy++) {
                    long py = y + row * scale + sy;
                    if (py < 0 || py >= GFX_H) continue;
                    for (int sx = 0; sx < scale; sx++) {
                        long px = x + column * scale + sx;
                        if (px < 0 || px >= GFX_W) continue;
                        i64 local_x = px - gfx_draw_origin_x;
                        i64 local_y = py - gfx_draw_origin_y;
                        if (!gfx_draw_target || local_x < 0 || local_x >= GFX_W ||
                            local_y < 0 || local_y >= GFX_H) continue;
                        u32 offset = (u32)local_y * GFX_STRIDE + (u32)local_x;
                        u32 blended = coverage == 3u ? foreground :
                                      gfx_blend_rgb(foreground, gfx_draw_target[offset], coverage);
                        gfx_target_pixel(px, py, blended);
                    }
                }
            }
        }
        x += GFX_UI_FONT_ADVANCE * scale;
        if (x >= GFX_W) break;
    }
}

void gfx_text_ui(long x, long y, const char *text, long color) {
    gfx_draw_begin();
    draw_ui_text_scaled(x, y, text, color, 1, 0);
    gfx_draw_end();
}

void gfx_text_ui_bold(long x, long y, const char *text, long color) {
    gfx_draw_begin();
    draw_ui_text_scaled(x, y, text, color, 1, 1);
    gfx_draw_end();
}

void gfx_text_ui_scale(long x, long y, const char *text, long color,
                       long scale, long bold) {
    gfx_draw_begin();
    draw_ui_text_scaled(x, y, text, color, scale, bold != 0);
    gfx_draw_end();
}

long gfx_text_width(const char *text) {
    long length = 0;
    if (!text) return 0;
    while (text[length] && length < 512) length++;
    return length * GFX_UI_FONT_ADVANCE;
}

void gfx_text_centered(long x, long y, long width, const char *text, long color) {
    if (!text || width <= 0) return;
    long text_width = gfx_text_width(text);
    long draw_x = x + (width - text_width) / 2;
    if (draw_x < x) draw_x = x;
    gfx_text_ui(draw_x, y, text, color);
}

long gfx_text_wrapped(long x, long y, long maximum_width, const char *text,
                      long color, long line_height) {
    if (!graphics_active || !text || maximum_width < GFX_UI_FONT_ADVANCE ||
        maximum_width > 4096) return 0;
    if (line_height < GFX_UI_FONT_HEIGHT) line_height = GFX_UI_FONT_HEIGHT;
    if (line_height > 256) return 0;
    const long start_y = y;
    int lines = 0;
    gfx_draw_begin();
    while (*text && lines < 256) {
        while (*text == ' ' || *text == '\t') text++;
        if (!*text) break;
        char line[256];
        int count = 0;
        int last_space = -1;
        const char *after_last_space = 0;
        while (*text && *text != '\n' && count < 255 &&
               (long)(count + 1) * GFX_UI_FONT_ADVANCE <= maximum_width) {
            line[count] = *text++;
            if (line[count] == ' ' || line[count] == '\t') {
                last_space = count;
                after_last_space = text;
            }
            count++;
        }
        if (*text && *text != '\n' && count > 0 && last_space >= 0) {
            text = after_last_space;
            count = last_space;
            while (*text == ' ' || *text == '\t') text++;
        }
        if (count == 0 && *text && *text != '\n') line[count++] = *text++;
        line[count] = 0;
        if (count) draw_ui_text_scaled(x, y, line, color, 1, 0);
        y += line_height;
        lines++;
        if (*text == '\n') text++;
    }
    gfx_draw_end();
    return y - start_y;
}

void gfx_triangle(long x1, long y1, long x2, long y2,
                  long x3, long y3, long color) {
    if (!graphics_active || !reasonable_point(x1, y1) ||
        !reasonable_point(x2, y2) || !reasonable_point(x3, y3)) return;
    gfx_draw_begin();
    gfx_line(x1, y1, x2, y2, color);
    gfx_line(x2, y2, x3, y3, color);
    gfx_line(x3, y3, x1, y1, color);
    gfx_draw_end();
}

static i64 triangle_edge(i64 ax, i64 ay, i64 bx, i64 by, i64 px, i64 py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void gfx_fill_triangle(long x1, long y1, long x2, long y2,
                       long x3, long y3, long color) {
    if (!graphics_active || !reasonable_point(x1, y1) ||
        !reasonable_point(x2, y2) || !reasonable_point(x3, y3)) return;
    gfx_draw_begin();

    i64 area = triangle_edge(x1, y1, x2, y2, x3, y3);
    if (area == 0) {
        gfx_triangle(x1, y1, x2, y2, x3, y3, color);
        gfx_draw_end();
        return;
    }

    i64 min_x = x1 < x2 ? x1 : x2;
    if (x3 < min_x) min_x = x3;
    i64 max_x = x1 > x2 ? x1 : x2;
    if (x3 > max_x) max_x = x3;
    i64 min_y = y1 < y2 ? y1 : y2;
    if (y3 < min_y) min_y = y3;
    i64 max_y = y1 > y2 ? y1 : y2;
    if (y3 > max_y) max_y = y3;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= GFX_W) max_x = GFX_W - 1;
    if (max_y >= GFX_H) max_y = GFX_H - 1;
    if (min_x > max_x || min_y > max_y) {
        gfx_draw_end();
        return;
    }

    u32 c = color_value((i64)color);
    for (i64 py = min_y; py <= max_y; py++) {
        for (i64 px = min_x; px <= max_x; px++) {
            i64 e1 = triangle_edge(x1, y1, x2, y2, px, py);
            i64 e2 = triangle_edge(x2, y2, x3, y3, px, py);
            i64 e3 = triangle_edge(x3, y3, x1, y1, px, py);
            int has_negative = e1 < 0 || e2 < 0 || e3 < 0;
            int has_positive = e1 > 0 || e2 > 0 || e3 > 0;
            if (!(has_negative && has_positive))
                gfx_target_pixel(px, py, c);
        }
    }
    gfx_draw_end();
}

void gfx_button(long x, long y, long width, long height,
                const char *label, long pressed) {
    if (!graphics_active || !label || !reasonable_point(x, y) ||
        width < 20 || height < 16 || width > 4096 || height > 4096 ||
        x + width - 1 > 4096 || y + height - 1 > 4096) return;
    gfx_draw_begin();
    int hovered = mouse_is_available() &&
        point_in_rect(gfx_mouse_state.x, gfx_mouse_state.y, x, y, width, height);
    int shown_pressed = pressed || (hovered && (gfx_mouse_state.buttons & MOUSE_BUTTON_LEFT));
    gfx_fill_rect(x, y, width, height, hovered ? BRIGHT_CYAN : LIGHT_GRAY);
    if (shown_pressed) {
        gfx_line(x, y, x + width - 1, y, DARK_GRAY);
        gfx_line(x, y, x, y + height - 1, DARK_GRAY);
        gfx_line(x, y + height - 1, x + width - 1, y + height - 1, WHITE);
        gfx_line(x + width - 1, y, x + width - 1, y + height - 1, WHITE);
    } else {
        gfx_line(x, y, x + width - 1, y, WHITE);
        gfx_line(x, y, x, y + height - 1, WHITE);
        gfx_line(x, y + height - 1, x + width - 1, y + height - 1, DARK_GRAY);
        gfx_line(x + width - 1, y, x + width - 1, y + height - 1, DARK_GRAY);
    }
    int length = 0;
    while (label[length] && length < 40) length++;
    long text_x = x + (width - length * 6) / 2 + (pressed ? 1 : 0);
    long text_y = y + (height - 7) / 2 + (pressed ? 1 : 0);
    gfx_text(text_x, text_y, label, BLACK);
    gfx_draw_end();
}

void gfx_progress_bar(long x, long y, long width, long height,
                      long value, long maximum, long fill_color, long background_color) {
    if (!graphics_active || !reasonable_point(x, y) || width < 8 || height < 8 ||
        width > 4096 || height > 4096 || maximum <= 0 ||
        maximum > 0x7FFFFFFFFFFFFFFLL / 4096) return;
    gfx_draw_begin();
    if (value < 0) value = 0;
    if (value > maximum) value = maximum;
    gfx_fill_rect(x, y, width, height, DARK_GRAY);
    gfx_fill_rect(x + 1, y + 1, width - 2, height - 2, background_color);
    long inner_width = width - 4;
    long filled = (value * inner_width) / maximum;
    if (filled > 0) gfx_fill_rect(x + 2, y + 2, filled, height - 4, fill_color);
    gfx_line(x + 1, y + height - 2, x + width - 2, y + height - 2, WHITE);
    gfx_draw_end();
}

int gfx_window_create(long x, long y, long width, long height, const char *title) {
    cursor_restore();
    long max_bottom = desktop_active ? GFX_H - GFX_TASKBAR_HEIGHT : GFX_H;
    if (!graphics_active || !title || !reasonable_point(x, y) || x < 0 || y < 0 ||
        width < GFX_WINDOW_MIN_WIDTH || height < GFX_WINDOW_MIN_HEIGHT ||
        width > GFX_W || height > max_bottom || x + width > GFX_W || y + height > max_bottom ||
        gfx_window_count_value >= GFX_MAX_WINDOWS || gfx_next_window_id <= 0 ||
        gfx_next_window_id >= 0x7FFFFFFF)
        return 0;

    int slot = -1;
    for (int i = 0; i < GFX_MAX_WINDOWS; i++) {
        if (!gfx_windows[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;

    if (!desktop_snapshot_valid) save_desktop();
    gfx_window_state_t *window = &gfx_windows[slot];
    window->used = 1;
    window->minimized = 0;
    window->id = gfx_next_window_id++;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    int i = 0;
    /* Leave room for the title-bar controls and a small visual gap. */
    int title_limit = (int)((width - 66) / GFX_UI_FONT_ADVANCE);
    if (title_limit > (int)sizeof(window->title) - 1)
        title_limit = (int)sizeof(window->title) - 1;
    while (title[i] && i < title_limit) {
        window->title[i] = title[i];
        i++;
    }
    window->title[i] = 0;
    gfx_fill_buffer(window->surface, color_value(LIGHT_GRAY));

    gfx_window_order[gfx_window_count_value++] = slot;
    gfx_active_slot = slot;
    gfx_update_draw_target();
    gfx_window_dragging = 0;
    gfx_drag_mode = GFX_DRAG_NONE;
    gfx_drag_slot = -1;
    gfx_queue_focus(slot);
    render_desktop_scene();
    return window->id;
}

void gfx_window_redraw(void) {
    cursor_restore();
    render_desktop_scene();
}

void gfx_window_close(void) {
    if (!graphics_active || gfx_active_slot < 0) return;
    close_window_slot(gfx_active_slot);
}

void gfx_window_close_id(long id) {
    if (!graphics_active) return;
    int slot = window_slot_from_id(id);
    if (slot >= 0) close_window_slot(slot);
}

void gfx_window_minimize(void) {
    /* Without a desktop taskbar there is no user-facing restore path. */
    if (!graphics_active || !desktop_active || gfx_active_slot < 0 ||
        !gfx_windows[gfx_active_slot].used) return;
    int minimized_id = gfx_windows[gfx_active_slot].id;
    gfx_windows[gfx_active_slot].minimized = 1;
    gfx_active_slot = top_visible_window();
    gfx_update_draw_target();
    gfx_window_dragging = 0;
    gfx_drag_mode = GFX_DRAG_NONE;
    gfx_drag_slot = -1;
    gfx_queue_event(GFX_EVENT_STATE, 0, gfx_mouse_state.x, gfx_mouse_state.y,
                    gfx_mouse_state.buttons, minimized_id, 1);
    if (gfx_active_slot >= 0) gfx_queue_focus(gfx_active_slot);
    render_desktop_scene();
}

long gfx_window_is_minimized(long id) {
    int slot = window_slot_from_id(id);
    return slot >= 0 && gfx_windows[slot].minimized ? 1 : 0;
}

int gfx_window_select(long id) {
    if (!graphics_active) return 0;
    int slot = window_slot_from_id(id);
    if (slot < 0) return 0;
    raise_window(slot);
    return 1;
}

long gfx_window_active(void) {
    return gfx_active_slot >= 0 && gfx_active_slot < GFX_MAX_WINDOWS &&
           gfx_windows[gfx_active_slot].used ? gfx_windows[gfx_active_slot].id : 0;
}

long gfx_window_count(void) { return gfx_window_count_value; }

long gfx_window_is_open_id(long id) {
    return window_slot_from_id(id) >= 0 ? 1 : 0;
}

int gfx_desktop_start(long background_color) {
    /* Desktop initialization is one-shot per graphics session. Reinitializing
       it would discard registered icons and the saved wallpaper surface. */
    if (!graphics_active || desktop_active || gfx_window_count_value != 0) return 0;
    gfx_begin_frame();
    desktop_active = 1;
    desktop_color = color_value((i64)background_color);
    gfx_set_draw_target(gfx_back_buffer, 0, 0);
    gfx_fill_buffer(gfx_back_buffer, desktop_color);
    /* A restrained two-tone wallpaper gives the desktop depth without
       requiring images or a second video mode. */
    gfx_fill_rect(0, 0, GFX_W, GFX_H - GFX_TASKBAR_HEIGHT, desktop_color);
    gfx_fill_rect(0, 112, GFX_W, 70, desktop_color == BLUE ? BRIGHT_BLUE : BLUE);
    gfx_line(0, 111, GFX_W - 1, 111, CYAN);
    save_desktop();
    gfx_set_draw_target(desktop_snapshot, 0, 0);
    gfx_frame_dirty = 1;
    gfx_end_frame();
    return 1;
}

void gfx_desktop_redraw(void) { render_desktop_scene(); }

int gfx_desktop_add_icon(long x, long y, const char *label, long color) {
    if (!graphics_active || !desktop_active || !label || x < 0 || y < 0 ||
        x > GFX_W - 40 || y > GFX_H - GFX_TASKBAR_HEIGHT - 36 ||
        desktop_icon_count >= GFX_MAX_DESKTOP_ICONS || desktop_next_icon_id <= 0)
        return 0;
    int slot = -1;
    for (int i = 0; i < GFX_MAX_DESKTOP_ICONS; i++)
        if (!desktop_icons[i].used) { slot = i; break; }
    if (slot < 0) return 0;
    gfx_desktop_icon_state_t *icon = &desktop_icons[slot];
    icon->used = 1;
    icon->id = desktop_next_icon_id++;
    icon->x = x;
    icon->y = y;
    icon->color = color_value((i64)color);
    int n = 0;
    /* Six glyphs fit inside the 40-pixel icon label area at the 5x7 font's
       six-pixel character advance. Keep the UI bounded and predictable. */
    while (label[n] && n < 6) {
        icon->label[n] = label[n];
        n++;
    }
    icon->label[n] = 0;
    desktop_icon_count++;
    render_desktop_scene();
    return icon->id;
}

long gfx_desktop_icon_clicked(void) {
    int id = desktop_clicked_id;
    desktop_clicked_id = 0;
    return id;
}

long gfx_desktop_is_active(void) { return desktop_active ? 1 : 0; }

static int gfx_pump_input(int *legacy_result) {
    int produced = 0;
    int mouse_produced = 0;
    if (legacy_result) *legacy_result = GFX_EVENT_NONE;
    gfx_mouse_pressed_state = 0;
    gfx_mouse_released_state = 0;
    gfx_mouse_event_pending = 0;

    /* A PS/2 controller can deliver several packets before the app polls.
       Process every packet and preserve its event, but only compose/present
       the final pointer/window state once per pump. */
    gfx_input_pumping = 1;
    gfx_scene_redraw_pending = 0;
    mouse_state_t state;
    for (u32 count = 0; count < 32u && mouse_poll(&state); count++) {
        produced = 1;
        mouse_produced = 1;
        gfx_mouse_state = state;
        gfx_mouse_pressed_state |= state.pressed;
        gfx_mouse_released_state |= state.released;
        gfx_mouse_event_pending = 1;
        unsigned int event_tail_before = gfx_event_tail;
        int action = handle_window_mouse(&state);
        int type = gfx_pending_event_type ? gfx_pending_event_type : GFX_EVENT_MOUSE;
        /* Window actions may already have queued a specific focus, resize,
           close, or state event. Add a generic pointer event only when the
           handler did not emit a more meaningful event for this packet. */
        if (gfx_event_tail == event_tail_before)
            gfx_queue_event(type, 0, state.x, state.y, state.buttons,
                            (int)gfx_window_active(), desktop_clicked_id);
        if (legacy_result && *legacy_result == GFX_EVENT_NONE) {
            if (action) *legacy_result = action;
            else *legacy_result = GFX_EVENT_MOUSE;
        }
    }

    /* Do not drain keyboard after a mouse event in the same pump. This keeps
       the legacy blocking API lossless: it returns one input class at a time. */
    if (!produced) {
        int key;
        if (serial_try_getkey(&key)) {
            produced = 1;
            int target_window = (int)gfx_window_active();
            gfx_queue_event(GFX_EVENT_KEY, key, gfx_mouse_state.x, gfx_mouse_state.y,
                            gfx_mouse_state.buttons, target_window, key);
            if (key == GFX_KEY_ESCAPE && gfx_active_slot >= 0) {
                close_window_slot(gfx_active_slot);
                if (legacy_result)
                    *legacy_result = gfx_window_count_value > 0 || desktop_active
                        ? GFX_EVENT_WINDOW : GFX_KEY_ESCAPE;
            } else if (legacy_result) {
                *legacy_result = key;
            }
        }
    }
    gfx_input_pumping = 0;
    if (gfx_scene_redraw_pending) {
        gfx_scene_redraw_pending = 0;
        render_desktop_scene();
    } else if (mouse_produced) {
        cursor_draw();
    }
    return produced;
}

long gfx_window_wait_key(void) {
    int legacy_result = GFX_EVENT_NONE;
    if (!gfx_window_count_value && !desktop_active) return GFX_KEY_ESCAPE;
    gfx_click_pending = 0;
    for (;;) {
        if (gfx_pump_input(&legacy_result)) {
            if (legacy_result == GFX_EVENT_NONE) legacy_result = GFX_EVENT_MOUSE;
            return legacy_result;
        }
        if (!gfx_window_count_value && !desktop_active) return GFX_KEY_ESCAPE;
#ifdef DENOS_GRAPHICS_TESTING
        __asm__ volatile("pause");
#else
        __asm__ volatile("hlt");
#endif
    }
}

long gfx_desktop_wait_event(void) { return gfx_window_wait_key(); }

long gfx_event_poll(void) {
    if (!graphics_active) return GFX_EVENT_NONE;
    if (!gfx_event_count) (void)gfx_pump_input(0);
    return gfx_pop_event();
}

long gfx_event_wait(void) {
    if (!graphics_active) return GFX_EVENT_NONE;
    for (;;) {
        if (gfx_event_count) return gfx_pop_event();
        (void)gfx_pump_input(0);
        if (gfx_event_count) return gfx_pop_event();
#ifdef DENOS_GRAPHICS_TESTING
        __asm__ volatile("pause");
#else
        __asm__ volatile("hlt");
#endif
    }
}

long gfx_event_type(void) { return gfx_current_event.type; }
long gfx_event_key(void) { return gfx_current_event.key; }
long gfx_event_x(void) { return gfx_current_event.x; }
long gfx_event_y(void) { return gfx_current_event.y; }
long gfx_event_window(void) { return gfx_current_event.window_id; }
long gfx_event_buttons(void) { return gfx_current_event.buttons; }
long gfx_event_code(void) { return gfx_current_event.code; }

static int gfx_focus_step(int direction) {
    int visible[GFX_MAX_WINDOWS];
    int count = 0;
    int current = -1;
    for (int order = 0; order < gfx_window_count_value; order++) {
        int slot = gfx_window_order[order];
        if (slot < 0 || slot >= GFX_MAX_WINDOWS || !gfx_windows[slot].used ||
            gfx_windows[slot].minimized) continue;
        visible[count] = slot;
        if (slot == gfx_active_slot) current = count;
        count++;
    }
    if (count == 0) return 0;
    int target;
    if (current < 0) target = direction > 0 ? 0 : count - 1;
    else target = (current + direction + count) % count;
    raise_window(visible[target]);
    return 1;
}

int gfx_window_focus_next(void) { return gfx_focus_step(1); }
int gfx_window_focus_previous(void) { return gfx_focus_step(-1); }

long gfx_window_content_x(void) {
    return gfx_active_slot >= 0 && gfx_windows[gfx_active_slot].used
        ? gfx_windows[gfx_active_slot].x + GFX_CONTENT_X : 0;
}
long gfx_window_content_y(void) {
    return gfx_active_slot >= 0 && gfx_windows[gfx_active_slot].used
        ? gfx_windows[gfx_active_slot].y + GFX_CONTENT_Y : 0;
}
long gfx_window_content_width(void) {
    return gfx_active_slot >= 0 && gfx_windows[gfx_active_slot].used
        ? gfx_windows[gfx_active_slot].width - 12 : 0;
}
long gfx_window_content_height(void) {
    return gfx_active_slot >= 0 && gfx_windows[gfx_active_slot].used
        ? gfx_windows[gfx_active_slot].height - 30 : 0;
}
long gfx_window_is_open(void) { return gfx_active_slot >= 0 ? 1 : 0; }
long gfx_window_is_dragging(void) { return gfx_window_dragging ? 1 : 0; }
long gfx_window_is_resizing(void) { return gfx_drag_mode == GFX_DRAG_RESIZE ? 1 : 0; }
long gfx_window_width(void) {
    return gfx_active_slot >= 0 && gfx_windows[gfx_active_slot].used
        ? gfx_windows[gfx_active_slot].width : 0;
}
long gfx_window_height(void) {
    return gfx_active_slot >= 0 && gfx_windows[gfx_active_slot].used
        ? gfx_windows[gfx_active_slot].height : 0;
}
long gfx_mouse_available(void) { return mouse_is_available() ? 1 : 0; }
long gfx_mouse_x(void) { return gfx_mouse_state.x; }
long gfx_mouse_y(void) { return gfx_mouse_state.y; }
long gfx_mouse_buttons(void) { return gfx_mouse_state.buttons; }
long gfx_mouse_pressed(void) { return gfx_mouse_pressed_state; }
long gfx_mouse_released(void) { return gfx_mouse_released_state; }
long gfx_button_clicked(long x, long y, long width, long height) {
    cursor_restore();
    if (width <= 0 || height <= 0 || !gfx_click_pending) return 0;
    if (!point_in_rect(gfx_click_x, gfx_click_y, x, y, width, height)) return 0;
    gfx_click_pending = 0;
    return 1;
}

long gfx_width(void) { return GFX_W; }
long gfx_height(void) { return GFX_H; }
long gfx_display_width(void) {
    return gfx_framebuffer_configured ? (long)gfx_framebuffer_width : 320;
}
long gfx_display_height(void) {
    return gfx_framebuffer_configured ? (long)gfx_framebuffer_height : 200;
}
long gfx_using_framebuffer(void) { return gfx_framebuffer_active ? 1 : 0; }
long gfx_wait_key(void) { return (long)serial_getkey(); }

void gfx_delay(long ticks) {
    if (ticks <= 0 || ticks > 6000) return;
    u64 start = timer_ticks();
    while (timer_ticks() - start < (u64)ticks) {
        __asm__ volatile("hlt");
    }
}

void gfx_polygon(const long *xy, long point_count, long color) {
    if (!graphics_active || !xy || point_count < 3 || point_count > 64) return;
    for (long i = 0; i < point_count; ++i) {
        long j = (i + 1) % point_count;
        if (!reasonable_point(xy[i * 2], xy[i * 2 + 1]) ||
            !reasonable_point(xy[j * 2], xy[j * 2 + 1])) return;
    }
    gfx_draw_begin();
    for (long i = 0; i < point_count; ++i) {
        long j = (i + 1) % point_count;
        gfx_line(xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1], color);
    }
    gfx_draw_end();
}

void gfx_fill_polygon(const long *xy, long point_count, long color) {
    if (!graphics_active || !xy || point_count < 3 || point_count > 64) return;
    long minimum_y = 4096;
    long maximum_y = -4096;
    for (long i = 0; i < point_count; ++i) {
        long vx = xy[i * 2];
        long vy = xy[i * 2 + 1];
        if (!reasonable_point(vx, vy)) return;
        if (vy < minimum_y) minimum_y = vy;
        if (vy > maximum_y) maximum_y = vy;
    }
    if (minimum_y < 0) minimum_y = 0;
    if (maximum_y >= GFX_H) maximum_y = GFX_H - 1;
    if (maximum_y < minimum_y) return;

    gfx_draw_begin();
    u32 pixel = color_value((i64)color);
    long intersections[64];
    for (long y = minimum_y; y <= maximum_y; ++y) {
        int count = 0;
        for (long i = 0; i < point_count; ++i) {
            long j = (i + 1) % point_count;
            i64 x1 = xy[i * 2], y1 = xy[i * 2 + 1];
            i64 x2 = xy[j * 2], y2 = xy[j * 2 + 1];
            if ((y1 <= y && y < y2) || (y2 <= y && y < y1)) {
                i64 numerator = ((i64)y - y1) * (x2 - x1);
                i64 denominator = y2 - y1;
                i64 intersection = x1 + numerator / denominator;
                if (count < 64) intersections[count++] = (long)intersection;
            }
        }
        for (int i = 1; i < count; ++i) {
            long value = intersections[i];
            int j = i;
            while (j > 0 && intersections[j - 1] > value) {
                intersections[j] = intersections[j - 1];
                --j;
            }
            intersections[j] = value;
        }
        for (int i = 0; i + 1 < count; i += 2) {
            i64 left = intersections[i];
            i64 right = intersections[i + 1];
            if (left < 0) left = 0;
            if (right >= GFX_W) right = GFX_W - 1;
            for (i64 x = left; x <= right; ++x)
                gfx_target_pixel(x, y, pixel);
        }
    }
    gfx_draw_end();
}

