/* arch/keyboard.c -- a model of the Archimedes keyboard. */

#include "armarc.h"
#include "DispKbd.h"
#include "arch/keyboard.h"

#define DEBUG_KEYBOARD 0

typedef struct {
    char *name;
    unsigned char row;
    unsigned char col;
} key_info;

static key_info keys[] = {
#define X(key, row, col) { #key, row, col },
    ARCH_KEYBOARD_DEFINITION
#undef X
};

/* ------------------------------------------------------------------ */

void keyboard_key_changed(struct arch_keyboard *kb, arch_key_id kid,
    int up)
{
    key_info *ki;
    KbdEntry *e;

    ki = keys + kid;
    
#if DEBUG_KEYBOARD
    fprintf(stderr, "keyboard_key_changed(kb, \"%s\", %d)\n", ki->name,
        up);
#endif

    if (kb->BuffOcc >= KBDBUFFLEN) {
        fprintf(stderr, "keyboard_key_changed: key \"%s\" discarded, "
            "buffer full\n", ki->name);
        return;
    }

    e = kb->Buffer + kb->BuffOcc++;
    e->KeyColToSend = ki->col;
    e->KeyRowToSend = ki->row;
    e->KeyUpNDown = up;

    return;
}
