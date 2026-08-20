/*
 *  DOSBox-X Kindle on-screen keyboard (soft keyboard)
 *
 *  Splits the SDL window into two regions:
 *    - upper 2/3 : the DOSBox display (touch events are routed as mouse)
 *    - lower 1/3 : a 5-row soft keyboard (touch events drive the keyboard)
 *
 *  Modifier keys (Ctrl / Alt / Win / Fn / Caps) are latching toggles;
 *  Shift is one-shot. This is optimised for single-touch e-ink screens.
 */
#include "sdl_osk.h"

#include <string.h>

#include "dosbox.h"
#include "sdlmain.h"
#include "keyboard.h"
#include "setup.h"
#include "control.h"

/* 8x16 BIOS font (see src/ints/int10.cpp) used to draw key labels. */
extern uint8_t int10_font_16[256 * 16];

/* ------------------------------------------------------------------------- */
/* Key layout                                                                 */
/* ------------------------------------------------------------------------- */

enum OSKKeyKind {
    OSK_NORMAL,   /* plain key: press + release with current modifiers */
    OSK_CTRL,     /* latching Ctrl  */
    OSK_ALT,      /* latching Alt   */
    OSK_WIN,      /* latching Win   */
    OSK_SHIFT,    /* one-shot Shift */
    OSK_FN,       /* latching Fn layer switch */
    OSK_CAPS,     /* latching Caps Lock */
};

struct OSKKey {
    const char *label;     /* normal label */
    const char *fn_label;  /* Fn-layer label (NULL = unchanged) */
    KBD_KEYS key;          /* key code to send */
    KBD_KEYS fn_key;       /* key code in Fn layer */
    OSKKeyKind kind;
    float width;           /* relative width within the row */
};

#define K(key, lbl, knd, w)   { lbl, NULL, key, key, knd, w }
#define KF(key, lbl, fnl, fnk, knd, w) { lbl, fnl, key, fnk, knd, w }

static const OSKKey row0[] = {
    K (KBD_esc,        "Esc", OSK_NORMAL, 1.0f),
    KF(KBD_1,          "1", "F1",  KBD_f1,  OSK_NORMAL, 1.0f),
    KF(KBD_2,          "2", "F2",  KBD_f2,  OSK_NORMAL, 1.0f),
    KF(KBD_3,          "3", "F3",  KBD_f3,  OSK_NORMAL, 1.0f),
    KF(KBD_4,          "4", "F4",  KBD_f4,  OSK_NORMAL, 1.0f),
    KF(KBD_5,          "5", "F5",  KBD_f5,  OSK_NORMAL, 1.0f),
    KF(KBD_6,          "6", "F6",  KBD_f6,  OSK_NORMAL, 1.0f),
    KF(KBD_7,          "7", "F7",  KBD_f7,  OSK_NORMAL, 1.0f),
    KF(KBD_8,          "8", "F8",  KBD_f8,  OSK_NORMAL, 1.0f),
    KF(KBD_9,          "9", "F9",  KBD_f9,  OSK_NORMAL, 1.0f),
    KF(KBD_0,          "0", "F10", KBD_f10, OSK_NORMAL, 1.0f),
    KF(KBD_minus,      "-", "F11", KBD_f11, OSK_NORMAL, 1.0f),
    KF(KBD_equals,     "=", "F12", KBD_f12, OSK_NORMAL, 1.0f),
    K (KBD_backspace,  "Bksp", OSK_NORMAL, 1.6f),
};

static const OSKKey row1[] = {
    K (KBD_tab,          "Tab", OSK_NORMAL, 1.3f),
    K (KBD_q,            "Q",   OSK_NORMAL, 1.0f),
    K (KBD_w,            "W",   OSK_NORMAL, 1.0f),
    K (KBD_e,            "E",   OSK_NORMAL, 1.0f),
    K (KBD_r,            "R",   OSK_NORMAL, 1.0f),
    K (KBD_t,            "T",   OSK_NORMAL, 1.0f),
    K (KBD_y,            "Y",   OSK_NORMAL, 1.0f),
    K (KBD_u,            "U",   OSK_NORMAL, 1.0f),
    K (KBD_i,            "I",   OSK_NORMAL, 1.0f),
    K (KBD_o,            "O",   OSK_NORMAL, 1.0f),
    K (KBD_p,            "P",   OSK_NORMAL, 1.0f),
    K (KBD_leftbracket,  "[",   OSK_NORMAL, 1.0f),
    K (KBD_rightbracket, "]",   OSK_NORMAL, 1.0f),
    K (KBD_backslash,    "\\",  OSK_NORMAL, 1.0f),
};

