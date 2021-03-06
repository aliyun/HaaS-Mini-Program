/*
 * Copyright (C) 2015-2020 Alibaba Group Holding Limited
 */

#ifndef AMP_HAL_FLASH_H
#define AMP_HAL_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Flash partition id defines
 */
typedef enum {
    AMP_PARTITION_ERROR = -1,
    AMP_PARTITION_BOOTLOADER = 0,
    AMP_PARTITION_APPLICATION,
    AMP_PARTITION_ATE,
    AMP_PARTITION_OTA_TEMP,
    AMP_PARTITION_RF_FIRMWARE,
    AMP_PARTITION_PARAMER1,
    AMP_PARTITION_PARAMETER_2,
    AMP_PARTITION_PARAMETER_3,
    AMP_PARTITION_PARAMETER_4,
    AMP_PARTITION_MAX,
    AMP_PARTITION_ID_KV /* kv partition */  
} amp_partition_id_t;

typedef enum {
    AMP_FLASH_EMBEDDED,
    AMP_FLASH_SPI,
    AMP_FLASH_QSPI,
    AMP_FLASH_MAX,
    AMP_FLASH_NONE,
} amp_hal_flash_t;

typedef struct {
    amp_hal_flash_t  partition_owner;
    const char       *partition_description;
    uint32_t         partition_start_addr;
    uint32_t         partition_length;
    uint32_t         partition_options;
} amp_hal_logic_partition_t;

/**
 * get flash area information
 *
 * @param[in]  id              The target flash logical partition which should be read
 * @param[in]  partition       Point to the flash area information
 *                             
 *
 * @return  0 : On success, Negative : If an error occurred with any step
 */
int32_t amp_hal_flash_info_get(amp_partition_id_t in_partition, amp_hal_logic_partition_t *partition);

/**
 * Read data from an area on a Flash to data buffer
 *
 * @param[in]  id              The target flash logical partition which should be read
 * @param[in]  offset          Point to the start address that the data is read, and
 *                             point to the last unread address after this function is
 *                             returned, so you can call this function serval times without
 *                             update this start address.
 * @param[in]  buffer          Point to the data buffer that stores the data read from flash
 * @param[in]  buffer_len      The length of the buffer
 *
 * @return  0 : On success, Negative : If an error occurred with any step
 */
int32_t amp_hal_flash_read(amp_partition_id_t id, uint32_t *offset, void *buffer, uint32_t buffer_len);


/**
 * Write data to an area on a flash logical partition without erase
 *
 * @param[in]  id              The target flash logical partition which should be read which should be written
 * @param[in]  offset          Point to the start address that the data is written to, and
 *                             point to the last unwritten address after this function is
 *                             returned, so you can call this function serval times without
 *                             update this start address.
 * @param[in]  buffer          point to the data buffer that will be written to flash
 * @param[in]  buffer_len      The length of the buffer
 *
 * @return  0 : On success, Negative : If an error occurred with any step
 */
int32_t amp_hal_flash_write(amp_partition_id_t id, uint32_t *offset, const void *buffer, uint32_t buffer_len);


/**
 * Erase an area on a Flash logical partition
 *
 * @note  Erase on an address will erase all data on a sector that the
 *        address is belonged to, this function does not save data that
 *        beyond the address area but in the affected sector, the data
 *        will be lost.
 *
 * @param[in]  id            The target flash logical partition which should be erased
 * @param[in]  offset        Start address of the erased flash area
 * @param[in]  size          Size of the erased flash area
 *
 * @return  0 : On success, Negative : If an error occurred with any step
 */
int32_t amp_hal_flash_erase(amp_partition_id_t id, uint32_t offset, uint32_t size);



#ifdef __cplusplus
}
#endif

#endif /* AMP_HAL_FLASH_H */

