#ifndef SOFT3D_INPUT_H
#define SOFT3D_INPUT_H

#include <stdint.h>

typedef enum {
    SOFT3D_KEY_NONE = 0,
    SOFT3D_KEY_SELECT,
    SOFT3D_KEY_DOWN,
    SOFT3D_KEY_UP,
    SOFT3D_KEY_LEFT,
    SOFT3D_KEY_RIGHT,
    SOFT3D_KEY_INVALID,
    SOFT3D_KEY_SELECT_HOLD,
    SOFT3D_KEY_DOWN_HOLD
} Soft3D_Key;

typedef struct {
    Soft3D_Key candidate;
    uint32_t since_ms;
    uint8_t pressed;
} Soft3D_Debounce;

typedef struct {
    Soft3D_Debounce debounce;
    uint32_t held_since_ms;
    Soft3D_Key pending;
    uint8_t hold_sent;
} Soft3D_KeyGesture;

Soft3D_Key Soft3D_KeyDecode(uint16_t adc);
/* One event per press; a stable release is required before the next event. */
Soft3D_Key Soft3D_KeyUpdate(Soft3D_Debounce *state, uint16_t adc, uint32_t now_ms);
/* Center/down emit on release or once after an 800 ms hold, never both. */
Soft3D_Key Soft3D_KeyGestureUpdate(Soft3D_KeyGesture *state, uint16_t adc, uint32_t now_ms);
/* Cancel pending gestures after a sampling failure. A stable release must occur
 * before any held key can become a new press.
 */
void Soft3D_KeyGestureCancel(Soft3D_KeyGesture *state, uint32_t now_ms);

#endif
