#
# $Id$
#

AS = armas
LD = armld
OBJCOPY = armobjcopy




all: hostfs,ffa hostfsfiler,ffa

%,ffa: %.elf
	$(OBJCOPY) -O binary $< $@

%.elf: %.o
	$(LD) --section-start .text=0 -o $@ $<

%.o: %.s
	$(AS) -o $@ $<

clean:
	rm -f *.o *.elf *,ffa
