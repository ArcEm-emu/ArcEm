#ifndef EXTNROM_H
#define EXTNROM_H

unsigned extnrom_calculate_size(unsigned *entry_count);

void extnrom_load(unsigned size, unsigned entry_count, void *address);

#endif /* EXTNROM_H */
