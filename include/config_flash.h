#ifndef _CONFIG_FLASH_H_
#define _CONFIG_FLASH_H_

#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "spi_flash.h"

#define MAGIC_NUMBER    0x01200580

typedef struct
{
    // To check if the structure is initialized or not in flash
    uint32_t    magic_number;

    // Length of the structure, since this is a evolving library, the variant may change
    // hence used for verification
    uint16_t     length;

    /* Below variables are specific to my code */
    uint8_t     ssid[32];       // SSID of the AP to connect to
    uint8_t     password[64];   // Password of the network
    uint8_t     auto_connect;   // Should we auto connect

    uint8_t     locked;		// Should we allow for config changes
    ip_addr_t	ip_addr;	// Address of the slip interface

    uint16_t	clock_speed;	// Freq of the CPU
} sysconfig_t, *sysconfig_p;

int config_load(int version, sysconfig_p config);
void config_save(int version, sysconfig_p config);

#endif
