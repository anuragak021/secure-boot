#include "boot.h"

#include <stdint.h>

#include "board.h"
#include "bytes.h"
#include "flash_layout.h"
#include "stm32h7xx.h"

#define NVIC_REG_COUNT 8u /* 8 x 32 = 256 interrupt lines, covers the H753's 150 */
#define ALL_BITS 0xFFFFFFFFu

typedef void (*app_entry_t)(void);

void boot_jump_to_app(void)
{
    const uint32_t vt = BL_APP_PAYLOAD_ADDR;
    /* The slot is memory mapped; word 0 is the initial stack pointer, word 1 the reset handler.
     * The integer to pointer casts are a deliberate MISRA deviation (documented in NOTES.md). */
    const uint32_t app_sp = *(const volatile uint32_t *)vt;
    const uint32_t app_reset = *(const volatile uint32_t *)(vt + 4u);

    /* 1. No interrupt may fire while the system is half torn down. The app re-enables them. */
    __disable_irq();

    /* 2. Stop the tick and put every peripheral we used back to its reset state, so the app
     *    sees what it would see after a real reset (no stray UART RX interrupt etc.). */
    board_deinit();

    /* 3. Clear all NVIC enable and pending bits. */
    for (uint32_t i = 0u; i < NVIC_REG_COUNT; i++) {
        NVIC->ICER[i] = ALL_BITS;
        NVIC->ICPR[i] = ALL_BITS;
    }

    /* 4. The bootloader never enables the caches, but if that changes, the app must not see
     *    stale lines or lose dirty ones: clean+disable D-cache, invalidate+disable I-cache. */
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_CleanDCache();
        SCB_DisableDCache();
    }
    if ((SCB->CCR & SCB_CCR_IC_Msk) != 0u) {
        SCB_DisableICache();
    }

    /* 5. Point the vector table at the app, load its stack pointer, then branch. The barriers
     *    make sure VTOR and MSP writes complete before the first app instruction. */
    SCB->VTOR = vt;
    __DSB();
    __ISB();
    __set_MSP(app_sp);
    ((app_entry_t)app_reset)();
    for (;;) {
        /* not reached: the app never returns; keeps the noreturn contract if it did */
    }
}
