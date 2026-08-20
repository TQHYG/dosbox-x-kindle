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
#ifndef DOSBOX_SDL_OSK_H
#define DOSBOX_SDL_OSK_H

#include <SDL.h>

/* Initialise the on-screen keyboard (read configuration). */
void OSK_Init(void);

/* True if the on-screen keyboard is enabled. */
bool OSK_Enabled(void);

/* Height in pixels of the keyboard area for a window of the given size.
 * Returns 0 when disabled. */
int OSK_KeyboardHeight(int window_width, int window_height);

/* Draw the keyboard onto the window surface (bottom area). */
void OSK_Draw(SDL_Surface *surface);

/* Handle a pointer/touch event in window coordinates.
 * `pressed` is true for a button-down, false for button-up.
 * Returns true if the event was consumed by the keyboard. */
bool OSK_HandleButton(int x, int y, bool pressed);

/* Handle pointer motion (for press feedback / slide). Returns true if
 * consumed by the keyboard. */
bool OSK_HandleMotion(int x, int y);

#endif /* DOSBOX_SDL_OSK_H */
