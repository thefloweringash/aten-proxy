#include <stdint.h>
#include <rfb/rfb.h>
#include <rfb/keysym.h>
#include "keymap.h"

static struct keymap_t {
    rfbKeySym keysym;
    uint8_t hid;
} keymap[] = {
    { XK_a, 0x04 },
    { XK_b, 0x05 },
    { XK_c, 0x06 },
    { XK_d, 0x07 },
    { XK_e, 0x08 },
    { XK_f, 0x09 },
    { XK_g, 0x0a },
    { XK_h, 0x0b },
    { XK_i, 0x0c },
    { XK_j, 0x0d },
    { XK_k, 0x0e },
    { XK_l, 0x0f },
    { XK_m, 0x10 },
    { XK_n, 0x11 },
    { XK_o, 0x12 },
    { XK_p, 0x13 },
    { XK_q, 0x14 },
    { XK_r, 0x15 },
    { XK_s, 0x16 },
    { XK_t, 0x17 },
    { XK_u, 0x18 },
    { XK_v, 0x19 },
    { XK_w, 0x1a },
    { XK_x, 0x1b },
    { XK_y, 0x1c },
    { XK_z, 0x1d },
    { XK_1, 0x1e },
    { XK_2, 0x1f },
    { XK_3, 0x20 },
    { XK_4, 0x21 },
    { XK_5, 0x22 },
    { XK_6, 0x23 },
    { XK_7, 0x24 },
    { XK_8, 0x25 },
    { XK_9, 0x26 },
    { XK_0, 0x27 },

    { XK_Return, 0x28 },
    { XK_Escape, 0x29 },
    { XK_BackSpace, 0x2a },
    { XK_Tab, 0x2b },
    { XK_space, 0x2c },
    { XK_minus, 0x2d },
    { XK_equal, 0x2e },
    { XK_bracketleft, 0x2f },
    { XK_bracketright, 0x30 },
    { XK_backslash, 0x31 },
    { XK_semicolon, 0x33 },
    { XK_apostrophe, 0x34 },
    { XK_grave, 0x35 },
    { XK_comma, 0x36 },
    { XK_period, 0x37 },
    { XK_slash, 0x38 },

    // this is a bit ugly. We have the result of os x applying our
    // local keyboard layout, and we only have to combined key. We
    // undo this mapping by picking an arbitrary layout that might
    // have resulted in the input we see
    { XK_less, 0x36 }, // ,
    { XK_greater, 0x37 }, // .
    { XK_exclam, 0x1e }, // 1
    { XK_at, 0x1f }, // 2
    { XK_numbersign, 0x20 }, // 3
    { XK_dollar, 0x21 }, // 4
    { XK_percent, 0x22 }, // 5
    { XK_asciicircum, 0x23 }, // 6
    { XK_ampersand, 0x24 }, // 7
    { XK_asterisk, 0x25 }, // 8
    { XK_parenleft, 0x26 }, // 9
    { XK_parenright, 0x27 }, // 0
    { XK_underscore, 0x2d }, // -
    { XK_bar, 0x31 }, // backslash
    { XK_quotedbl, 0x34 }, // '
    { XK_asciitilde, 0x35 }, // `
	{ XK_question, 0x38 },

	{ XK_colon, 0x33 }, // ;

    // and the alphabet
    { XK_A, 0x04 },
    { XK_B, 0x05 },
    { XK_C, 0x06 },
    { XK_D, 0x07 },
    { XK_E, 0x08 },
    { XK_F, 0x09 },
    { XK_G, 0x0A },
    { XK_H, 0x0B },
    { XK_I, 0x0C },
    { XK_J, 0x0D },
    { XK_K, 0x0E },
    { XK_L, 0x0F },
    { XK_M, 0x10 },
    { XK_N, 0x11 },
    { XK_O, 0x12 },
    { XK_P, 0x13 },
    { XK_Q, 0x14 },
    { XK_R, 0x15 },
    { XK_S, 0x16 },
    { XK_T, 0x17 },
    { XK_U, 0x18 },
    { XK_V, 0x19 },
    { XK_W, 0x1A },
    { XK_X, 0x1B },
    { XK_Y, 0x1C },
    { XK_Z, 0x1D },

    {XK_F1, 0x3a},
    {XK_F2, 0x3b},
    {XK_F3, 0x3c},
    {XK_F4, 0x3d},
    {XK_F5, 0x3e},
    {XK_F6, 0x3f},
    {XK_F7, 0x40},
    {XK_F8, 0x41},
    {XK_F9, 0x42},
    {XK_F10, 0x43},
    {XK_F11, 0x44},
    {XK_F12, 0x45},

	{XK_F13, 0x68},
	{XK_F14, 0x69},
	{XK_F15, 0x6a},
	{XK_F16, 0x6b},
	{XK_F17, 0x6c},
	{XK_F18, 0x6d},
	{XK_F19, 0x6e},
	{XK_F20, 0x6f},
	{XK_F21, 0x70},
	{XK_F22, 0x71},
	{XK_F23, 0x72},
	{XK_F24, 0x73},
	
	{XK_Home, 0x4a},
	{XK_Left, 0x50},
	{XK_Up, 0x52},
	{XK_Right, 0x4f},
	{XK_Down, 0x51},
	{XK_Prior, 0x4b},
	{XK_Next, 0x4e},
	{XK_End, 0x4d},

    // {NSPrintScreenFunctionKey, 0x46},
    // {NSScrollLockFunctionKey, 0x47},
    // {NSPauseFunctionKey, 0x48},
    // {NSInsertFunctionKey, 0x49},
    // {NSHomeFunctionKey, 0x4a},
    // {NSPageUpFunctionKey, 0x4b},
    // {NSDeleteFunctionKey, 0x4c},
    // {NSEndFunctionKey, 0x4d},
    // {NSPageDownFunctionKey, 0x4e},
    // {NSRightArrowFunctionKey, 0x4f},
    // {NSLeftArrowFunctionKey, 0x50},
    // {NSDownArrowFunctionKey, 0x51},
    // {NSUpArrowFunctionKey, 0x52},

	{XK_Shift_L, 0xE1},
	{XK_Shift_R, 0xE5},
	{XK_Control_L, 0xE0},
	{XK_Control_R, 0xE4},
	{XK_Alt_L, 0xE2},
	{XK_Alt_R, 0xE6},
};

static int compare_keysym(const void *lv, const void *rv) {
    const struct keymap_t *left = (const struct keymap_t*) lv;
    const struct keymap_t *right = (const struct keymap_t*) rv;
    return left->keysym - right->keysym;
}

void keymap_init() {
    qsort(&keymap[0], sizeof(keymap) / sizeof(*keymap), sizeof(*keymap),
          compare_keysym);
}

uint8_t keymap_usageForKeysym(rfbKeySym c) {
    const struct keymap_t *mapping = (struct keymap_t*)
		bsearch(&c, &keymap[0], sizeof(keymap) / sizeof(*keymap), sizeof(*keymap),
				compare_keysym);
    if (mapping) {
        return mapping->hid;
    }
    else {
        return 0;
    }
}
