/*
 * Copyright (C) 2015-2020 Alibaba Group Holding Limited
 */

#include <stdio.h>
#include "amp_hal_spi.h"

int32_t amp_hal_spi_init(spi_dev_t *spi)
{
    return 0;
}

int32_t amp_hal_spi_send(spi_dev_t *spi, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    return 0;
}

int32_t amp_hal_spi_recv(spi_dev_t *spi, uint8_t *data, uint16_t size, uint32_t timeout)
{
    return 0;
}

int32_t amp_hal_spi_send_recv(spi_dev_t *spi, uint8_t *tx_data, uint8_t *rx_data,
                          uint16_t size, uint32_t timeout)
{
    return 0;
}

int32_t amp_hal_spi_finalize(spi_dev_t *spi)
{
    return 0;
}


