#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/app/espconn.h"
#include "lwip/app/espconn_tcp.h"

#include "lwip/ip.h"
#include "lwip/ip_route.h"
#include "netif/slipif.h"
#include "driver/uart.h"
#include "driver/softuart.h"

#include "ringbuf.h"
#include "user_config.h"
#include "config_flash.h"

#define user_procTaskPrio        0
#define user_procTaskQueueLen    10
os_event_t    user_procTaskQueue[user_procTaskQueueLen];

Softuart softuart;

struct netif sl_netif;

// Holds the system wide configuration
sysconfig_t config;

static ringbuf_t console_rx_buffer, console_tx_buffer;

static ip_addr_t my_ip, dns_ip;
bool connected;

uint8_t remote_console_disconnect;

uint32_t g_bit_rate;

// Similar to strtok
int parse_str_into_tokens(char *str, char **tokens, int max_tokens)
{
char    *p;
int     token_count = 0;
bool    in_token = false;

   p = str;

   while (*p != 0) {
	if (*p <= ' ') {
	   if (in_token) {
		*p = 0;
		in_token = false;
	   }
	} else {
	   if (!in_token) {
		tokens[token_count++] = p;
		if (token_count == max_tokens)
		   return token_count;
		in_token = true;
	   }  
	}
	p++;
   }
   return token_count;
}


void console_send_response(struct espconn *pespconn)
{
    char payload[MAX_CON_SEND_SIZE+4];
    uint16_t len = ringbuf_bytes_used(console_tx_buffer);

    ringbuf_memcpy_from(payload, console_tx_buffer, len);
    os_memcpy(&payload[len], "CMD>", 4);

    if (pespconn != NULL)
	espconn_sent(pespconn, payload, len+4);
}


#ifdef ALLOW_SCANNING
struct espconn *scanconn;
void ICACHE_FLASH_ATTR scan_done(void *arg, STATUS status)
{
  uint8 ssid[33];
  char response[128];

  if (status == OK)
  {
    struct bss_info *bss_link = (struct bss_info *)arg;

    while (bss_link != NULL)
    {
      ringbuf_memcpy_into(console_tx_buffer, "\r", 1);

      os_memset(ssid, 0, 33);
      if (os_strlen(bss_link->ssid) <= 32)
      {
        os_memcpy(ssid, bss_link->ssid, os_strlen(bss_link->ssid));
      }
      else
      {
        os_memcpy(ssid, bss_link->ssid, 32);
      }
      os_sprintf(response, "(%d,\"%s\",%d,\""MACSTR"\",%d)\r\n",
                 bss_link->authmode, ssid, bss_link->rssi,
                 MAC2STR(bss_link->bssid),bss_link->channel);
      ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
      bss_link = bss_link->next.stqe_next;
    }
  }
  else
  {
     os_sprintf(response, "scan fail !!!\r\n");
     ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
  }
  system_os_post(0, SIG_CONSOLE_TX, (ETSParam) scanconn);
}
#endif


