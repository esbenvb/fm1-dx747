/* Host stand-ins for the AC791N registers used by app/board/fm1_input.h.
 * Include before panel.c / fm1_input.h; test_backlight.c drives the model. */
#ifndef HOST_HW_H
#define HOST_HW_H
#include <stddef.h>
#include <stdint.h>
#include "fm1_gpio.h" /* real port and offset constants; only FM1_PR is mocked */

/* GPIO: ports PA=0, PB=1, PC=2, PH=7, sixteen words each. */
#undef FM1_PR
static uint32_t host_gpio[16 * 8];
#define FM1_PR(port, offset) (host_gpio[16U * (port) + (offset) / 4U])

/* Scan SPI: the DMA done flag (bit 15) reads set unless host_dma_busy. */
static uint32_t host_scan[8], host_io_map;
static int host_dma_busy;
static inline volatile uint32_t *host_scan_reg(unsigned offset)
{
    if (offset == 0) {
        if (host_dma_busy) host_scan[0] &= ~(1u << 15);
        else host_scan[0] |= 1u << 15;
    }
    return &host_scan[offset / 4u];
}
#define FM1_SCAN_REG(offset) (*host_scan_reg(offset))
#define FM1_IO_MAP (host_io_map)
#define FM1_SCAN_BARRIER() __asm__ volatile("" ::: "memory")

/* The settle wait runs the scan interrupt (see host_poll in the test). */
static void host_poll(void);
#define FM1_SETTLE_POLL() host_poll()
#endif
