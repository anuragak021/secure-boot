/**
 * @file boot.h
 * @brief Jump from the bootloader into the application.
 */
#ifndef BOOT_H
#define BOOT_H

/**
 * @brief Hand control to the application whose vector table is at the app payload address.
 *
 * Steps (each one explained in boot.c): disable interrupts, stop SysTick and reset the
 * peripherals we used, clear NVIC enables/pendings, clean+disable caches if they are on,
 * relocate VTOR, load MSP, branch to the reset handler.
 *
 * @note Does not return. The caller must have validated the slot with boot_check_slot().
 */
void boot_jump_to_app(void) __attribute__((noreturn));

#endif /* BOOT_H */
