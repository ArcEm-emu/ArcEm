
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
#define VK_RETURN	(36)
#define VK_SHIFT	(56)
#define VK_CONTROL	(59)
#define VK_MENU		(-1)
#define VK_PAUSE	(-1)
#define VK_PAGEUP	(116)
#define VK_PAGEDOWN	(121)
#define VK_CAPITAL	(57)
#define VK_ESCAPE	(53)
#define VK_SPACE	(49)
#define VK_PRIOR	(33)
#define VK_ALT		(58)
#define VK_COMMAND	(55)
#define VK_NEXT		(34)
#define VK_END		(119)
#define VK_HOME		(115)
#define VK_LEFT		(123)
#define VK_UP		(126)
#define VK_RIGHT 	(124)
#define VK_DOWN		(125)
#define VK_SELECT	(-1)
#define VK_PRINT	(-1)
#define VK_EXECUTE	(76)
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
#define VK_RSHIFT	(60) 
#define VK_RCONTROL	(-1)
#define VK_RMENU	(-1)

#define VK_FUNCTION	(63)

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
