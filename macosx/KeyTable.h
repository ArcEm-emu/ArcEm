
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

/* Virtual Key codes */

#define VK_LBUTTON	(-1)
#define VK_RBUTTON	(-1)
#define VK_CANCEL	(-1)
#define VK_MBUTTON	(-1)
#define VK_BACK		(-1)
#define VK_TAB		(48)
#define VK_CLEAR	(12)
#define VK_RETURN	(37)
#define VK_SHIFT	(56)
#define VK_CONTROL	(59)
#define VK_MENU		(-1)
#define VK_PAUSE	(-1)
#define VK_CAPITAL	(57)
#define VK_ESCAPE	(53)
#define VK_SPACE	(49)
#define VK_PRIOR	(33)
#define VK_NEXT		(34)
#define VK_END		(119)
#define VK_HOME		(115)
#define VK_LEFT		(123)
#define VK_UP		(126)
#define VK_RIGHT 	(124)
#define VK_DOWN		(125)
#define VK_SELECT	(-1)
#define VK_PRINT	(-1)
#define VK_EXECUTE	(36)
#define VK_SNAPSHOT	(-1)
#define VK_INSERT	(-1)
#define VK_DELETE	(51)
#define VK_SEMICOLON	(41)
#define VK_APOSTROPHY	(39)
#define VK_COMMA	(43)
#define VK_PERIOD	(47)
#define VK_SLASH	(44)
#define VK_BACKSLASH	(42)
#define VK_DASH		(27)
#define VK_TOPADD	(24)
#define VK_BRACKETLEFT	(33)
#define VK_BRACKETRIGHT	(30)
#define VK_BACKTICK	(50)
// VK_WEIRD is ¤/±
#define VK_WEIRD	(10) 
#define VK_HELP	(47)
#define VK_1	(18)
#define VK_2	(19)
#define VK_3	(20)
#define VK_4	(21)
#define VK_5	(23)
#define VK_6	(22)
#define VK_7	(26)
#define VK_8	(28)
#define VK_9	(25)
#define VK_0	(29)
#define VK_A	(0)
#define VK_B	(11)
#define VK_C	(8)
#define VK_D	(2)
#define VK_E	(14)
#define VK_F	(3)
#define VK_G	(5)
#define VK_H	(4)
#define VK_I	(34)
#define VK_J	(38)
#define VK_K	(40)
#define VK_L	(37)
#define VK_M	(46)
#define VK_N	(45)
#define VK_O	(31)
#define VK_P	(35)
#define VK_Q	(12)
#define VK_R	(15)
#define VK_S	(1)
#define VK_T	(17)
#define VK_U	(32)
#define VK_V	(9)
#define VK_W	(13)
#define VK_X	(7)
#define VK_Y	(16)
#define VK_Z	(6)
#define VK_LWIN		(-1)
#define VK_RWIN		(-1)
#define VK_APPS		(-1)
#define VK_NUMPAD0	(82)
#define VK_NUMPAD1	(83)
#define VK_NUMPAD2	(84)
#define VK_NUMPAD3	(85)
#define VK_NUMPAD4	(86)
#define VK_NUMPAD5	(87)
#define VK_NUMPAD6	(88)
#define VK_NUMPAD7	(89)
#define VK_NUMPAD8	(91)
#define VK_NUMPAD9	(92)
#define VK_MULTIPLY	(67)
#define VK_ADD		(69)
#define VK_SEPARATOR	(81)
#define VK_SUBTRACT	(78)
#define VK_DECIMAL	(65)
#define VK_DIVIDE	(75)
#define VK_F1	(122)
#define VK_F2	(120)
#define VK_F3	(99)
#define VK_F4	(118)
#define VK_F5	(96)
#define VK_F6	(97)
#define VK_F7	(98)
#define VK_F8	(100)
#define VK_F9	(101)
#define VK_F10	(109)
#define VK_F11	(103)
#define VK_F12	(111)

/* GetAsyncKeyState */
#define VK_NUMLOCK	(127)
#define VK_SCROLL	(-1)
#define VK_LSHIFT	(56)
#define VK_LCONTROL	(59)
#define VK_LMENU	(-1)
#define VK_RSHIFT	(-1) // L and R map to the same key in macosx
#define VK_RCONTROL	(-1)
#define VK_RMENU	(-1)

