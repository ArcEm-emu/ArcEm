/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef I2C_HEADER
#define I2C_HEADER

typedef enum {
  I2CChipState_Idle=0,
  I2CChipState_GotStart, /* Got a special condition */
  I2CChipState_WaitingForSlaveAddr,
  I2CChipState_AckAfterSlaveAddr,
  I2CChipState_GettingWordAddr,
  I2CChipState_AckAfterWordAddr,
  I2CChipState_TransmittingToMaster,
  I2CChipState_AckAfterTransmitData,
  I2CChipState_GettingData,
  I2CChipState_AckAfterReceiveData
} I2CState;

struct I2CStruct {
  unsigned char Data[256];
  int IAmTransmitter;
  int OldDataState;
  int OldClockState;
  I2CState state;
  int DataBuffer;
  int NumberOfBitsSoFar;
  int LastrNw;
  unsigned char WordAddress; /* Note unsigned char - can never be outside Data bounds */
};

/*------------------------------------------------------------------------------*/
/* Called when the I2C clock changes state                                      */
void I2C_Update(ARMul_State *state);

/* ------------------------------------------------------------------------- */
void I2C_Init(ARMul_State *state);

#endif
