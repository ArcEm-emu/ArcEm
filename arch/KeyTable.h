

#ifdef __riscos__
//#include "ROKeys.h"
#endif


struct ArcKeyTrans {
  KeySym sym;
  int row,col;
};


struct ArcKeyTrans transTable[]={
  {XK_Escape,0,0}, {XK_F1,0,1}, {XK_F2,0,2}, {XK_F3,0,3}, {XK_F4,0,4}, {XK_F5,0,5},
  {XK_F6,0,6}, {XK_F7,0,7},{XK_F8,0,8}, {XK_F9,0,9}, 
  {XK_F10,0,10},{XK_F11,0,11},{XK_F12,0,12},
  {XK_Print,0,13},{XK_Scroll_Lock,0,14},{XK_Pause,0,15}, {XK_Break,0,15},

  {XK_asciitilde,1,0},{XK_1,1,1},{XK_2,1,2},{XK_3,1,3},{XK_4,1,4},
  {XK_5,1,5},{XK_6,1,6},{XK_7,1,7},{XK_8,1,8},{XK_9,1,9},{XK_0,1,10},
  {XK_minus,1,11},{XK_underscore,1,11},
  {XK_equal,1,12},{XK_plus,1,12},
  {XK_sterling,1,13},{XK_currency,1,13},
  {XK_BackSpace,1,14},{XK_Insert,1,15},

  {XK_Home,2,0},
/* For some screwy reason these seem to be missing in X11R5 */
#ifdef XK_Page_Up  
  {XK_Page_Up,2,1},
#endif
  {XK_Num_Lock,2,2},
  {XK_KP_Divide,2,3},{XK_KP_Multiply,2,4},{XK_KP_F1,2,5}, /* X doesn't define
                                                             a # on the keypad -
                                                             so we use KP_F1 - but
                                                             most keypads don't have that either! */

  {XK_Tab,2,6},{XK_q,2,7},{XK_w,2,8},{XK_e,2,9},
  {XK_r,2,10},{XK_t,2,11},{XK_y,2,12},{XK_u,2,13},
  {XK_i,2,14},{XK_o,2,15},

  {XK_p,3,0},{XK_bracketleft,3,1},{XK_braceleft,3,1},
  {XK_bracketright,3,2},{XK_braceleft,3,2},
  {XK_backslash,3,3},{XK_bar,3,3},
  {XK_Delete,3,4},{XK_End,3,5},
#ifdef XK_Page_Down
  {XK_Page_Down,3,6},
#endif
  {XK_KP_7,3,7},{XK_KP_8,3,8},{XK_KP_9,3,9},
  {XK_KP_Subtract,3,10},{XK_Control_L,3,11},
  {XK_a,3,12},{XK_s,3,13},{XK_d,3,14},{XK_f,3,15},

  {XK_g,4,0},{XK_h,4,1},{XK_j,4,2},{XK_k,4,3},
  {XK_l,4,4},{XK_colon,4,5},{XK_semicolon,4,5},
  {XK_apostrophe,4,6},{XK_quotedbl,4,6},
  {XK_Return,4,7},{XK_KP_4,4,8},
  {XK_KP_5,4,9},{XK_KP_6,4,10},{XK_KP_Add,4,11},
  {XK_Shift_L,4,12},{XK_z,4,14},{XK_x,4,15},


  {XK_c,5,0},{XK_v,5,1},{XK_b,5,2},{XK_n,5,3},{XK_m,5,4},
  {XK_comma,5,5},{XK_less,5,5},{XK_period,5,6},
  {XK_greater,5,6},{XK_slash,5,7},{XK_question,5,7},
  {XK_Shift_R,5,8},{XK_Up,5,9},{XK_KP_1,5,10},
  {XK_KP_2,5,11},{XK_KP_3,5,12},{XK_Caps_Lock,5,13},
  {XK_Alt_L,5,14},{XK_space,5,15},

  {XK_Alt_R,6,0},{XK_Control_R,6,1},
  {XK_Left,6,2},{XK_Down,6,3},{XK_Right,6,4},
  {XK_KP_0,6,5},{XK_KP_Decimal,6,6},{XK_KP_Enter,6,7},

  {0,-1,-1} /* Termination of list */
};
