#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MOUSE_LEFT 0x01
#define MOUSE_RIGHT 0x02
#define MOUSE_BACK 0x08
#define MOUSE_FORWARD 0x10
#define MOUSE_QUEUE_SIZE 32

typedef struct {
    bool scroll_mode;
    uint8_t buttons;
    uint8_t queue[MOUSE_QUEUE_SIZE];
    unsigned head, count;
    int32_t rem_x, rem_y, rem_wheel, rem_pan;
} mouse_t;

void mouse_button(mouse_t *mouse, unsigned button, bool pressed);
void mouse_chord(mouse_t *mouse, unsigned chord, bool pressed);
void mouse_release(mouse_t *mouse);
void mouse_motion(mouse_t *mouse, int x, int y, int8_t *dx, int8_t *dy, int8_t *wheel, int8_t *pan);
bool mouse_peek(const mouse_t *mouse, uint8_t *buttons);
void mouse_pop(mouse_t *mouse);
