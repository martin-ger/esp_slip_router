#include "user_interface.h"
#include "config_flash.h"


/*     From the document 99A-SDK-Espressif IOT Flash RW Operation_v0.2      *
 * -------------------------------------------------------------------------*
 * Flash is erased sector by sector, which means it has to erase 4Kbytes one
 * time at least. When you want to change some data in flash, you have to
 * erase the whole sector, and then write it back with the new data.
 *--------------------------------------------------------------------------*/
void config_load_default(sysconfig_p config)
{
    os_memset(config, 0, sizeof(sysconfig_t));
    os_printf("Loading default configuration\r\n");
    config->magic_number                = MAGIC_NUMBER;
    config->length                      = sizeof(sysconfig_t);
    os_sprintf(config->ssid,"%s",       WIFI_SSID);
    os_sprintf(config->password,"%s",   WIFI_PASSWORD);
    config->auto_connect                = 1;

    config->use_ap			= false;
    os_sprintf(config->ap_ssid,"%s",    WIFI_AP_SSID);
    os_sprintf(config->ap_password,"%s",WIFI_AP_PASSWORD);
    config->ap_channel			= 1;
    config->ap_open			= 1;
    config->ap_on			= 1;
    config->ssid_hidden			= 0;
    config->max_clients			= MAX_CLIENTS;
    IP4_ADDR(&config->ap_dns, 192, 168, 240, 2);

    config->locked			= 0;
    IP4_ADDR(&config->ip_addr, 192, 168, 240, 1);
    IP4_ADDR(&config->ip_addr_peer, 192, 168, 240, 2);
    config->clock_speed			= 160;
    config->bit_rate                    = 115200;
}

int config_load(sysconfig_p config)
{
    if (config == NULL) return -1;
    uint16_t base_address = FLASH_BLOCK_NO;

    spi_flash_read(base_address* SPI_FLASH_SEC_SIZE, &config->magic_number, 4);

    if((config->magic_number != MAGIC_NUMBER))
    {
        os_printf("\r\nNo config found, saving default in flash\r\n");
        config_load_default(config);
        config_save(config);
        return -1;
    }

    os_printf("\r\nConfig found and loaded\r\n");
    spi_flash_read(base_address * SPI_FLASH_SEC_SIZE, (uint32 *) config, sizeof(sysconfig_t));
    if (config->length != sizeof(sysconfig_t))
    {
        os_printf("Length Mismatch, probably old version of config, loading defaults\r\n");
        config_load_default(config);
        config_save(config);
	return -1;
    }
    return 0;
}

void config_save(sysconfig_p config)
{
    uint16_t base_address = FLASH_BLOCK_NO;
    os_printf("Saving configuration\r\n");
    spi_flash_erase_sector(base_address);
    spi_flash_write(base_address * SPI_FLASH_SEC_SIZE, (uint32 *)config, sizeof(sysconfig_t));
}

void ICACHE_FLASH_ATTR blob_save(uint8_t blob_no, uint32_t *data, uint16_t len)
{
    uint16_t base_address = FLASH_BLOCK_NO + 1 + blob_no;
    spi_flash_erase_sector(base_address);
    spi_flash_write(base_address * SPI_FLASH_SEC_SIZE, data, len);
}

void ICACHE_FLASH_ATTR blob_load(uint8_t blob_no, uint32_t *data, uint16_t len)
{
    uint16_t base_address = FLASH_BLOCK_NO + 1 + blob_no;
    spi_flash_read(base_address * SPI_FLASH_SEC_SIZE, data, len);
}

void ICACHE_FLASH_ATTR blob_zero(uint8_t blob_no, uint16_t len)
{
int i;
    uint8_t z[len];
    os_memset(z, 0,len);
    uint16_t base_address = FLASH_BLOCK_NO + 1 + blob_no;
    spi_flash_erase_sector(base_address);
    spi_flash_write(base_address * SPI_FLASH_SEC_SIZE, (uint32_t *)z, len);
}

const uint8_t esp_init_data_default[] = {
    "\x05\x08\x04\x02\x05\x05\x05\x02\x05\x00\x04\x05\x05\x04\x05\x05"
    "\x04\xFE\xFD\xFF\xF0\xF0\xF0\xE0\xE0\xE0\xE1\x0A\xFF\xFF\xF8\x00"
    "\xF8\xF8\x4E\x4A\x46\x40\x3C\x38\x00\x00\x01\x01\x02\x03\x04\x05"
    "\x01\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\xE1\x0A\x00\x00\x00\x00\x00\x00\x00\x00\x01\x93\x43\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\x00\x00\x00\x00"
    "\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"};

void user_rf_pre_init() {
  uint8_t esp_init_data_current[sizeof(esp_init_data_default)];

  enum flash_size_map size_map = system_get_flash_size_map();
  uint32 rf_cal_sec = 0, addr, i;
  //os_printf("\nUser preinit: ");
   switch (size_map) {
      case FLASH_SIZE_4M_MAP_256_256:
         rf_cal_sec = 128 - 5;     
         break;

      case FLASH_SIZE_8M_MAP_512_512:
         rf_cal_sec = 256 - 5;
         break;

      case FLASH_SIZE_16M_MAP_512_512:
      case FLASH_SIZE_16M_MAP_1024_1024:
         rf_cal_sec = 512 - 5;
         break;

      case FLASH_SIZE_32M_MAP_512_512:
      case FLASH_SIZE_32M_MAP_1024_1024:
         rf_cal_sec = 1024 - 5;
         break;

      default:
         rf_cal_sec = 0;
         break;
   }

  addr = ((rf_cal_sec) * SPI_FLASH_SEC_SIZE)+SPI_FLASH_SEC_SIZE;
  spi_flash_read(addr, (uint32_t *)esp_init_data_current, sizeof(esp_init_data_current));

  for (i=0; i<sizeof(esp_init_data_default); i++) {
    
    if (esp_init_data_current[i] != esp_init_data_default[i]) {     
      spi_flash_erase_sector(rf_cal_sec);
      spi_flash_erase_sector(rf_cal_sec+1);
      spi_flash_erase_sector(rf_cal_sec+2);
      addr = ((rf_cal_sec) * SPI_FLASH_SEC_SIZE)+SPI_FLASH_SEC_SIZE;
      os_printf("Storing rfcal init data @ address=0x%08X\n", addr);
      spi_flash_write(addr, (uint32 *)esp_init_data_default, sizeof(esp_init_data_default));
     
      break;
    }
/* else {
      os_printf("RF data[%u] is ok\n", i);
    }*/
  }
}
