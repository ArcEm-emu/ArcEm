/* arch/keyboard.c -- a model of the Archimedes keyboard. */

#include "armarc.h"
#include "DispKbd.h"
#include "arch/keyboard.h"

#define DEBUG_KEYBOARD 0

void keyboard_key_changed(struct arch_keyboard *kb, int row, int col,
    int up)
{
    KbdEntry *e;

    if (kb->BuffOcc >= KBDBUFFLEN) {
#if DEBUG_KEYBOARD
        fputs("keyboard_key_changed: key/button discarded, buffer "
            "would overflow\n", stderr);
#endif
        return;
    }

    e = kb->Buffer + kb->BuffOcc++;
    e->KeyColToSend = col;
    e->KeyRowToSend = row;
    e->KeyUpNDown = up;

#if DEBUG_KEYBOARD
    fprintf(stderr, "keyboard_key_changed: %2d, %2d going %-4s.  "
        "BuffOcc=%d\n", e->KeyColToSend, e->KeyRowToSend,
        e->KeyUpNDown ? "up" : "down", e - kb->Buffer);
#endif

    return;
}
