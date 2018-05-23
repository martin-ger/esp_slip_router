#ifndef _CONFIG_FLASH_H_
#define _CONFIG_FLASH_H_

#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "spi_flash.h"

#define MAGIC_NUMBER    0x01200583

typedef struct
{
    // To check if the structure is initialized, matching or not in flash
    uint32_t    magic_number;
    uint16_t    length;

    // Config parameters

    uint8_t     ssid[32];       // SSID of the AP to connect to
    uint8_t     password[64];   // Password of the network
    uint8_t     auto_connect;   // Should we auto connect

    uint8_t     use_ap;		   // Should we run as AP instead of STA
    uint8_t     ap_ssid[32];       // SSID of the own AP
    uint8_t     ap_password[64];   // Password of the own network
    uint8_t     ap_channel;        // Channel of the AP
    uint8_t     ap_open;           // Should we use no WPA?
    uint8_t	ap_on;		   // AP enabled?
    uint8_t	ssid_hidden;	   // Hidden SSID?
    uint8_t	max_clients;	   // Max number of STAs on the SoftAP

    uint8_t     locked;		// Should we allow for config changes
    ip_addr_t	ip_addr;	// Address of the slip interface
    ip_addr_t	ip_addr_peer;	// Address of the slip interface

    uint16_t	clock_speed;	// Freq of the CPU
    uint32_t    bit_rate;       // Bit rate of serial link
} sysconfig_t, *sysconfig_p;

int config_load(int version, sysconfig_p config);
void config_save(int version, sysconfig_p config);

#endif