static const OSKKey row2[] = {
    K (KBD_capslock,  "Caps", OSK_CAPS,   1.5f),
    K (KBD_a,         "A",    OSK_NORMAL, 1.0f),
    K (KBD_s,         "S",    OSK_NORMAL, 1.0f),
    K (KBD_d,         "D",    OSK_NORMAL, 1.0f),
    K (KBD_f,         "F",    OSK_NORMAL, 1.0f),
    K (KBD_g,         "G",    OSK_NORMAL, 1.0f),
    K (KBD_h,         "H",    OSK_NORMAL, 1.0f),
    K (KBD_j,         "J",    OSK_NORMAL, 1.0f),
    K (KBD_k,         "K",    OSK_NORMAL, 1.0f),
    K (KBD_l,         "L",    OSK_NORMAL, 1.0f),
    K (KBD_semicolon, ";",    OSK_NORMAL, 1.0f),
    K (KBD_quote,     "'",    OSK_NORMAL, 1.0f),
    K (KBD_enter,     "Ent",  OSK_NORMAL, 1.8f),
};

static const OSKKey row3[] = {
    K (KBD_leftshift, "Shift", OSK_SHIFT,  1.8f),
    K (KBD_z,         "Z",     OSK_NORMAL, 1.0f),
    K (KBD_x,         "X",     OSK_NORMAL, 1.0f),
    K (KBD_c,         "C",     OSK_NORMAL, 1.0f),
    K (KBD_v,         "V",     OSK_NORMAL, 1.0f),
    K (KBD_b,         "B",     OSK_NORMAL, 1.0f),
    K (KBD_n,         "N",     OSK_NORMAL, 1.0f),
    K (KBD_m,         "M",     OSK_NORMAL, 1.0f),
    K (KBD_comma,     ",",     OSK_NORMAL, 1.0f),
    K (KBD_period,    ".",     OSK_NORMAL, 1.0f),
    K (KBD_slash,     "/",     OSK_NORMAL, 1.0f),
    K (KBD_up,        "Up",    OSK_NORMAL, 1.0f),
};

static const OSKKey row4[] = {
    K (KBD_leftctrl,  "Ctrl", OSK_CTRL,  1.3f),
    K (KBD_lwindows,  "Win",  OSK_WIN,   1.0f),
    K (KBD_leftalt,   "Alt",  OSK_ALT,   1.0f),
    K (KBD_space,     "Space", OSK_NORMAL, 4.0f),
    K (KBD_NONE,      "Fn",   OSK_FN,    1.0f),
    K (KBD_left,      "Left", OSK_NORMAL, 1.0f),
    K (KBD_down,      "Down", OSK_NORMAL, 1.0f),
    K (KBD_right,     "Right", OSK_NORMAL, 1.0f),
};

struct OSKRow {
    const OSKKey *keys;
    int count;
};

static const OSKRow osk_rows[5] = {
    { row0, (int)(sizeof(row0) / sizeof(row0[0])) },
    { row1, (int)(sizeof(row1) / sizeof(row1[0])) },
    { row2, (int)(sizeof(row2) / sizeof(row2[0])) },
    { row3, (int)(sizeof(row3) / sizeof(row3[0])) },
    { row4, (int)(sizeof(row4) / sizeof(row4[0])) },
};

/* ------------------------------------------------------------------------- */
/* State                                                                      */
/* ------------------------------------------------------------------------- */

static bool osk_enabled = false;

/* Keyboard area in window coordinates (recomputed on each layout). */
static SDL_Rect osk_rect = { 0, 0, 0, 0 };

/* Latching/one-shot modifier state. */
static bool mod_ctrl  = false;
static bool mod_alt   = false;
static bool mod_win   = false;
static bool mod_fn    = false;
static bool mod_caps  = false;
static bool mod_shift = false;   /* one-shot */

/* Index of the key currently held (for press feedback). -1 = none. */
static int pressed_key = -1;

/* True while the current pointer/touch interaction belongs to the keyboard. */
static bool osk_owned = false;

/* Computed key rectangles (row-major, flattened). */
static SDL_Rect osk_key_rects[5][16];
static int osk_key_counts[5];

/* ------------------------------------------------------------------------- */

