/* Virtual Key codes */

#define VK_LBUTTON	(1)
#define VK_RBUTTON	(2)
#define VK_CANCEL	(3)
#define VK_MBUTTON	(4)
#define VK_BACK	(8)
#define VK_TAB	(9)
#define VK_CLEAR	(12)
#define VK_RETURN	(13)
#define VK_SHIFT	(16)
#define VK_CONTROL	(17)
#define VK_MENU	(18)
#define VK_PAUSE	(19)
#define VK_CAPITAL	(20)
#define VK_ESCAPE	(27)
#define VK_SPACE	(32)
#define VK_PRIOR	(33)
#define VK_NEXT	(34)
#define VK_END	(35)
#define VK_HOME	(36)
#define VK_LEFT	(37)
#define VK_UP	(38)
#define VK_RIGHT	(39)
#define VK_DOWN	(40)
#define VK_SELECT	(41)
#define VK_PRINT	(42)
#define VK_EXECUTE	(43)
#define VK_SNAPSHOT	(44)
#define VK_INSERT	(45)
#define VK_DELETE	(46)
#define VK_HELP	(47)
#define VK_0	(48)
#define VK_1	(49)
#define VK_2	(50)
#define VK_3	(51)
#define VK_4	(52)
#define VK_5	(53)
#define VK_6	(54)
#define VK_7	(55)
#define VK_8	(56)
#define VK_9	(57)
#define VK_A	(65)
#define VK_B	(66)
#define VK_C	(67)
#define VK_D	(68)
#define VK_E	(69)
#define VK_F	(70)
#define VK_G	(71)
#define VK_H	(72)
#define VK_I	(73)
#define VK_J	(74)
#define VK_K	(75)
#define VK_L	(76)
#define VK_M	(77)
#define VK_N	(78)
#define VK_O	(79)
#define VK_P	(80)
#define VK_Q	(81)
#define VK_R	(82)
#define VK_S	(83)
#define VK_T	(84)
#define VK_U	(85)
#define VK_V	(86)
#define VK_W	(87)
#define VK_X	(88)
#define VK_Y	(89)
#define VK_Z	(90)
#define VK_LWIN	(91)
#define VK_RWIN	(92)
#define VK_APPS	(93)
#define VK_NUMPAD0	(96)
#define VK_NUMPAD1	(97)
#define VK_NUMPAD2	(98)
#define VK_NUMPAD3	(99)
#define VK_NUMPAD4	(100)
#define VK_NUMPAD5	(101)
#define VK_NUMPAD6	(102)
#define VK_NUMPAD7	(103)
#define VK_NUMPAD8	(104)
#define VK_NUMPAD9	(105)
#define VK_MULTIPLY	(106)
#define VK_ADD	(107)
#define VK_SEPARATOR	(108)
#define VK_SUBTRACT	(109)
#define VK_DECIMAL	(110)
#define VK_DIVIDE	(111)
#define VK_F1	(112)
#define VK_F2	(113)
#define VK_F3	(114)
#define VK_F4	(115)
#define VK_F5	(116)
#define VK_F6	(117)
#define VK_F7	(118)
#define VK_F8	(119)
#define VK_F9	(120)
#define VK_F10	(121)
#define VK_F11	(122)
#define VK_F12	(123)
#define VK_F13	(124)
#define VK_F14	(125)
#define VK_F15	(126)
#define VK_F16	(127)
#define VK_F17	(128)
#define VK_F18	(129)
#define VK_F19	(130)
#define VK_F20	(131)
#define VK_F21	(132)
#define VK_F22	(133)
#define VK_F23	(134)
#define VK_F24	(135)

/* GetAsyncKeyState */
#define VK_NUMLOCK	(144)
#define VK_SCROLL	(145)
#define VK_LSHIFT	(160)
#define VK_LCONTROL	(162)
#define VK_LMENU	(164)
#define VK_RSHIFT	(161)
#define VK_RCONTROL	(163)
#define VK_RMENU	(165)

/* ImmGetVirtualKey */
#define VK_PROCESSKEY	(229)

struct ArcKeyTrans {
  int sym;
  int row,col;
};

struct ArcKeyTrans transTable[]={
  {VK_ESCAPE,0,0}, {VK_F1,0,1}, {VK_F2,0,2}, {VK_F3,0,3}, {VK_F4,0,4}, {VK_F5,0,5},
  {VK_F6,0,6}, {VK_F7,0,7},{VK_F8,0,8}, {VK_F9,0,9}, 
  {VK_F10,0,10},{VK_F11,0,11},{VK_F12,0,12},
  {VK_PRINT,0,13},{VK_SCROLL,0,14},{VK_PAUSE,0,15},
/*  {XK_Break,0,15}, */

  {0xC0,1,0},{VK_1,1,1},{VK_2,1,2},{VK_3,1,3},{VK_4,1,4},
  {VK_5,1,5},{VK_6,1,6},{VK_7,1,7},{VK_8,1,8},{VK_9,1,9},{VK_0,1,10},
  {0xBD,1,11},
  {0xBB,1,12},
/*  {XK_sterling,1,13},{XK_currency,1,13}, */
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

  {VK_P,3,0},{0xDB,3,1},
  {0xDD,3,2},
  {0xDC,3,3},
  {VK_DELETE,3,4},{VK_END,3,5},
#ifdef XK_Page_Down
  {XK_Page_Down,3,6},
#endif
  {VK_NUMPAD7,3,7},{VK_NUMPAD8,3,8},{VK_NUMPAD9,3,9},
  {VK_SUBTRACT,3,10},{VK_LCONTROL,3,11},
  {VK_A,3,12},{VK_S,3,13},{VK_D,3,14},{VK_F,3,15},

  {VK_G,4,0},{VK_H,4,1},{VK_J,4,2},{VK_K,4,3},
  {VK_L,4,4},{0xBA,4,5},
  {0xDE,4,6},
  {VK_RETURN,4,7},{VK_NUMPAD4,4,8},
  {VK_NUMPAD5,4,9},{VK_NUMPAD6,4,10},{VK_ADD,4,11},
  /* {VK_LSHIFT,4,12}, */ {VK_SHIFT,4,12},{VK_Z,4,14},{VK_X,4,15},

  {VK_C,5,0},{VK_V,5,1},{VK_B,5,2},{VK_N,5,3},{VK_M,5,4},
  {0xBC,5,5},{0xBE,5,6},
  {0xBF,5,7},
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
