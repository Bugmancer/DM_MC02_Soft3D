#include "soft3d_input.h"

Soft3D_Key Soft3D_KeyDecode(uint16_t adc)
{
    static const uint16_t levels[] = {50U, 13000U, 26100U, 39100U, 52200U};
    uint32_t i;
    if (adc >= 60000U) return SOFT3D_KEY_NONE;
    for (i = 0U; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        int32_t difference = (int32_t)adc - (int32_t)levels[i];
        if (difference >= -1000 && difference <= 1000) {
            return (Soft3D_Key)(i + 1U);
        }
    }
    return SOFT3D_KEY_INVALID;
}

Soft3D_Key Soft3D_KeyUpdate(Soft3D_Debounce *state, uint16_t adc, uint32_t now_ms)
{
    Soft3D_Key key = Soft3D_KeyDecode(adc);
    if (key == SOFT3D_KEY_INVALID) {
        state->candidate = key;
        state->since_ms = now_ms;
        return SOFT3D_KEY_NONE;
    }
    if (key != state->candidate) {
        state->candidate = key;
        state->since_ms = now_ms;
    }
    if ((uint32_t)(now_ms - state->since_ms) < 60U) {
        return SOFT3D_KEY_NONE;
    }
    if (key == SOFT3D_KEY_NONE) {
        state->pressed = 0U;
    } else if (state->pressed == 0U) {
        state->pressed = 1U;
        return key;
    }
    return SOFT3D_KEY_NONE;
}

Soft3D_Key Soft3D_KeyGestureUpdate(Soft3D_KeyGesture *state, uint16_t adc, uint32_t now_ms)
{
    Soft3D_Key key = Soft3D_KeyUpdate(&state->debounce, adc, now_ms);
    if (key == SOFT3D_KEY_SELECT || key == SOFT3D_KEY_DOWN) {
        state->pending = key;
        state->hold_sent = 0U;
        state->held_since_ms = now_ms;
    } else if (key != SOFT3D_KEY_NONE) {
        return key;
    }
    if (state->pending != SOFT3D_KEY_NONE) {
        if (state->debounce.pressed == 0U) {
            Soft3D_Key released = state->pending;
            state->pending = SOFT3D_KEY_NONE;
            return state->hold_sent != 0U ? SOFT3D_KEY_NONE : released;
        }
        if (state->debounce.candidate != state->pending) {
            state->held_since_ms = now_ms;
        } else if (state->hold_sent == 0U && now_ms - state->held_since_ms >= 800U) {
            state->hold_sent = 1U;
            return state->pending == SOFT3D_KEY_SELECT ? SOFT3D_KEY_SELECT_HOLD : SOFT3D_KEY_DOWN_HOLD;
        }
    }
    return SOFT3D_KEY_NONE;
}

void Soft3D_KeyGestureCancel(Soft3D_KeyGesture *state, uint32_t now_ms)
{
    state->debounce.candidate = SOFT3D_KEY_INVALID;
    state->debounce.since_ms = now_ms;
    state->debounce.pressed = 1U;
    state->held_since_ms = now_ms;
    state->pending = SOFT3D_KEY_NONE;
    state->hold_sent = 0U;
}
