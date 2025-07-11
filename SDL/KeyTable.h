/* Virtual Key codes */

typedef struct {
    SDL_Keycode sym;
    arch_key_id kid;
} sdlk_to_arch_key;

#define X(sym, kid) { SDLK_ ## sym, ARCH_KEY_ ## kid },
static const sdlk_to_arch_key sdlk_to_arch_key_map[] = {
    X(ESCAPE, escape)
    X(F1, f1)
    X(F2, f2)
    X(F3, f3)
    X(F4, f4)
    X(F5, f5)
    X(F6, f6)
    X(F7, f7)
    X(F8, f8)
    X(F9, f9)
    X(F10, f10)
    X(F11, f11)
    X(F12, f12)

    X(GRAVE, grave)
    X(1, 1)
    X(2, 2)
    X(3, 3)
    X(4, 4)
    X(5, 5)
    X(6, 6)
    X(7, 7)
    X(8, 8)
    X(9, 9)
    X(0, 0)
    X(MINUS, minus)
    X(EQUALS, equal)
    X(HASH, sterling)
    X(BACKSPACE, backspace)

    X(TAB, tab)
    X(Q, q)
    X(W, w)
    X(E, e)
    X(R, r)
    X(T, t)
    X(Y, y)
    X(U, u)
    X(I, i)
    X(O, o)
    X(P, p)
    X(LEFTBRACKET, bracket_l)
    X(RIGHTBRACKET, bracket_r)
    X(BACKSLASH, backslash)

    X(LCTRL, control_l)
    X(A, a)
    X(S, s)
    X(D, d)
    X(F, f)
    X(G, g)
    X(H, h)
    X(J, j)
    X(K, k)
    X(L, l)
    X(SEMICOLON, semicolon)
    X(APOSTROPHE, apostrophe)
    X(RETURN, return)

    X(LSHIFT, shift_l)
    X(Z, z)
    X(X, x)
    X(C, c)
    X(V, v)
    X(B, b)
    X(N, n)
    X(M, m)
    X(COMMA, comma)
    X(PERIOD, period)
    X(SLASH, slash)
    X(RSHIFT, shift_r)

    X(CAPSLOCK, caps_lock)
    X(LALT, alt_l)
    X(SPACE, space)
    X(RALT, alt_r)
    X(RCTRL, control_r)

    X(PRINTSCREEN, print)
    X(SCROLLLOCK, scroll_lock)
    X(PAUSE, break)
    X(INSERT, insert)
    X(HOME, home)
    X(PAGEUP, page_up)
    X(DELETE, delete)
    X(END, copy)
    X(PAGEDOWN, page_down)

    X(UP, up)
    X(LEFT, left)
    X(DOWN, down)
    X(RIGHT, right)

    X(NUMLOCKCLEAR, num_lock)
    X(KP_DIVIDE, kp_slash)
    X(KP_MULTIPLY, kp_star)
#if SDL_VERSION_ATLEAST(2, 0, 0)
    X(KP_HASH, kp_hash)
#endif
    X(KP_7, kp_7)
    X(KP_8, kp_8)
    X(KP_9, kp_9)
    X(KP_MINUS, kp_minus)
    X(KP_4, kp_4)
    X(KP_5, kp_5)
    X(KP_6, kp_6)
    X(KP_PLUS, kp_plus)
    X(KP_1, kp_1)
    X(KP_2, kp_2)
    X(KP_3, kp_3)
    X(KP_0, kp_0)
    X(KP_PERIOD, kp_decimal)
    X(KP_ENTER, kp_enter)

    { 0, 0 },
};
#undef X