void ICACHE_FLASH_ATTR console_handle_command(struct espconn *pespconn)
{
    char cmd_line[256];
    char response[256];
    char *tokens[5];

    int bytes_count, nTokens, i, j;

    bytes_count = ringbuf_bytes_used(console_rx_buffer);
    ringbuf_memcpy_from(cmd_line, console_rx_buffer, bytes_count);

    for (i=j=0; i<bytes_count; i++) {
	if (cmd_line[i] != 8) {
	   cmd_line[j++] = cmd_line[i];
	} else {
	   if (j > 0) j--;
	}
    }
    cmd_line[j] = 0;

    cmd_line[bytes_count] = 0;

    nTokens = parse_str_into_tokens(cmd_line, tokens, 5);

    if (nTokens == 0) {
	char c = '\n';
	ringbuf_memcpy_into(console_tx_buffer, &c, 1);
	goto command_handled;
    }

    if (strcmp(tokens[0], "help") == 0)
    {
        os_sprintf(response, "show|\r\nset [ssid|password|auto_connect|addr|speed|bitrate] <val>\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        os_sprintf(response, "set [ap_ssid|ap_password|ap_open|use_ap|ssid_hidden|max_clients] <val>\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        os_sprintf(response, "quit|save|reset [factory]|lock|unlock <password>");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
#ifdef ALLOW_SCANNING
        os_sprintf(response, "|scan");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
#endif
	ringbuf_memcpy_into(console_tx_buffer, "\r\n", 2);
        goto command_handled;
    }

    if (strcmp(tokens[0], "show") == 0)
    {
      if (nTokens == 1) {
        os_sprintf(response, "SLIP: IP: " IPSTR "\r\n", IP2STR(&config.ip_addr));
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	if (config.use_ap) {
            os_sprintf(response, "AP:  SSID:%s %s PW:%s%s\r\n",
                   config.ap_ssid,
		   config.ssid_hidden?"[hidden]":"",
                   config.locked?"***":(char*)config.ap_password,
                   config.ap_open?" [open]":"");
	} else {
            os_sprintf(response, "STA: SSID: %s PW: %s [AutoConnect:%d] \r\n",
                   config.ssid,
                   config.locked?"***":(char*)config.password,
                   config.auto_connect);
            ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	    if (connected) {
	       os_sprintf(response, "External IP: " IPSTR "\r\nDNS server: " IPSTR "\r\n", IP2STR(&my_ip), IP2STR(&dns_ip));
	    } else {
	       os_sprintf(response, "Not connected to AP\r\n");
	    }
	}
	ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        os_sprintf(response, "Clock speed: %d\r\n", config.clock_speed);
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	os_sprintf(response, "Serial bit rate: %d\r\n", config.bit_rate);
	ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	goto command_handled;
      }
    }

    if (strcmp(tokens[0], "save") == 0)
    {
        config_save(0, &config);
        os_sprintf(response, "Config saved\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        goto command_handled;
    }
#ifdef ALLOW_SCANNING
    if (strcmp(tokens[0], "scan") == 0)
    {
        scanconn = pespconn;
        wifi_station_scan(NULL,scan_done);
        os_sprintf(response, "Scanning...\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        goto command_handled;
    }
#endif
    if (strcmp(tokens[0], "reset") == 0)
    {
	if (nTokens == 2 && strcmp(tokens[1], "factory") == 0) {
           config_load_default(&config);
           config_save(0, &config);
	}
        os_printf("Restarting ... \r\n");
	system_restart();
        goto command_handled;
    }

    if (strcmp(tokens[0], "quit") == 0)
    {
	remote_console_disconnect = 1;
        goto command_handled;
    }


    if (strcmp(tokens[0], "lock") == 0)
    {
	config.locked = 1;
	os_sprintf(response, "Config locked\r\n");
	ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        goto command_handled;
    }

    if (strcmp(tokens[0], "unlock") == 0)
    {
        if (nTokens != 2)
        {
            os_sprintf(response, "Invalid number of arguments\r\n");
            ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        }
        else if (strcmp(tokens[1],config.password) == 0) {
	    config.locked = 0;
	    os_sprintf(response, "Config unlocked\r\n");
            ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        } else {
	    os_sprintf(response, "Unlock failed. Invalid password\r\n");
	    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        }
        goto command_handled;
    }


    if (strcmp(tokens[0], "set") == 0)
    {
        if (config.locked)
        {
            os_sprintf(response, "Invalid set command. Config locked\r\n");
            ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
            goto command_handled;
        }

        /*
         * For set commands atleast 2 tokens "set" "parameter" "value" is needed
         * hence the check
         */
        if (nTokens < 3)
        {
            os_sprintf(response, "Invalid number of arguments\r\n");
            ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
            goto command_handled;
        }
        else
        {
            // atleast 3 tokens, proceed
            if (strcmp(tokens[1],"ssid") == 0)
            {
                os_sprintf(config.ssid, "%s", tokens[2]);
                os_sprintf(response, "SSID set\r\n");
                ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"password") == 0)
            {
                os_sprintf(config.password, "%s", tokens[2]);
                os_sprintf(response, "Password set\r\n");
                ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"auto_connect") == 0)
            {
                config.auto_connect = atoi(tokens[2]);
                os_sprintf(response, "Auto Connect set\r\n");
                ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_ssid") == 0)
            {
                os_sprintf(config.ap_ssid, "%s", tokens[2]);
                os_sprintf(response, "AP SSID set\r\n");
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_password") == 0)
            {
		if (os_strlen(tokens[2])<8) {
		    os_sprintf(response, "Password too short (min. 8)\r\n");
		} else {
                    os_sprintf(config.ap_password, "%s", tokens[2]);
		    config.ap_open = 0;
                    os_sprintf(response, "AP Password set\r\n");
		}
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_open") == 0)
            {
                config.ap_open = atoi(tokens[2]);
                os_sprintf(response, "Open Auth set\r\n");
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"use_ap") == 0)
            {
                config.use_ap = atoi(tokens[2]);
		if (config.use_ap)
                    os_sprintf(response, "Using AP interface\r\n");
		else
                    os_sprintf(response, "Using STA interface\r\n");
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"ap_channel") == 0)
            {
		uint8_t chan = atoi(tokens[2]);
		if (chan >= 1 && chan <= 13) {
		    config.ap_channel = chan;
            	    os_sprintf(response, "AP channel set to %d\r\n", config.ap_channel);
		} else {
		    os_sprintf(response, "Invalid channel (1-13)\r\n");
		}
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"ssid_hidden") == 0)
            {
                config.ssid_hidden = atoi(tokens[2]);
                os_sprintf(response, "Hidden SSID set\r\n");
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"max_clients") == 0)
            {
		if (atoi(tokens[2]) <= MAX_CLIENTS) {
		    config.max_clients = atoi(tokens[2]);
		    os_sprintf(response, "Max clients set\r\n");
		} else {
		    os_sprintf(response, "Invalid val (<= 8)\r\n");
		}
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

	    if (strcmp(tokens[1], "speed") == 0)
	    {
		uint16_t speed = atoi(tokens[2]);
		bool succ = system_update_cpu_freq(speed);
		if (succ) 
		    config.clock_speed = speed;
		os_sprintf(response, "Clock speed update %s\r\n",
		  succ?"successful":"failed");
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        	goto command_handled;
	    }

            if (strcmp(tokens[1],"addr") == 0)
            {
                config.ip_addr.addr = ipaddr_addr(tokens[2]);
                os_sprintf(response, "IP address set to %d.%d.%d.%d/24\r\n", 
			IP2STR(&config.ip_addr));
                ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }

            if (strcmp(tokens[1],"bitrate") == 0)
            {
                config.bit_rate = atoi(tokens[2]);
                os_sprintf(response, "Bitrate will be %d after save & reset.\r\n", config.bit_rate);
                ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
                goto command_handled;
            }
        }
    }

    /* Control comes here only if the tokens[0] command is not handled */
    os_sprintf(response, "\r\nInvalid Command\r\n");
    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));

