/* arch/keyboard.c -- a model of the Archimedes keyboard. */

/* Tell the Archimedes that key (row, col) has changed state.  Mouse
 * button presses look like key presses on row 7 as far as the
 * Archimedes is concerned. */
void keyboard_key_changed(struct arch_keyboard *kb, int row, int col,
    int up);
