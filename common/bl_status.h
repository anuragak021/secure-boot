/**
 * @file bl_status.h
 * @brief Status codes shared by all bootloader modules and sent on the wire in NACK packets.
 */
#ifndef BL_STATUS_H
#define BL_STATUS_H

/**
 * @brief Module status. Values 1..10 are also the NACK error codes on the wire (u8).
 *        BL_OK is always 0. Values above 10 are local only and never sent in a NACK.
 */
typedef enum {
    BL_OK = 0,
    BL_ERR_BAD_CRC = 1,     /**< packet CRC32 mismatch */
    BL_ERR_BAD_LEN = 2,     /**< length field wrong or out of range */
    BL_ERR_BAD_SEQ = 3,     /**< sequence number is not the expected one */
    BL_ERR_BAD_OFFSET = 4,  /**< DATA offset not sequential or beyond the image */
    BL_ERR_BAD_HEADER = 5,  /**< image header failed validation */
    BL_ERR_FLASH_ERASE = 6, /**< flash erase failed */
    BL_ERR_FLASH_WRITE = 7, /**< flash program failed */
    BL_ERR_VERIFY_FAIL = 8, /**< image CRC32 differs from the header */
    BL_ERR_NOT_READY = 9,   /**< command not allowed in the current state */
    BL_ERR_BAD_CMD = 10,    /**< unknown command byte (addition to the documented enum) */
    BL_ERR_INVALID_ARG = 11,
    BL_ERR_COBS = 12,     /**< malformed COBS data */
    BL_ERR_OVERFLOW = 13, /**< buffer too small / frame too long */
    BL_ERR_TIMEOUT = 14,
    BL_ERR_TRANSPORT = 15,
    BL_ERR_BAD_VECTORS = 16 /**< app stack pointer / reset vector outside valid ranges */
} bl_status_t;

#endif /* BL_STATUS_H */