command_handled:
    system_os_post(0, SIG_CONSOLE_TX, (ETSParam) pespconn);
    return;
}

#ifdef REMOTE_CONFIG
static void ICACHE_FLASH_ATTR tcp_client_recv_cb(void *arg,
                                                 char *data,
                                                 unsigned short length)
{
    struct espconn *pespconn = (struct espconn *)arg;
    int            index;
    uint8_t         ch;

    for (index=0; index <length; index++)
    {
        ch = *(data+index);
	ringbuf_memcpy_into(console_rx_buffer, &ch, 1);

        // If a complete commandline is received, then signal the main
        // task that command is available for processing
        if (ch == '\n')
            system_os_post(0, SIG_CONSOLE_RX, (ETSParam) arg);
    }

    *(data+length) = 0;
}


static void ICACHE_FLASH_ATTR tcp_client_discon_cb(void *arg)
{
    os_printf("tcp_client_discon_cb(): client disconnected\n");
    struct espconn *pespconn = (struct espconn *)arg;
}


/* Called when a client connects to the console server */
static void ICACHE_FLASH_ATTR tcp_client_connected_cb(void *arg)
{
    char payload[128];
    struct espconn *pespconn = (struct espconn *)arg;

    os_printf("tcp_client_connected_cb(): Client connected\r\n");

    //espconn_regist_sentcb(pespconn,     tcp_client_sent_cb);
    espconn_regist_disconcb(pespconn,   tcp_client_discon_cb);
    espconn_regist_recvcb(pespconn,     tcp_client_recv_cb);
    espconn_regist_time(pespconn,  300, 1);  // Specific to console only

    ringbuf_reset(console_rx_buffer);
    ringbuf_reset(console_tx_buffer);
    
    os_sprintf(payload, "CMD>");
    espconn_sent(pespconn, payload, os_strlen(payload));
}
#endif



