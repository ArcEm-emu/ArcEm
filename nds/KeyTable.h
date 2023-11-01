/* Virtual Key codes */

#include <nds/input.h>

typedef struct {
    int sym;
    arch_key_id kid;
} button_to_arch_key;

/* TODO: Provide a GUI for remapping the buttons */
static const button_to_arch_key button_to_arch_key_map[] = {
    { KEY_Y,     ARCH_KEY_left      },
    { KEY_B,     ARCH_KEY_down      },
    { KEY_A,     ARCH_KEY_right     },
    { KEY_X,     ARCH_KEY_up        },
    { KEY_L,     ARCH_KEY_shift_r   },
    { KEY_R,     ARCH_KEY_control_r },
    { KEY_LEFT,  ARCH_KEY_button_1  },
    { KEY_DOWN,  ARCH_KEY_button_2  },
    { KEY_RIGHT, ARCH_KEY_button_3  },
    { KEY_UP,    ARCH_KEY_space     },
    { 0, 0 }
};

#include <nds/arm9/keyboard.h>

typedef struct {
    int sym;
    arch_key_id kid;
    bool isShift;
} dvk_to_arch_key;

#define X(sym, kid) { sym, ARCH_KEY_ ## kid, false },
#define C(sym, shift, kid) { sym, ARCH_KEY_ ## kid, false }, { shift, ARCH_KEY_ ## kid, true },
static const dvk_to_arch_key dvk_to_arch_key_map[] = {
    X(DVK_FOLD, escape)
    X(DVK_MENU, f12)

    C('`', '~', grave)
    C('1', '!', 1)
    C('2', '@', 2)
    C('3', '#', 3)
    C('4', '$', 4)
    C('5', '%', 5)
    C('6', '^', 6)
    C('7', '&', 7)
    C('8', '*', 8)
    C('9', '(', 9)
    C('0', ')', 0)
    C('-', '_', minus)
    C('=', '+', equal)
    X(DVK_BACKSPACE, backspace)

    X(DVK_TAB, tab)
    C('q', 'Q', q)
    C('w', 'W', w)
    C('e', 'E', e)
    C('r', 'R', r)
    C('t', 'T', t)
    C('y', 'Y', y)
    C('u', 'U', u)
    C('i', 'I', i)
    C('o', 'O', o)
    C('p', 'P', p)
    C('[', '{', bracket_l)
    C(']', '}', bracket_r)
    C('\\', '|', backslash)

    X(DVK_CTRL, control_l)
    C('a', 'A', a)
    C('s', 'S', s)
    C('d', 'D', d)
    C('f', 'F', f)
    C('g', 'G', g)
    C('h', 'H', h)
    C('j', 'J', j)
    C('k', 'K', k)
    C('l', 'L', l)
    C(';', ':', semicolon)
    C('\'', '"', apostrophe)
    X(DVK_ENTER, return)

    X(DVK_SHIFT, shift_l)
    C('z', 'Z', z)
    C('x', 'X', x)
    C('c', 'C', c)
    C('v', 'V', v)
    C('b', 'B', b)
    C('n', 'N', n)
    C('m', 'M', m)
    C(',', '<', comma)
    C('.', '>', period)
    C('/', '?', slash)

    X(DVK_CAPS, caps_lock)
    X(DVK_ALT, alt_l)
    X(DVK_SPACE, space)

    X(DVK_UP, up)
    X(DVK_LEFT, left)
    X(DVK_DOWN, down)
    X(DVK_RIGHT, right)

    { 0, 0, false },
};
#undef C
#undef X
