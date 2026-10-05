#ifndef SOFT3D_BOOT_H
#define SOFT3D_BOOT_H

#include <stdbool.h>

#define SOFT3D_DISPLAY_BUILD "ENGINE LAB V4"

/* After GPIO, DMA clock and SPI1 init; works before the RTOS is started. */
bool Soft3D_BootInit(void);
/* The display owner must retire any DMA transfer before drawing this page. */
bool Soft3D_BootStatus(const char *phase, const char *detail, const char *footer);

#endif