//-------------------------------------------------------------------------------------------------

static void ICACHE_FLASH_ATTR user_procTask(os_event_t *events)
{
    switch(events->sig)
    {
    case SIG_START_SERVER:
	// Anything to do here, when the repeater has received its IP?
	break;

    case UART0_SIGNAL:
	// We get this every time the UART0 receive buffer has been tranfered to the lwip stack
	// Check, whether it is a complete IP packet
	slipif_process_rxqueue(&sl_netif);

	break;

    case SIG_CONSOLE_TX:
        {
            struct espconn *pespconn = (struct espconn *) events->par;
            console_send_response(pespconn);

	    if (pespconn != 0 && remote_console_disconnect) espconn_disconnect(pespconn);
	    remote_console_disconnect = 0;
        }
        break;

    case SIG_CONSOLE_RX:
        {
            struct espconn *pespconn = (struct espconn *) events->par;
            console_handle_command(pespconn);
        }
        break;

    default:
	// Intentionally ignoring other signals
	os_printf("Spurious Signal received\n");
	break;
    }
}

/* Callback called when the connection state of the module with an Access Point changes */
void wifi_handle_event_cb(System_Event_t *evt)
{

    //os_printf("wifi_handle_event_cb: ");
    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        os_printf("connect to ssid %s, channel %d\n", evt->event_info.connected.ssid, evt->event_info.connected.channel);
        break;

    case EVENT_STAMODE_DISCONNECTED:
        os_printf("disconnect from ssid %s, reason %d\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
	connected = false;
        break;

    case EVENT_STAMODE_AUTHMODE_CHANGE:
        os_printf("mode: %d -> %d\n", evt->event_info.auth_change.old_mode, evt->event_info.auth_change.new_mode);
        break;

    case EVENT_STAMODE_GOT_IP:
	dns_ip = dns_getserver(0);

        os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR ",dns:" IPSTR "\n", IP2STR(&evt->event_info.got_ip.ip), IP2STR(&evt->event_info.got_ip.mask), IP2STR(&evt->event_info.got_ip.gw), IP2STR(&dns_ip));

	my_ip = evt->event_info.got_ip.ip;
	connected = true;

        // Post a Server Start message as the IP has been acquired to Task with priority 0
	system_os_post(user_procTaskPrio, SIG_START_SERVER, 0 );
        break;

    default:
        break;
    }
}

void ICACHE_FLASH_ATTR user_set_station_config(void)
{
    struct station_config stationConf;
    char hostname[40];

    /* Setup AP credentials */
    stationConf.bssid_set = 0;
    os_sprintf(stationConf.ssid, "%s", config.ssid);
    os_sprintf(stationConf.password, "%s", config.password);
    wifi_station_set_config(&stationConf);

    wifi_set_event_handler_cb(wifi_handle_event_cb);

    wifi_station_set_auto_connect(config.auto_connect != 0);
}

