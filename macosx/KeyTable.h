
/*****************************************************************************
 * 
 * Copyright (C) 2002 Michael Dales
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * Name   : KeyTable.h
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/

#include <Carbon/Carbon.h>

#include "armdefs.h"
#include "arch/keyboard.h"

typedef struct {
    int sym;
    arch_key_id kid;
} mac_to_arch_key;

#define A(sym, kid) { kVK_ANSI_ ## sym, ARCH_KEY_ ## kid },
#define X(sym, kid) { kVK_ ## sym, ARCH_KEY_ ## kid },
static const mac_to_arch_key mac_to_arch_key_map[] = {
    X(Escape,         escape)
    X(F1,             f1)
    X(F2,             f2)
    X(F3,             f3)
    X(F4,             f4)
    X(F5,             f5)
    X(F6,             f6)
    X(F7,             f7)
    X(F8,             f8)
    X(F9,             f9)
    X(F10,            f10)
    X(F11,            f11)
    X(F12,            f12)

    A(Grave,          grave)
    A(1,              1)
    A(2,              2)
    A(3,              3)
    A(4,              4)
    A(5,              5)
    A(6,              6)
    A(7,              7)
    A(8,              8)
    A(9,              9)
    A(0,              0)
    A(Minus,          minus)
    A(Equal,          equal)
    {0x0a,            ARCH_KEY_sterling}, /* This uses the § key for the pound key */
    X(Delete,         backspace)

    X(Tab,            tab)
    A(Q,              q)
    A(W,              w)
    A(E,              e)
    A(R,              r)
    A(T,              t)
    A(Y,              y)
    A(U,              u)
    A(I,              i)
    A(O,              o)
    A(P,              p)
    A(LeftBracket,    bracket_l)
    A(RightBracket,   bracket_r)
    A(Backslash,      backslash)

    X(Control,        control_l)
    A(A,              a)
    A(S,              s)
    A(D,              d)
    A(F,              f)
    A(G,              g)
    A(H,              h)
    A(J,              j)
    A(K,              k)
    A(L,              l)
    A(Semicolon,      semicolon)
    A(Quote,          apostrophe)
    X(Return,         return)

    X(Shift,          shift_l)
    A(Z,              z)
    A(X,              x)
    A(C,              c)
    A(V,              v)
    A(B,              b)
    A(N,              n)
    A(M,              m)
    A(Comma,          comma)
    A(Period,         period)
    A(Slash,          slash)
    X(RightShift,     shift_r)

    X(CapsLock,       caps_lock)
    X(Option,         alt_l)
    X(Space,          space)
    X(RightOption,    alt_r)
    X(RightControl,   control_r)

    X(F13,            print)
    X(F14,            scroll_lock)
    X(F15,            break)

    X(Help,           insert)
    X(Home,           home)
    X(PageUp,         page_up)
    X(ForwardDelete,  delete)
    X(End,            copy)
    X(PageDown,       page_down)

    X(UpArrow,        up)
    X(LeftArrow,      left)
    X(DownArrow,      down)
    X(RightArrow,     right)

    A(KeypadClear,    num_lock)
    A(KeypadDivide,   kp_slash)
    A(KeypadMultiply, kp_star)
    A(KeypadEquals,   kp_hash)
    A(Keypad7,        kp_7)
    A(Keypad8,        kp_8)
    A(Keypad9,        kp_9)
    A(KeypadMinus,    kp_minus)
    A(Keypad4,        kp_4)
    A(Keypad5,        kp_5)
    A(Keypad6,        kp_6)
    A(KeypadPlus,     kp_plus)
    A(Keypad1,        kp_1)
    A(Keypad2,        kp_2)
    A(Keypad3,        kp_3)
    A(Keypad0,        kp_0)
    A(KeypadDecimal,  kp_decimal)
    A(KeypadEnter,    kp_enter)

    { -1, 0 }
};
#undef X
#undef A
