#ifndef EXTNROM_H
#define EXTNROM_H

uint32_t extnrom_calculate_size(const char* dir, uint32_t *entry_count);

void extnrom_load(const char* dir, uint32_t size, uint32_t entry_count, void *address);

#endif /* EXTNROM_H */
