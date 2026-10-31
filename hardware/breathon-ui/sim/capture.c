#include "capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t framebuffer[240 * 320 * 3];
static uint8_t draw_buffer[240 * 320 * 3];
static lv_indev_t *pointer;
static lv_indev_data_t pointer_data;
unsigned sim_checks;
static unsigned flush_count;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(framebuffer + (y * 240 + area->x1) * 3,
               pixels + (y - area->y1) * width * 3, (size_t)width * 3);
    }
    lv_display_flush_ready(display);
    ++flush_count;
}

static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    *data = pointer_data;
}

void sim_init(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_pointer);
}

void sim_advance(uint32_t ms)
{
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 20) {
        lv_tick_inc(20);
        lv_timer_handler();
    }
}

void sim_press(int x, int y, bool pressed)
{
    pointer_data.point.x = x;
    pointer_data.point.y = y;
    pointer_data.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    /* A freshly rebuilt page must have layout before native hit testing, just
     * as it does on the rendered display. Do not rely on a preceding capture. */
    lv_obj_update_layout(lv_screen_active());
    lv_indev_read(pointer);
    sim_advance(40);
}

void sim_tap(int x, int y)
{
    sim_press(x, y, true);
    sim_press(x, y, false);
}

static uint32_t next_codepoint(const unsigned char **cursor)
{
    const unsigned char *s = *cursor;
    uint32_t cp = *s++;
    if (cp >= 0xc0) {
        int count = cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
        cp &= (1u << (6 - count)) - 1u;
        while (count--) {
            assert((*s & 0xc0) == 0x80);
            cp = (cp << 6) | (*s++ & 0x3f);
        }
    }
    *cursor = s;
    return cp;
}

static void verify_object(lv_obj_t *object)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    if (lv_obj_check_type(object, &lv_button_class)) {
        assert(area.x1 >= 0 && area.x2 < 240 && area.y1 >= 0 && area.y2 < 320);
        assert(lv_obj_get_height(object) >= 44);
        assert(lv_obj_get_width(object) >= 44);
        ++sim_checks;
    }
    if (lv_obj_check_type(object, &lv_label_class)) {
        const lv_font_t *font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        const unsigned char *text = (const unsigned char *)lv_label_get_text(object);
        while (*text) {
            uint32_t cp = next_codepoint(&text);
            if (cp < 32) continue;
            lv_font_glyph_dsc_t glyph;
            bool present = font->get_glyph_dsc(font, &glyph, cp, 0);
            if (!present) fprintf(stderr, "Missing glyph U+%04lX\n", (unsigned long)cp);
            assert(present);
            ++sim_checks;
        }
        assert(area.x1 >= 0 && area.x2 < 240);
        lv_obj_t *parent = lv_obj_get_parent(object);
        if (!lv_obj_has_flag(parent, LV_OBJ_FLAG_SCROLLABLE)) {
            lv_area_t bounds;
            lv_obj_get_coords(parent, &bounds);
            assert(area.y1 >= bounds.y1 && area.y2 <= bounds.y2);
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
        verify_object(lv_obj_get_child(object, (int32_t)i));
    }
}

void sim_verify(void)
{
    lv_obj_update_layout(lv_screen_active());
    verify_object(lv_screen_active());
}

lv_obj_t *sim_label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *label = sim_label(lv_obj_get_child(root, (int32_t)i), text);
        if (label) return label;
    }
    return NULL;
}

unsigned sim_timer_count(void)
{
    unsigned count = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) ++count;
    return count;
}

uint32_t sim_frame_hash(void)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(framebuffer); ++i) hash = (hash ^ framebuffer[i]) * 16777619u;
    return hash;
}

unsigned sim_flush_count(void) { return flush_count; }

static void u16(FILE *file, uint16_t value) { fwrite(&value, 2, 1, file); }
static void u32(FILE *file, uint32_t value) { fwrite(&value, 4, 1, file); }

void sim_capture(const char *name)
{
    lv_refr_now(NULL);
    sim_verify();
    char path[256];
    snprintf(path, sizeof(path), "evidence/%s.bmp", name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fwrite("BM", 1, 2, file);
    u32(file, 54 + sizeof(framebuffer)); u32(file, 0); u32(file, 54);
    u32(file, 40); u32(file, 240); u32(file, 320); u16(file, 1); u16(file, 24);
    u32(file, 0); u32(file, sizeof(framebuffer)); u32(file, 2835); u32(file, 2835);
    u32(file, 0); u32(file, 0);
    for (int y = 319; y >= 0; --y) fwrite(framebuffer + y * 720, 1, 720, file);
    fclose(file);
    printf("Captured %s (240x320, real LVGL)\n", name);
}
