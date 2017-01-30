#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"

#include "lwip/ip.h"
#include "lwip/netif.h"
#include "netif/slipif.h"
#include "driver/uart.h"
#include "driver/softuart.h"

#define user_procTaskPrio        0
#define user_procTaskQueueLen    10
os_event_t    user_procTaskQueue[user_procTaskQueueLen];

Softuart softuart;

struct netif sl_netif;

//-------------------------------------------------------------------------------------------------
static void ICACHE_FLASH_ATTR  loop(os_event_t *events)
{
    switch(events->sig)
    {
    case UART0_SIGNAL:
	// We get this every time the UART0 receive buffer has been tranfered to the lwip stack
	// Check, whether it is a complete IP packet
	slipif_process_rxqueue(&sl_netif);

	break;

    default:
	// Intentionally ignoring other signals
	os_printf("Spurious Signal received\n");
	break;
    }
}


LOCAL void
softuart_write_char(char c)
{
    if (c == '\n') {
	Softuart_Putchar(&softuart, '\r');
	Softuart_Putchar(&softuart, '\n');
    } else if (c == '\r') {
    } else {
	Softuart_Putchar(&softuart, c);
    }
}

LOCAL void
write_to_pbuf(char c)
{
    slipif_received_byte(&sl_netif, c);
}

//-------------------------------------------------------------------------------------------------
//Init function
void ICACHE_FLASH_ATTR  user_init()
{
struct station_config wifi_config;
ip_addr_t ipaddr;
ip_addr_t netmask;
ip_addr_t gw;

// This interface number 2 is just to avoid any confusion with the WiFi-Interfaces (0 and 1)
// Should be different in the name anyway - just to be sure
// Matches the number in sio_open()
char int_no = 2; 

    // Initialize software uart
    Softuart_SetPinRx(&softuart,14);	
    Softuart_SetPinTx(&softuart,12);
    Softuart_Init(&softuart,19200);

    // os_printf to softuart
    os_install_putc1(softuart_write_char);

    // The callback fn that unloads one char from the receive buffer of UART0
    // We write it directly into the lwip pbufs
    uart0_unload_fn = write_to_pbuf;

    // Set station configuration
    os_printf("Starting STA\n");
    wifi_set_opmode(STATION_MODE);
    os_memset(&wifi_config,0,sizeof(wifi_config));
    os_strcpy(wifi_config.ssid, WIFI_SSID, os_strlen(WIFI_SSID));
    os_strcpy(wifi_config.password, WIFI_PASSWORD, WIFI_PASSWORD);
    wifi_station_set_config(&wifi_config);
    wifi_station_connect();

    // Configure the SLIP interface
    os_printf("Starting SLIP\n");
    IP4_ADDR(&ipaddr, 192, 168, 240, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 240, 2);
    netif_add (&sl_netif, &ipaddr, &netmask, &gw, &int_no, slipif_init, ip_input);
    netif_set_up(&sl_netif);

    // enable NAT on it for outgoing traffic via the WiFi
    ip_napt_enable(ipaddr.addr, 1);

    //Start our user task
    system_os_task(loop, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
}
