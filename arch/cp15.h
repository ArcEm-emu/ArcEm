/* (c) Peter Howkins 2006 - see Readme file for copying info */
/*
  Coprocessor 15 on an ARM processor is reserved by ARM to be the
  CPU control processor. Introduced with the ARM3 it allowed control
  of cache. Later ARMs use it to control the MMU as well.

  To succesfully emulate an ARM3 or later you need coprocessor 15
  emulation.

  ARM3 cp15 description
  http://www.home.marutan.net/arcemdocs/arm3.txt

  ARM3/ARM610/ARM710/SA110 cp15 description
  http://www.heyrick.co.uk/assembler/coprocmnd.html

  MCR and MRC instruction formats
  http://www.pinknoise.demon.co.uk/ARMinstrs/ARMinstrs.html#CoproOp

 */

 
/**
 * ARM3_CoProAttach
 *
 * Attach the ARM3 cpu control coprocessor.
 *
 * @param hState Emulator state
 */
void ARM3_CoProAttach(ARMul_State *state);