void ICACHE_FLASH_ATTR user_set_softap_wifi_config(void)
{
struct softap_config apConfig;

   wifi_softap_get_config(&apConfig); // Get config first.
    
   os_memset(apConfig.ssid, 0, 32);
   os_sprintf(apConfig.ssid, "%s", config.ap_ssid);
   os_memset(apConfig.password, 0, 64);
   os_sprintf(apConfig.password, "%s", config.ap_password);
   apConfig.channel = config.ap_channel;
   if (!config.ap_open)
      apConfig.authmode = AUTH_WPA_WPA2_PSK;
   else
      apConfig.authmode = AUTH_OPEN;
   apConfig.ssid_len = 0;// or its actual length

   apConfig.max_connection = config.max_clients; // how many stations can connect to ESP8266 softAP at most.
   apConfig.ssid_hidden = config.ssid_hidden;

   // Set ESP8266 softap config
   wifi_softap_set_config(&apConfig);
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
void_write_char(char c) {}

LOCAL void
write_to_pbuf(char c)
{
    slipif_received_byte(&sl_netif, c);
}

//-------------------------------------------------------------------------------------------------
//Init function
void ICACHE_FLASH_ATTR  user_init()
{
ip_addr_t netmask;
ip_addr_t gw;

// This interface number 2 is just to avoid any confusion with the WiFi-Interfaces (0 and 1)
// Should be different in the name anyway - just to be sure
// Matches the number in sio_open()
char int_no = 2; 

    connected = false;
    console_rx_buffer = ringbuf_new(80);
    console_tx_buffer = ringbuf_new(MAX_CON_SEND_SIZE);

#ifdef DEBUG_SOFTUART
    // Initialize software uart
    Softuart_SetPinRx(&softuart,14);	
    Softuart_SetPinTx(&softuart,12);
    Softuart_Init(&softuart,19200);

    // os_printf to softuart
    os_install_putc1(softuart_write_char);

    os_printf("\r\n\r\nSLIP Interface V1.0 starting\r\n");
#else
    // all system output to /dev/null
    system_set_os_print(0);
    os_install_putc1(void_write_char);
#endif /* DEBUG_SOFTUART */

    // Load WiFi-config
    config_load(0, &config);

    g_bit_rate = config.bit_rate;
    remote_console_disconnect = 0;

    if (config.use_ap) {
	// Start the AP-Mode
	wifi_set_opmode(SOFTAP_MODE);
    	user_set_softap_wifi_config();
    } else {
	// Start the STA-Mode
	wifi_set_opmode(STATION_MODE);
	user_set_station_config();
    }

    system_update_cpu_freq(config.clock_speed);

    // The callback fn that unloads one char from the receive buffer of UART0
    // We write it directly into the lwip pbufs
    uart0_unload_fn = write_to_pbuf;

    // Configure the SLIP interface
    if (config.use_ap) {
	IP4_ADDR(&netmask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 240, 2);
	netif_add (&sl_netif, &config.ip_addr, &netmask, &gw, &int_no, slipif_init, ip_input);
	netif_set_up(&sl_netif);

	// enable NAT on the AP interface
	ip_addr_t ap_ip;
	IP4_ADDR(&ap_ip, 192, 168, 4, 1);
	ip_napt_enable(ap_ip.addr, 1);

    } else {
	IP4_ADDR(&netmask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 127, 0, 0, 1);
	netif_add (&sl_netif, &config.ip_addr, &netmask, &gw, &int_no, slipif_init, ip_input);
	netif_set_up(&sl_netif);

	// enable NAT on the SLIP interface for outgoing traffic via WiFi
	ip_napt_enable(config.ip_addr.addr, 1);
    }

    // Start the telnet server (TCP)
    os_printf("Starting Console TCP Server on %d port\r\n", CONSOLE_SERVER_PORT);
    struct espconn *pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));
    if (pCon == NULL)
    {
        os_printf("CONNECT FAIL\r\n");
        return;
    }

    // Equivalent to bind
    pCon->type  = ESPCONN_TCP;
    pCon->state = ESPCONN_NONE;
    pCon->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    pCon->proto.tcp->local_port = CONSOLE_SERVER_PORT;

    // Register callback when clients connect to the server
    espconn_regist_connectcb(pCon, tcp_client_connected_cb);

    // Put the connection in accept mode
    espconn_accept(pCon);

    //Start our user task
    system_os_task(user_procTask, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
}