void OSK_Init(void)
{
    osk_enabled = true;
    Section_prop *section = static_cast<Section_prop *>(control->GetSection("sdl"));
    if (section != NULL)
        osk_enabled = section->Get_bool("onscreen_keyboard");
}

bool OSK_Enabled(void)
{
    return osk_enabled;
}

int OSK_KeyboardHeight(int window_width, int window_height)
{
    (void)window_width;
    if (!osk_enabled || window_height < 120)
        return 0;
    return window_height / 3;
}

/* Lay out the key rectangles for the current keyboard rect. */
static void OSK_Layout(void)
{
    const int rows = 5;
    const int row_h = osk_rect.h / rows;

    for (int r = 0; r < rows; r++) {
        const OSKRow &row = osk_rows[r];
        int n = row.count;
        osk_key_counts[r] = n;

        float total = 0.0f;
        for (int i = 0; i < n; i++)
            total += row.keys[i].width;

        const float unit = (float)osk_rect.w / total;
        int x = osk_rect.x;
        const int y = osk_rect.y + r * row_h;

        for (int i = 0; i < n; i++) {
            const int kw = (int)(row.keys[i].width * unit);
            SDL_Rect &rc = osk_key_rects[r][i];
            rc.x = x + 2;
            rc.y = y + 2;
            rc.w = kw - 4;
            rc.h = row_h - 4;
            if (rc.w < 8) rc.w = 8;
            if (rc.h < 8) rc.h = 8;
            x += kw;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Drawing                                                                    */
/* ------------------------------------------------------------------------- */

static void OSK_DrawChar(SDL_Surface *surface, int x, int y, unsigned char c, Uint32 color)
{
    if (surface->format->BitsPerPixel != 32)
        return;
    if (x < 0 || y < 0 || x + 8 > surface->w || y + 16 > surface->h)
        return;

    const unsigned char *bmp = int10_font_16 + (c * 16);
    for (int row = 0; row < 16; row++) {
        const unsigned char bits = bmp[row];
        Uint32 *dst = (Uint32 *)surface->pixels + (size_t)(y + row) * (surface->pitch / 4) + (size_t)x;
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                dst[col] = color;
        }
    }
}

static void OSK_DrawText(SDL_Surface *surface, const SDL_Rect &rc, const char *text, Uint32 color)
{
    if (text == NULL)
        return;
    const int w = (int)strlen(text) * 8;
    int x = rc.x + (rc.w - w) / 2;
    int y = rc.y + (rc.h - 16) / 2;
    for (const char *p = text; *p; p++) {
        OSK_DrawChar(surface, x, y, (unsigned char)*p, color);
        x += 8;
    }
}

void OSK_Draw(SDL_Surface *surface)
{
    if (!osk_enabled || surface == NULL || surface->w == 0 || surface->h == 0)
        return;

    osk_rect.x = 0;
    osk_rect.y = surface->h - surface->h / 3;
    osk_rect.w = surface->w;
    osk_rect.h = surface->h / 3;

    OSK_Layout();

    const Uint32 c_bg     = SDL_MapRGB(surface->format, 30, 32, 40);
    const Uint32 c_key    = SDL_MapRGB(surface->format, 200, 202, 210);
    const Uint32 c_border = SDL_MapRGB(surface->format, 90, 92, 100);
    const Uint32 c_text   = SDL_MapRGB(surface->format, 15, 15, 20);
    const Uint32 c_active = SDL_MapRGB(surface->format, 110, 180, 255);
    const Uint32 c_atext  = SDL_MapRGB(surface->format, 5, 10, 20);
    const Uint32 c_press  = SDL_MapRGB(surface->format, 255, 210, 110);

    /* keyboard background */
    SDL_FillRect(surface, &osk_rect, c_bg);

    int flat = 0;
    for (int r = 0; r < 5; r++) {
        const OSKRow &row = osk_rows[r];
        for (int i = 0; i < osk_key_counts[r]; i++, flat++) {
            const OSKKey &key = row.keys[i];
            const SDL_Rect &rc = osk_key_rects[r][i];

            bool active = false;
            switch (key.kind) {
                case OSK_CTRL:  active = mod_ctrl;  break;
                case OSK_ALT:   active = mod_alt;   break;
                case OSK_WIN:   active = mod_win;   break;
                case OSK_FN:    active = mod_fn;    break;
                case OSK_CAPS:  active = mod_caps;  break;
                case OSK_SHIFT: active = mod_shift; break;
                default: break;
            }
            const bool pressed = (flat == pressed_key);

            const Uint32 fill = pressed ? c_press : (active ? c_active : c_key);
            const Uint32 text = pressed ? c_atext : (active ? c_atext : c_text);

            SDL_FillRect(surface, &rc, fill);

            /* border */
            SDL_Rect border = rc;
            SDL_FillRect(surface, &border, c_border);
            SDL_Rect inner = rc;
            inner.x += 1; inner.y += 1; inner.w -= 2; inner.h -= 2;
            SDL_FillRect(surface, &inner, fill);

            const char *label = (mod_fn && key.fn_label != NULL) ? key.fn_label : key.label;
            OSK_DrawText(surface, inner, label, text);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Input                                                                      */
/* ------------------------------------------------------------------------- */

static int OSK_KeyAt(int x, int y)
{
    if (!osk_enabled)
        return -1;
    if (x < osk_rect.x || x >= osk_rect.x + osk_rect.w ||
        y < osk_rect.y || y >= osk_rect.y + osk_rect.h)
        return -1;

    int flat = 0;
    for (int r = 0; r < 5; r++) {
        for (int i = 0; i < osk_key_counts[r]; i++, flat++) {
            const SDL_Rect &rc = osk_key_rects[r][i];
            if (x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h)
                return flat;
        }
    }
    return -1;
}

static void OSK_Redraw(void)
{
    if (!osk_enabled || sdl.surface == NULL || sdl.window == NULL)
        return;
    OSK_Draw(sdl.surface);
    SDL_UpdateWindowSurfaceRects(sdl.window, &osk_rect, 1);
}

static void OSK_PressKey(int flat)
{
    if (flat < 0)
        return;

    /* locate the key */
    const OSKKey *key = NULL;
    {
        int n = flat;
        for (int r = 0; r < 5; r++) {
            if (n < osk_key_counts[r]) { key = &osk_rows[r].keys[n]; break; }
            n -= osk_key_counts[r];
        }
    }
    if (key == NULL)
        return;

    const KBD_KEYS code = (mod_fn && key->fn_key != KBD_NONE) ? key->fn_key : key->key;

    switch (key->kind) {
        case OSK_CTRL:
            mod_ctrl = !mod_ctrl;
            KEYBOARD_AddKey(KBD_leftctrl, mod_ctrl);
            break;
        case OSK_ALT:
            mod_alt = !mod_alt;
            KEYBOARD_AddKey(KBD_leftalt, mod_alt);
            break;
        case OSK_WIN:
            mod_win = !mod_win;
            KEYBOARD_AddKey(KBD_lwindows, mod_win);
            break;
        case OSK_SHIFT:
            mod_shift = !mod_shift;   /* one-shot */
            break;
        case OSK_FN:
            mod_fn = !mod_fn;
            break;
        case OSK_CAPS:
            mod_caps = !mod_caps;
            KEYBOARD_AddKey(KBD_capslock, true);
            KEYBOARD_AddKey(KBD_capslock, false);
            break;
        case OSK_NORMAL:
        default:
            if (code != KBD_NONE) {
                if (mod_shift) KEYBOARD_AddKey(KBD_leftshift, true);
                KEYBOARD_AddKey(code, true);
                KEYBOARD_AddKey(code, false);
                if (mod_shift) {
                    KEYBOARD_AddKey(KBD_leftshift, false);
                    mod_shift = false;   /* one-shot consumed */
                }
            }
            break;
    }
}

bool OSK_HandleButton(int x, int y, bool pressed)
{
    if (!osk_enabled)
        return false;

    if (pressed) {
        const int key = OSK_KeyAt(x, y);
        if (key < 0) {
            osk_owned = false;
            return false;   /* outside keyboard; let DOSBox treat it as mouse */
        }
        osk_owned = true;
        pressed_key = key;
        OSK_PressKey(key);
        OSK_Redraw();
        return true;
    } else {
        if (!osk_owned)
            return false;
        osk_owned = false;
        if (pressed_key >= 0) {
            pressed_key = -1;
            OSK_Redraw();
        }
        return true;
    }
}

bool OSK_HandleMotion(int x, int y)
{
    if (!osk_enabled)
        return false;
    if (!osk_owned)
        return false;   /* not dragging a key */

    const int key = OSK_KeyAt(x, y);
    if (key != pressed_key) {
        pressed_key = key;
        OSK_Redraw();
    }
    return true;   /* keep consuming motion while a key is held */
}
