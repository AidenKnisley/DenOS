#ifndef DENOS_GFX_H
#define DENOS_GFX_H

#include "types.h"


/*
 * DenOS graphics library.
 *
 * The preferred logical drawing surface is 640x360 and is scaled 2x to the
 * preferred 1280x720 RGB display. VGA compatibility mode uses 320x200.
 * Query gfx_width()/gfx_height() at runtime before laying out UI.
 * Drawing calls require gfx_start(). This is the kernel's desktop renderer
 * and window-composition API; it is not yet a userspace application ABI.
 */
#if defined(DENOS_GRAPHICS_TESTING) && !defined(DENOS_GRAPHICS_HIRES_TEST)
#define GFX_WIDTH  320
#define GFX_HEIGHT 200
#else
/* Full-detail 16:9 canvas for anti-aliased type and modern UI spacing. */
#define GFX_WIDTH  640
#define GFX_HEIGHT 360
#endif

#define BLACK        0
#define BLUE         1
#define GREEN        2
#define CYAN         3
#define RED          4
#define PURPLE       5
#define BROWN        6
#define LIGHT_GRAY   7
#define DARK_GRAY    8
#define BRIGHT_BLUE  9
#define BRIGHT_GREEN 10
#define BRIGHT_CYAN  11
#define BRIGHT_RED   12
#define PINK         13
#define YELLOW       14
#define WHITE        15

#define GFX_KEY_ENTER  13
#define GFX_KEY_ESCAPE 27
#define GFX_KEY_UP     256
#define GFX_KEY_DOWN   257
#define GFX_KEY_LEFT   258
#define GFX_KEY_RIGHT  259
#define GFX_KEY_HOME   260
#define GFX_KEY_END    261
#define GFX_KEY_DELETE 262
#define GFX_KEY_PAGEUP 263
#define GFX_KEY_PAGEDOWN 264
#define GFX_KEY_BACKSPACE 8
#define GFX_KEY_TAB 9
#define GFX_KEY_SPACE 32

#define GFX_WINDOW_MIN_WIDTH 80
#define GFX_WINDOW_MIN_HEIGHT 50
#define GFX_EVENT_NONE 0
#define GFX_EVENT_KEY 1
#define GFX_EVENT_MOUSE 512
#define GFX_EVENT_WINDOW 513 /* Legacy aggregate return; queued events use specific types. */
#define GFX_EVENT_RESIZE 514
#define GFX_EVENT_FOCUS 515
#define GFX_EVENT_CLOSE 516
#define GFX_EVENT_STATE 517
#define GFX_EVENT_QUEUE_CAPACITY 32
#define GFX_MAX_WINDOWS 4
#define GFX_MAX_DESKTOP_ICONS 8
#define GFX_MOUSE_LEFT 1
#define GFX_MOUSE_RIGHT 2
#define GFX_MOUSE_MIDDLE 4

int  gfx_start(void);
void gfx_stop(void);
/* Kernel boot code configures a validated Multiboot2 32-bit RGB framebuffer.
   Returns 1 when accepted; otherwise gfx_start() retains the VGA fallback. */
int gfx_configure_framebuffer(unsigned long long address,
                              unsigned int pitch, unsigned int width,
                              unsigned int height, unsigned char bits_per_pixel,
                              unsigned char framebuffer_type,
                              unsigned char red_position, unsigned char red_mask_size,
                              unsigned char green_position, unsigned char green_mask_size,
                              unsigned char blue_position, unsigned char blue_mask_size);
long gfx_display_width(void);
long gfx_display_height(void);
long gfx_using_framebuffer(void);
long gfx_rgb(long red, long green, long blue);
void gfx_clear(long color);
void gfx_pixel(long x, long y, long color);
/* Copy a clipped row-major 0x00RRGGBB image into the current drawing surface. */
void gfx_blit_rgb32(long x, long y, long width, long height, const u32 *pixels, long pitch_pixels);
/* Alpha-blend 0xAARRGGBB pixels over the selected drawing surface. */
void gfx_blit_rgba32(long x, long y, long width, long height, const u32 *pixels, long pitch_pixels);
/* Nearest-neighbor scaled image blits. Source pixels are row-major 32-bit RGB/RGBA. */
void gfx_blit_scaled_rgb32(long x, long y, long source_width, long source_height,
                           long destination_width, long destination_height,
                           const u32 *pixels, long pitch_pixels);
void gfx_blit_scaled_rgba32(long x, long y, long source_width, long source_height,
                            long destination_width, long destination_height,
                            const u32 *pixels, long pitch_pixels);
