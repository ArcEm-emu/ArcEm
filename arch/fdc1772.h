/* Emulation of 1772 fdc - (C) David Alan Gilbert 1995 */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef FDC1772
#define FDC1772

#include "../armdefs.h"
#include "../armopts.h"

typedef struct {
    /* User-visible name. */
    const char *name;
    int bytes_per_sector;
    int sectors_per_track;
    int sector_base;
    int num_cyl;
    int num_sides;
} floppy_format;

typedef struct {
    /* To access the disc image.  NULL if disc ejected. */
    FILE *fp;
    /* Based on whether read/write access to the disc image was
     * obtained. */
    int write_protected;
    /* Points to an element of avail_format. */
    floppy_format *form;
} floppy_drive;

struct FDCStruct{
  unsigned char LastCommand;
  int Direction; /* -1 or 1 */
  unsigned char StatusReg;
  unsigned char Track;
  unsigned char Sector;
  unsigned char Sector_ReadAddr; /* Used to hold the next sector to be found! */
  unsigned char Data;
  int BytesToGo;
  int DelayCount;
  int DelayLatch;
  int CurrentDisc;
#ifdef MACOSX
  char* driveFiles[4];  // In MAE, we use *real* filenames for disks
#endif
    floppy_drive drive[4];
    /* The bottom four bits of leds holds their current state.  If the
     * bit is set the LED should be emitting. */
    void (*leds_changed)(unsigned int leds);
};

ARMword FDC_Read(ARMul_State *state, ARMword offset);

ARMword FDC_Write(ARMul_State *state, ARMword offset, ARMword data, int bNw);

void FDC_LatchAChange(ARMul_State *state);
void FDC_LatchBChange(ARMul_State *state);

void FDC_Init(ARMul_State *state);

/* Deprecated.  See fdc_insert_floppy() and fdc_eject_floppy(). */
void FDC_ReOpen(ARMul_State *state,int drive);

unsigned FDC_Regular(ARMul_State *state);

/* Associate disc image with drive.  Drive must be empty.  Return static
 * error message, if not NULL. */
const char *fdc_insert_floppy(int drive, const char *image);

/* Close and forget about the disc image associated with drive.  Disc
 * must be inserted.  Return static error message, if not NULL. */
const char *fdc_eject_floppy(int drive);

#define FDC (PRIVD->FDCData)

#endif
