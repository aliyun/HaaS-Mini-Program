/*
 * Copyright (C) 2015-2020 Alibaba Group Holding Limited
 */

#ifndef AMP_HAL_WDG_H
#define AMP_HAL_WDG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    uint32_t timeout; /* Watchdag timeout */
} wdg_config_t;

typedef struct {
    uint8_t       port;   /* wdg port */
    wdg_config_t  config; /* wdg config */
    void         *priv;   /* priv data */
} wdg_dev_t;

/**
 * This function will initialize the on board CPU hardware watch dog
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success, EIO : if an error occurred with any step
 */
int32_t amp_hal_wdg_init(wdg_dev_t *wdg);

/**
 * Reload watchdog counter.
 *
 * @param[in]  wdg  the watch dog device
 */
void amp_hal_wdg_reload(wdg_dev_t *wdg);

/**
 * This function performs any platform-specific cleanup needed for hardware watch dog.
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success, EIO : if an error occurred with any step
 */
int32_t amp_hal_wdg_finalize(wdg_dev_t *wdg);

#ifdef __cplusplus
}
#endif

#endif /* AMP_HAL_WDG_H */