/* Returns the selected surface pixel as opaque 0x00RRGGBB; zero if invalid. */
long gfx_get_pixel(long x, long y);
void gfx_line(long x1, long y1, long x2, long y2, long color);
void gfx_rect(long x, long y, long width, long height, long color);
void gfx_fill_rect(long x, long y, long width, long height, long color);
void gfx_circle(long center_x, long center_y, long radius, long color);
void gfx_fill_circle(long center_x, long center_y, long radius, long color);
void gfx_ellipse(long center_x, long center_y, long radius_x, long radius_y, long color);
void gfx_fill_ellipse(long center_x, long center_y, long radius_x, long radius_y, long color);
void gfx_rounded_rect(long x, long y, long width, long height, long radius, long color);
void gfx_fill_rounded_rect(long x, long y, long width, long height, long radius, long color);
void gfx_gradient_rect(long x, long y, long width, long height,
                       long first_color, long last_color, long vertical);
void gfx_text(long x, long y, const char *text, long color);
/* Modern UI font: anti-aliased 10x16 glyphs, with regular/bold weights. */
void gfx_text_ui(long x, long y, const char *text, long color);
void gfx_text_ui_bold(long x, long y, const char *text, long color);
void gfx_text_ui_scale(long x, long y, const char *text, long color,
                       long scale, long bold);
void gfx_text_centered(long x, long y, long width, const char *text, long color);
long gfx_text_width(const char *text);
long gfx_width(void);
long gfx_height(void);
long gfx_wait_key(void);
void gfx_delay(long ticks); /* 100 ticks is about one second. */

/* Extra drawing helpers. Coordinates and dimensions are measured in pixels. */
void gfx_triangle(long x1, long y1, long x2, long y2,
                  long x3, long y3, long color);
void gfx_fill_triangle(long x1, long y1, long x2, long y2,
                       long x3, long y3, long color);
/* Polygon routines accept 3..64 interleaved x,y vertices. Coordinates are clipped. */
void gfx_polygon(const long *xy, long point_count, long color);
void gfx_fill_polygon(const long *xy, long point_count, long color);
void gfx_text_scale(long x, long y, const char *text, long color, long scale);
/* Draw anti-aliased text with word wrapping. Returns the rendered height. */
long gfx_text_wrapped(long x, long y, long maximum_width, const char *text,
                      long color, long line_height);
void gfx_button(long x, long y, long width, long height,
                const char *label, long pressed);
void gfx_progress_bar(long x, long y, long width, long height,
                      long value, long maximum, long fill_color, long background_color);

/* Up to four independently buffered windows are supported. Window IDs are
   stable for the current graphics session. ESC or the red X closes focus. */
int gfx_window_create(long x, long y, long width, long height, const char *title);
void gfx_window_redraw(void);
void gfx_window_close(void);
long gfx_window_wait_key(void);
long gfx_window_content_x(void);
long gfx_window_content_y(void);
long gfx_window_content_width(void);
long gfx_window_content_height(void);
long gfx_window_is_open(void);
long gfx_window_is_dragging(void);
long gfx_window_is_resizing(void);
long gfx_window_width(void);
long gfx_window_height(void);

/* Small desktop/window manager. Window IDs are returned by create(). */
int gfx_desktop_start(long background_color);
void gfx_desktop_redraw(void);
int gfx_desktop_add_icon(long x, long y, const char *label, long color);
long gfx_desktop_icon_clicked(void);
long gfx_desktop_is_active(void);
long gfx_desktop_wait_event(void);
int gfx_window_select(long window_id);
long gfx_window_active(void);
long gfx_window_count(void);
long gfx_window_is_open_id(long window_id);
void gfx_window_close_id(long window_id);
void gfx_window_minimize(void);
long gfx_window_is_minimized(long window_id);
int gfx_window_focus_next(void);
int gfx_window_focus_previous(void);

/* Queued events. Poll returns GFX_EVENT_NONE when no event is ready. The
   event accessors describe the most recently returned event. */
long gfx_event_poll(void);
long gfx_event_wait(void);
long gfx_event_type(void);
long gfx_event_key(void);
long gfx_event_x(void);
long gfx_event_y(void);
long gfx_event_window(void);
long gfx_event_buttons(void);
long gfx_event_code(void);

/* Drawing calls are automatically presented after each call. Use begin/end
   to group multiple calls into one flicker-free frame. */
void gfx_begin_frame(void);
void gfx_end_frame(void);
void gfx_present(void);

/* Polled mouse state. Pressed/released are edges from the latest event. */
long gfx_mouse_available(void);
long gfx_mouse_x(void);
long gfx_mouse_y(void);
long gfx_mouse_buttons(void);
long gfx_mouse_pressed(void);
long gfx_mouse_released(void);
/* Returns 1 once when the latest left click landed inside the rectangle. */
long gfx_button_clicked(long x, long y, long width, long height);


#endif
