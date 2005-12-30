/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef I2C_HEADER
#define I2C_HEADER

/*------------------------------------------------------------------------------*/
/* Called when the I2C clock changes state                                      */
void I2C_Update(ARMul_State *state);

/* ------------------------------------------------------------------------- */
void I2C_Init(ARMul_State *state);

#endif