/* ImmGetVirtualKey */
#define VK_PROCESSKEY	(229)

struct ArcKeyTrans {
  int sym;
  int row,col;
};

// Used in the inverted key table
struct keyloc {
    int row, col;
};

struct ArcKeyTrans transTable[]={
  {VK_ESCAPE,0,0}, {VK_F1,0,1}, {VK_F2,0,2}, {VK_F3,0,3}, {VK_F4,0,4}, {VK_F5,0,5},
  {VK_F6,0,6}, {VK_F7,0,7},{VK_F8,0,8}, {VK_F9,0,9}, 
  {VK_F10,0,10},{VK_F11,0,11},{VK_F12,0,12},
  {VK_PRINT,0,13},{VK_SCROLL,0,14},{VK_PAUSE,0,15},
/*  {XK_Break,0,15}, */

  {VK_BACKTICK,1,0},{VK_1,1,1},{VK_2,1,2},{VK_3,1,3},{VK_4,1,4},
  {VK_5,1,5},{VK_6,1,6},{VK_7,1,7},{VK_8,1,8},{VK_9,1,9},{VK_0,1,10},
  {VK_DASH,1,11},
  {VK_TOPADD,1,12},
/*  {XK_sterling,1,13},{XK_currency,1,13}, */
    // I'm using the weird key for the pound key
    {VK_WEIRD, 1, 13}, {VK_WEIRD, 1, 13},
  {VK_BACK,1,14},{VK_INSERT,1,15},

  {VK_HOME,2,0},
/* For some screwy reason these seem to be missing in X11R5 */
#ifdef XK_Page_Up  
  {0x21,2,1},
#endif
  {VK_NUMLOCK,2,2},
  {VK_DIVIDE,2,3},{VK_MULTIPLY,2,4},{VK_SEPARATOR,2,5}, /* X doesn't define
                                                             a # on the keypad -
                                                             so we use KP_F1 - but
                                                             most keypads don't have that either! */

  {VK_TAB,2,6},{VK_Q,2,7},{VK_W,2,8},{VK_E,2,9},
  {VK_R,2,10},{VK_T,2,11},{VK_Y,2,12},{VK_U,2,13},
  {VK_I,2,14},{VK_O,2,15},

  {VK_P,3,0},{VK_BRACKETLEFT,3,1},
  {VK_BRACKETRIGHT,3,2},
  {VK_BACKSLASH,3,3},
  {VK_DELETE,3,4},{VK_END,3,5},
#ifdef XK_Page_Down
  {XK_Page_Down,3,6},
#endif
  {VK_NUMPAD7,3,7},{VK_NUMPAD8,3,8},{VK_NUMPAD9,3,9},
  {VK_SUBTRACT,3,10},{VK_LCONTROL,3,11},
  {VK_A,3,12},{VK_S,3,13},{VK_D,3,14},{VK_F,3,15},

  {VK_G,4,0},{VK_H,4,1},{VK_J,4,2},{VK_K,4,3},
  {VK_L,4,4},{VK_SEMICOLON,4,5},
  {VK_APOSTROPHY,4,6},
  {VK_RETURN,4,7},{VK_NUMPAD4,4,8},
  {VK_NUMPAD5,4,9},{VK_NUMPAD6,4,10},{VK_ADD,4,11},
  /* {VK_LSHIFT,4,12}, */ {VK_SHIFT,4,12},{VK_Z,4,14},{VK_X,4,15},


  {VK_C,5,0},{VK_V,5,1},{VK_B,5,2},{VK_N,5,3},{VK_M,5,4},
  {VK_COMMA,5,5},{VK_PERIOD,5,6},
  {VK_SLASH,5,7},
  {VK_RSHIFT,5,8},{VK_UP,5,9},{VK_NUMPAD1,5,10},
  {VK_NUMPAD2,5,11},{VK_NUMPAD3,5,12},{VK_CAPITAL,5,13},
/*  {XK_Alt_L,5,14}, */
  {VK_SPACE,5,15},

/*  {XK_Alt_R,6,0}, */
  {VK_RCONTROL,6,1},
  {VK_LEFT,6,2},{VK_DOWN,6,3},{VK_RIGHT,6,4},
  {VK_NUMPAD0,6,5},{VK_DECIMAL,6,6},{VK_EXECUTE,6,7},

  {0,-1,-1} /* Termination of list */
};
