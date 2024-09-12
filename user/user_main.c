#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/lwip_napt.h"
#include "lwip/app/espconn.h"
#include "lwip/app/espconn_tcp.h"

#include "lwip/ip.h"
#include "lwip/ip_route.h"
#include "netif/slipif.h"
#include "driver/uart.h"
#include "driver/softuart.h"

#include "ringbuf.h"
#include "user_config.h"

#ifndef ENABLE_HAYES
#define ENABLE_HAYES
#endif

#ifdef ENABLE_HAYES
#include "hayes.h"
#endif

#include "config_flash.h"

#define user_procTaskPrio        0
#define user_procTaskQueueLen    10
os_event_t    user_procTaskQueue[user_procTaskQueueLen];

static char INVALID_LOCKED[] = "Invalid command. Config locked\r\n";
static char INVALID_NUMARGS[] = "Invalid number of arguments\r\n";
static char INVALID_ARG[] = "Invalid argument\r\n";

Softuart softuart;

struct netif sl_netif;

// Holds the system wide configuration
sysconfig_t config;

#ifdef ENABLE_HAYES
// Holds the Hayes modem emulation state
hayes_t modem;
#endif

static ringbuf_t console_rx_buffer, console_tx_buffer;

static ip_addr_t my_ip, dns_ip;
bool connected;

uint8_t remote_console_disconnect;

uint32_t g_bit_rate;

uint64_t Bytes_in, Bytes_out;

static os_timer_t ptimer;

// Similar to strtok
int ICACHE_FLASH_ATTR parse_str_into_tokens(char *str, char **tokens, int max_tokens)
{
    char *p, *q, *end;
    int token_count = 0;
    bool in_token = false;

    // preprocessing
    for (p = q = str; *p != 0; p++)
    {
        if (*(p) == '%' && *(p + 1) != 0 && *(p + 2) != 0)
        {
            // quoted hex
            uint8_t a;
            p++;
            if (*p <= '9')
                a = *p - '0';
            else
                a = toupper(*p) - 'A' + 10;
            a <<= 4;
            p++;
            if (*p <= '9')
                a += *p - '0';
            else
                a += toupper(*p) - 'A' + 10;
            *q++ = a;
        }
        else if (*p == '\\' && *(p + 1) != 0)
        {
            // next char is quoted - just copy it, skip this one
            *q++ = *++p;
        }
        else if (*p == 8)
        {
            // backspace - delete previous char
            if (q != str)
                q--;
        }
        else if (*p <= ' ')
        {
            // mark this as whitespace
            *q++ = 0;
        }
        else
        {
            *q++ = *p;
        }
    }

    end = q;
    *q = 0;

    // cut into tokens
    for (p = str; p != end; p++)
    {
        if (*p == 0)
        {
            if (in_token)
            {
                in_token = false;
            }
        }
        else
        {
            if (!in_token)
            {
                tokens[token_count++] = p;
                if (token_count == max_tokens)
                    return token_count;
                in_token = true;
            }
        }
    }
    return token_count;
}


void ICACHE_FLASH_ATTR console_send_response(struct espconn *pespconn)
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
    char *tokens[6];

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

    nTokens = parse_str_into_tokens(cmd_line, tokens, 6);

    if (nTokens == 0) {
	char c = '\n';
	ringbuf_memcpy_into(console_tx_buffer, &c, 1);
	goto command_handled;
    }

    if (strcmp(tokens[0], "help") == 0)
    {
        os_sprintf(response, "show [stats]\r\nset [ssid|password|auto_connect|addr|addr_peer|speed|bitrate] <val>\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        os_sprintf(response, "set [use_ap|ap_ssid|ap_password|ap_channel|ap_open|ssid_hidden|max_clients|dns] <val>\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        os_sprintf(response, "quit|save|reset [factory]|lock|unlock <password>\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        os_sprintf(response, "portmap [add|remove] [TCP|UDP] <ext_port> <int_addr> <int_port>\r\n");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
#ifdef ALLOW_SCANNING
        os_sprintf(response, "scan");
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
#endif
	ringbuf_memcpy_into(console_tx_buffer, "\r\n", 2);
        goto command_handled;
    }

    if (strcmp(tokens[0], "show") == 0)
    {
      int16_t i;
      struct portmap_table *p;
      ip_addr_t i_ip;

      if (nTokens == 1) {
	os_sprintf(response, "ESP SLIP Router %s (build: %s)\r\n", ESP_SLIP_ROUTER_VERSION, __TIMESTAMP__);
	ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
        os_sprintf(response, "SLIP: IP: " IPSTR " PeerIP: " IPSTR "\r\n", IP2STR(&config.ip_addr), IP2STR(&config.ip_addr_peer));
        ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	if (config.use_ap) {
	    os_sprintf(response, "DNS server: " IPSTR "\r\n", IP2STR(&config.ap_dns));
	    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
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

	for (i = 0; i<IP_PORTMAP_MAX; i++) {
	    p = &ip_portmap_table[i];
	    if(p->valid) {
		i_ip.addr = p->daddr;
		os_sprintf(response, "Portmap: %s: " IPSTR ":%d -> "  IPSTR ":%d\r\n",
		   p->proto==IP_PROTO_TCP?"TCP":p->proto==IP_PROTO_UDP?"UDP":"???",
		   IP2STR(&my_ip), ntohs(p->mport), IP2STR(&i_ip), ntohs(p->dport));
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	    }
	}

	goto command_handled;
      }

      if (nTokens == 2 && strcmp(tokens[1], "stats") == 0) {

	   os_sprintf(response, "%d KiB in\r\n%d KiB out\r\n",
         (uint32_t)(Bytes_in/1024), (uint32_t)(Bytes_out/1024));
	   ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));

	   os_sprintf(response, "Free mem: %d\r\n", system_get_free_heap_size());
	   ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));

	   if (config.use_ap) {
	     os_sprintf(response, "%d Station%s connected to SoftAP\r\n", wifi_softap_get_station_num(),
		  wifi_softap_get_station_num()==1?"":"s");
	     ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	   } else {
	     if (connected) {
		struct netif *sta_nf = (struct netif *)eagle_lwip_getif(0);
		os_sprintf(response, "STA IP: %d.%d.%d.%d GW: %d.%d.%d.%d\r\n", IP2STR(&sta_nf->ip_addr), IP2STR(&sta_nf->gw));
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
		os_sprintf(response, "STA RSSI: %d\r\n", wifi_station_get_rssi());
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	     } else {
		os_sprintf(response, "STA not connected\r\n");
		ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	     }
	   }
	   goto command_handled;
      }
    }

    if (strcmp(tokens[0], "save") == 0)
    {
        config_save(&config);
	// also save the portmap table
	blob_save(0, (uint32_t *)ip_portmap_table, sizeof(struct portmap_table) * IP_PORTMAP_MAX);
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
           config_save(&config);
	   // clear saved portmap table
	   blob_zero(0, sizeof(struct portmap_table) * IP_PORTMAP_MAX);
	}
        os_printf("Restarting ... \r\n");
	system_restart();
        while(true);
    }

    if (strcmp(tokens[0], "quit") == 0)
    {
	remote_console_disconnect = 1;
        goto command_handled;
    }

    if (strcmp(tokens[0], "portmap") == 0)
    {
    uint32_t daddr;
    uint16_t mport;
    uint16_t dport;
    uint8_t proto;
    bool add;
    uint8_t retval;

        if (config.locked)
        {
            os_sprintf(response, INVALID_LOCKED);
	    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
            goto command_handled;
        }

        if (nTokens < 4 || (strcmp(tokens[1],"add")==0 && nTokens != 6))
        {
            os_sprintf(response, INVALID_NUMARGS);
	    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	    goto command_handled;
        }

        add = strcmp(tokens[1],"add")==0;
	if (!add && strcmp(tokens[1],"remove")!=0) {
	    os_sprintf(response, INVALID_ARG);
	    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	    goto command_handled;
	}

	if (strcmp(tokens[2],"TCP") == 0) proto = IP_PROTO_TCP;
	else if (strcmp(tokens[2],"UDP") == 0) proto = IP_PROTO_UDP;
        else {
	    os_sprintf(response, INVALID_ARG);
	    ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
	    goto command_handled;
	}

        mport = (uint16_t)atoi(tokens[3]);
	if (add) {
	    daddr = ipaddr_addr(tokens[4]);
            dport = atoi(tokens[5]);
	    retval = ip_portmap_add(proto, my_ip.addr, mport, daddr, dport);
	} else {
            retval = ip_portmap_remove(proto, mport);
	}

	if (retval) {
	    os_sprintf(response, "Portmap %s\r\n", add?"set":"deleted");
	} else {
	    os_sprintf(response, "Portmap failed\r\n");
	}
	ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
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
            os_sprintf(response, INVALID_NUMARGS);
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
            os_sprintf(response, INVALID_LOCKED);
            ringbuf_memcpy_into(console_tx_buffer, response, os_strlen(response));
            goto command_handled;
        }

        /*
         * For set commands atleast 2 tokens "set" "parameter" "value" is needed
         * hence the check
         */
        if (nTokens < 3)
        {
            os_sprintf(response, INVALID_NUMARGS);
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

            if (strcmp(tokens[1],"dns") == 0)
            {
                config.ap_dns.addr = ipaddr_addr(tokens[2]);
                os_sprintf(response, "DNS address set to %d.%d.%d.%d/24\r\n",
			IP2STR(&config.ap_dns));
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

            if (strcmp(tokens[1],"addr_peer") == 0)
            {
                config.ip_addr_peer.addr = ipaddr_addr(tokens[2]);
                os_sprintf(response, "IP peer address set to %d.%d.%d.%d/24\r\n",
			IP2STR(&config.ip_addr_peer));
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

#ifdef STATUS_LED
// Timer cb function
void ICACHE_FLASH_ATTR timer_func(void *arg)
{
    // Turn LED off
    GPIO_OUTPUT_SET (STATUS_LED, 1);
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
void ICACHE_FLASH_ATTR wifi_handle_event_cb(System_Event_t *evt)
{
int i;

    //os_printf("wifi_handle_event_cb: ");
    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        os_printf("connect to ssid %s, channel %d\n", evt->event_info.connected.ssid, evt->event_info.connected.channel);
        break;

    case EVENT_STAMODE_DISCONNECTED:
        os_printf("disconnect from ssid %s, reason %d\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
	    connected = false;
#ifdef STATUS_LED
        // Stop LED-off timer
        os_timer_disarm(&ptimer);
        // Turn LED on when waiting
        GPIO_OUTPUT_SET (STATUS_LED, 0);
#endif
        break;

    case EVENT_STAMODE_AUTHMODE_CHANGE:
        os_printf("mode: %d -> %d\n", evt->event_info.auth_change.old_mode, evt->event_info.auth_change.new_mode);
        break;

    case EVENT_STAMODE_GOT_IP:
	    dns_ip = dns_getserver(0);

        os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR ",dns:" IPSTR "\n", IP2STR(&evt->event_info.got_ip.ip), IP2STR(&evt->event_info.got_ip.mask), IP2STR(&evt->event_info.got_ip.gw), IP2STR(&dns_ip));
 #ifdef STATUS_LED
        // Turn LED off when ready
        GPIO_OUTPUT_SET (STATUS_LED, 1);
        // Start LED-off timer
        os_timer_setfn(&ptimer, timer_func, 0);
        os_timer_arm(&ptimer, 100, 1);
#endif
	    my_ip = evt->event_info.got_ip.ip;
	    connected = true;

	    // Update any predefined portmaps to the new IP addr
        for (i = 0; i<IP_PORTMAP_MAX; i++) {
	        if(ip_portmap_table[i].valid) {
	            ip_portmap_table[i].maddr = my_ip.addr;
	        }
	    }
        break;

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

LOCAL void ICACHE_FLASH_ATTR
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

LOCAL void ICACHE_FLASH_ATTR
void_write_char(char c) {}

#ifdef ENABLE_HAYES
// Hayes-compatible wrapper
/* TODO / To implement / notes
 * ATI1-7 commands
 * honour verbose/quiet modes
 * verbose mode defaults to enabled; disabled it should just output numbers
 * quiet mode defaults to disabled; enabled it should output nothing in response
 * configuration via AT& commands as an alternative to telnet
 * ATZ to reset
 * persist settings to nvram
 * it would be cool if autodetecting baud rate worked but that might be impossible
 *
 * command reference used: https://support.usr.com/support/756/756-ug/six.html
 */
LOCAL void ICACHE_FLASH_ATTR h_init() {
  modem = (hayes_t){.prefs={0},.state={0}};
  modem.prefs.echo = true;
  modem.prefs.report = 7;
  modem.prefs.verbose = true;
  // Register settings
  modem.prefs.regs[2] = ESC_CHR;
  modem.prefs.regs[3] = CR;
  modem.prefs.regs[4] = LF;
  modem.prefs.regs[5] = BS;
  modem.prefs.regs[6] = 2;
  modem.prefs.regs[7] = 60;
  modem.prefs.regs[8] = 2;
  modem.prefs.regs[9] = 6;
  modem.prefs.regs[10] = 7;
  modem.prefs.regs[11] = 70;
  modem.prefs.regs[12] = 50;
  modem.prefs.regs[21] = 10;
  modem.prefs.regs[22] = DC1;
  modem.prefs.regs[23] = DC3;
  modem.prefs.regs[25] = 5;
  if(HAYES_CMD_MODE_AT_BOOT)
    modem.state.on_hook = true;
  else {
    modem.state.on_hook = false;
    modem.state.online = true;
  }
}
LOCAL bool ICACHE_FLASH_ATTR h_is_num(char c) {
  return c > 47 && c < 59;
}
static uint8_t ICACHE_FLASH_ATTR h_parse_num(char c) {
  return c-48;
}
LOCAL uint8_t ICACHE_FLASH_ATTR h_multi_parse_num(uint8_t i) {
  uint8_t n = 0;
  // This just feels real gross but,. eh.
  // Validate first number
  if(!h_is_num(modem.state.cmdbuf[i])) return 0;
  if(i+1 < modem.state.cmd_i && h_is_num(modem.state.cmdbuf[i+1])) {
    if(i+2 < modem.state.cmd_i && h_is_num(modem.state.cmdbuf[i+2]))
      n += 100*h_parse_num(modem.state.cmdbuf[i++]);
    n += 10*h_parse_num(modem.state.cmdbuf[i++]);
  }
  n += h_parse_num(modem.state.cmdbuf[i]);
  return n;
}
LOCAL void ICACHE_FLASH_ATTR h_echo(char c) {
  if(!modem.prefs.echo) return;
  // LF is swallowed if last character was CR
  if(c == REG_LF && modem.state.l_chr == REG_CR) return;
  uart_tx_one_char(UART0, c);
}
LOCAL void ICACHE_FLASH_ATTR h_print(char s[]) {
  uint16_t i=0;
  uint16_t sz=os_strlen(s);
  while(i<sz) {
    uart_tx_one_char(UART0, s[i++]);
  }
  uart_tx_one_char(UART0, REG_CR);
}
LOCAL void h_print_i(uint8_t n) {
  char s[3]; // uint8_t can at most be 255
  os_sprintf(s, "%u", n);
  h_print(s);
}
LOCAL void ICACHE_FLASH_ATTR h_print_h(char suffix[]) {
  char str[60];
  os_sprintf(str, "%s %s", S_ID, suffix);
  h_print(str);
}
LOCAL void ICACHE_FLASH_ATTR h_result_connbaud() {
  if(modem.prefs.quiet) return;
  if(modem.prefs.verbose) {
    char connstr[15];
    os_sprintf(connstr, RESP_CONBAUD, config.bit_rate);
    h_print(connstr);
    return;
  }
  switch(config.bit_rate) {
    case 56000: h_print_i(232); break;
    case 54666: h_print_i(228); break;
    case 53333: h_print_i(224); break;
    case 52000: h_print_i(220); break;
    case 50666: h_print_i(216); break;
    case 49333: h_print_i(212); break;
    case 48000: h_print_i(208); break;
    case 46666: h_print_i(204); break;
    case 45333: h_print_i(200); break;
    case 44000: h_print_i(196); break;
    case 42666: h_print_i(192); break;
    case 41333: h_print_i(188); break;
    case 37333: h_print_i(184); break;
    case 33333: h_print_i(180); break;
    case 33600: h_print_i(155); break;
    case 31200: h_print_i(151); break;
    case 28800: h_print_i(107); break;
    case 26400: h_print_i(103); break;
    case 24000: h_print_i( 99); break;
    case 21600: h_print_i( 91); break;
    case 19200: h_print_i( 85); break;
    case 16800: h_print_i( 43); break;
    case 14400: h_print_i( 25); break;
    case 12000: h_print_i( 21); break;
    case  7200: h_print_i( 20); break;
    case  4800: h_print_i( 18); break;
    case  1200: h_print_i( 15); break;
    case  9600: h_print_i( 13); break;
    case  2400: h_print_i( 10); break;
    default   : h_print_i(1);
  }
}
LOCAL void ICACHE_FLASH_ATTR h_result_send(char verbose[], uint8_t code) {
  if(modem.prefs.quiet) return;
  if(modem.prefs.verbose) h_print(verbose);
  else h_print_i(code);
}
LOCAL void ICACHE_FLASH_ATTR h_result(hayes_result_t res) {
  if(modem.prefs.quiet) return;
  char output[20]; // can't declare inside a switch block
  switch(res) {
    case OKAY:
      h_result_send(RESP_OK, 0);
      break;
    case CONNECT:
      h_result_send(RESP_CON, 1);
      break;
    case RING:
      h_result_send(RESP_RING, 2);
      break;
    case NO_CARRIER:
      h_result_send(RESP_NOCAR, 3);
      break;
    case ERROR:
      h_result_send(RESP_ERR, 4);
      break;
    case CONNECT_BAUD:
      h_result_connbaud();
      break;
    case NO_DIAL_TONE:
      h_result_send(RESP_NODT, 6);
      break;
    case LINE_BUSY:
      h_result_send(RESP_BUS, 7);
      break;
    case NO_ANSWER:
      h_result_send(RESP_NOANS, 8);
      break;
    case RINGING:
      h_result_send(RESP_RR, 11);
      break;
    default:
      os_sprintf(output, "????? %u", res);
      h_print(output);
  }
}
LOCAL void ICACHE_FLASH_ATTR h_dial(bool go_online) {
  // Handle "dialling".
  modem.state.in_call = true;
  modem.state.online = go_online;
  h_result(CONNECT_BAUD);
}

// AT command implementations
// Commands that take an argument will return a bool indicating if that
//  argument was used, as boolean commands can be shortened to just their
//  letter if the desired effect is to set them to false.
// Commands that will terminate the command parse process are prefixed 'ht_'
#ifdef DISABLED_CODE
LOCAL void ICACHE_FLASH_ATTR ht_ATDOLLAR() {
  h_result(OKAY);
}
LOCAL void ICACHE_FLASH_ATTR ht_ATAMPDOLLAR() {
  h_result(OKAY);
}
#endif
LOCAL void ICACHE_FLASH_ATTR hz_ATA() {
  // ATA - Answer
  modem.state.on_hook=false;
  modem.state.in_call=true;
  h_result(OKAY);
  return;
}
LOCAL bool ICACHE_FLASH_ATTR h_ATA(char a) {
  // ATA0 - Answer 0
  // Possibly the argument here is for if you have more than one line/"call"?
  modem.state.on_hook=false;
  modem.state.in_call=true;
  h_result(OKAY);
  return a == '0';
}
#ifdef DISABLED_CODE
LOCAL void ICACHE_FLASH_ATTR ht_ATDDOLLAR() {
  h_result(OKAY);
}
#endif
LOCAL void ICACHE_FLASH_ATTR ht_ATDL() {
  modem.state.on_hook = false;
  h_dial(true);
}
LOCAL uint8_t ICACHE_FLASH_ATTR ht_ATDN(uint8_t i) {
  uint8_t taken = 1;
  uint8_t pause_time = 0;
  modem.state.on_hook = false;
  bool go_online = true;
  while(i+taken<modem.state.cmd_i) {
    if(!h_is_num(modem.state.cmdbuf[i+taken]) &&
      modem.state.cmdbuf[i+taken] != ',' && // 2s pause before resuming dial
      modem.state.cmdbuf[i+taken] != '@' && // Wait for answer (X3, X4)
      modem.state.cmdbuf[i+taken] != '.' && // Not in spec, allows dialling IPs
      modem.state.cmdbuf[i+taken] != 'W' && // Wait for second dialtone (X2, X4)
      modem.state.cmdbuf[i+taken] != '#' && // Aux tone dial digit
      modem.state.cmdbuf[i+taken] != '!' && // Switch hook flash
      modem.state.cmdbuf[i+taken] != '$' && // Wait for calling-card bong
      modem.state.cmdbuf[i+taken] != '&' && // Wait for calling-card bong
      modem.state.cmdbuf[i+taken] != ';' && // Remain in command mode after dial
      modem.state.cmdbuf[i+taken] != '*' && // Aux tone dial digit
      modem.state.cmdbuf[i+taken] != '"') { // Set quote mode for the following?
      break;
    } else {
      switch(modem.state.cmdbuf[i+taken]) {
        case ';':
          go_online = false;
          break;
        case ',':
        case 'W':
        case '@':
          pause_time += 2;
          break;
      }
    }
    taken++;
  }
  h_dial(go_online);
  return taken;
}
LOCAL void ICACHE_FLASH_ATTR hz_ATD() {
  // ATD - Dial (no arguments)
  h_result(ERROR);
}
LOCAL uint8_t ICACHE_FLASH_ATTR ht_ATD(uint8_t i) {
  // ATD - Dial
  // Takes in the current location in the command buffer + 1
  // Returns how many characters it consumed
  switch(modem.state.cmdbuf[i]) {
    case 'L':
      ht_ATDL();
      return 1;
    case 'P': // Pulse-dial
    case 'R': // Dial an originate-only modem
    case 'T': // Touch-tone dial
      return ht_ATDN(i);
    case 'S':
      h_result(ERROR);
      return 1;
#ifdef DISABLED_CODE
    case '$':
      ht_ATDDOLLAR();
      return 1;
#endif
  }
  if(h_is_num(modem.state.cmdbuf[i])) return ht_ATDN(--i)-1;
  h_result(ERROR);
  return 40-i;
}
LOCAL bool ICACHE_FLASH_ATTR h_ATE(char a) {
  // ATE[0,1] - Echo on/off
  h_result(OKAY);
  if(a != '0' && a != '1') {
    modem.prefs.echo = false;
    return false;
  }
  modem.prefs.echo = a == '1';
  return true;
}
LOCAL void ICACHE_FLASH_ATTR hz_ATE() {
  // ATE - Echo off
  modem.prefs.echo = false;
  h_result(OKAY);
}
LOCAL void ICACHE_FLASH_ATTR hz_ATI() {
  // ATI - Should error if missing argument.
  h_result(ERROR);
}
LOCAL void ICACHE_FLASH_ATTR ht_ATI(uint8_t i) {
  // ATI0-11 - Inform, Inquire, Interrogate
  if(!h_is_num(modem.state.cmdbuf[i])) {
    h_result(ERROR);
    return;
  }
  uint8_t method = h_multi_parse_num(i);
  switch(method) {
    case 0:
      // ATI0 - Model string
      h_print("ESP_SR");
      break;
    case 1:
      // ATI1 - ROM Checksum (4 characters)
      h_print("A0B1");
      break;
    case 2:
      // ATI2 - RAM test results
      break;
    case 3:
      // ATI3 - Firmware version
      h_print(ESP_SLIP_ROUTER_VERSION);
      break;
    case 4:
      // ATI4 - Settings
      /*
       * Lists states for B, C, E, F, L, M, Q, V and X settings
       * Lists baud, parity, length
       * "DIAL=HUNT"? "ON HOOKAY" "TIMER"
       * Lists states for &A, B, C, D, ...
       * Lists register states
       * Lists last dialed number
       */
      break;
    case 5:
      // ATI5 - NVRAM Settings
      // lists most of the same as above but also phonebook and extra settings
      // also stored command(?)
      break;
    case 6:
      // ATI6 - Link diagnostics
      // Chars, Octets, Blocks sent/recv
      // Chars lost
      // Blocks resent
      // Retrains req/granted
      // Line reversals
      // "Blers"
      // Link timeouts/naks
      // Compression, Equalization, fallback, last call length, current state
      break;
    case 7:
      // ATI7 - configuration profile
      break;
    // ATI8 - (riker voice) that never happened.
    case 9:
      // ATI9 - some kind of plug'n'play string?
      // (1.0USR00BA\\MODEM\PNPC107\USRobotics Courier V.Everything EXT)
      break;
    case 10:
      // ATI10 - Dial security status
      // long listing on this
      // Could be used to display current lock status for router
      break;
    case 11:
      // ATI11 - More link diagnostics
      // Modulation, carrier freq., sym rate, encoding, shaping
      // Signal/noise levels, echo loss, timing, up/down/speed shifts
      // V.90 status
      break;
    default:
      h_result(ERROR);
      return;
  }
  h_result(OKAY);
}
LOCAL void ICACHE_FLASH_ATTR htz_ATH() {
  // ATH - Hangup
  // Whether a call is in progress or not, this always returns 'OKAY'
  //  on real hardware.
  modem.state.on_hook = true;
  modem.state.in_call = false;
  h_result(OKAY);
}
LOCAL bool ICACHE_FLASH_ATTR ht_ATH(char c) {
  if(c != '0' && c != '1') {
    htz_ATH();
    return false;
  }
  modem.state.on_hook = c == '0';
  return true;
}
LOCAL void ICACHE_FLASH_ATTR ht_ATO() {
  // ATO - Enter on-line mode
  // Output validated to be consistent with real hardware.
  if(modem.state.on_hook || !modem.state.in_call) h_result(NO_CARRIER);
  else {
    modem.state.online = true;
    h_result(OKAY);
  }
}
LOCAL bool ICACHE_FLASH_ATTR h_ATQ(char a) {
  // ATQ[0,1] - Quiet on/off
  h_result(OKAY);
  if(a != '0' && a != '1') {
    modem.prefs.quiet = false;
    return false;
  }
  modem.prefs.quiet = a == '1';
  return true;
}
LOCAL void ICACHE_FLASH_ATTR hz_ATQ() {
  // ATQ - Quiet off
  modem.prefs.quiet = false;
  h_result(OKAY);
}
LOCAL void ICACHE_FLASH_ATTR hz_ATS() {
  // ATS - Set/interrogate/list registers
  // Error on no arguments.
  h_result(ERROR);
}
#ifdef DISABLED_CODE
LOCAL void ICACHE_FLASH_ATTR hz_ATSDOLLAR() {
  h_result(OKAY);
}
#endif
LOCAL uint8_t ICACHE_FLASH_ATTR h_ATS(uint8_t i) {
#ifdef DISABLED_CODE
  if(modem.state.cmdbuf[i] == '$') {
    hz_ATSDOLLAR();
    return 1;
  }
#endif
  if(!h_is_num(modem.state.cmdbuf[i])) {
    h_result(ERROR);
    return 40-i;
  }
  uint8_t taken = 0;
  uint8_t reg = h_multi_parse_num(i);
  if((reg>13 && reg<16) || (reg>25 && reg<38) || reg>38 || reg==17 || reg==20 || reg==24) {
    h_result(ERROR);
    return 40-i;
  }
  // Parse intent
  char buf[20]; // can't declare inside a switch block
  switch(modem.state.cmdbuf[i+taken]) {
    case '?':
      // ATSn? - Interrogate the register's contents
      if((reg>1 && reg<6) || (reg>21 && reg<24))
        // These registers are chars
        os_sprintf(buf, S_REG_C, reg, modem.prefs.regs[reg]);
      else
        os_sprintf(buf, S_REG_I, reg, modem.prefs.regs[reg]);
      h_print(buf);
      h_result(OKAY);
    case '=':
      // ATSn=v - Set a register to a value
      if((reg>1 && reg<6) || (reg>21 && reg<24))
        modem.prefs.regs[reg] = modem.state.cmdbuf[++taken+i];
      else modem.prefs.regs[reg] = h_multi_parse_num(++taken+i);
      h_result(OKAY);
      break;
    default:
      h_result(ERROR);
      return 40-i; // Prevent further command execution
  }
  return taken;
}
LOCAL bool ICACHE_FLASH_ATTR h_ATV(char a) {
  // ATV[0,1] - Verbose on/off
  h_result(OKAY);
  if(a != '0' && a != '1') {
    modem.prefs.verbose = false;
    return false;
  }
  modem.prefs.verbose = a == '1';
  return true;
}
LOCAL void ICACHE_FLASH_ATTR hz_ATX() {
  h_result(ERROR);
}
LOCAL bool ICACHE_FLASH_ATTR h_ATX(char c) {
  if(!h_is_num(c)) return false;
  if(c-48 > 7) {
    h_result(ERROR);
    return true;
  }
  modem.prefs.report = c-48;
  return true;
}
LOCAL void ICACHE_FLASH_ATTR hz_ATV() {
  // ATV - Verbose off
  modem.prefs.verbose = false;
  h_result(OKAY);
}
LOCAL void ICACHE_FLASH_ATTR ht_ATZ() {
  // ATZ - Restart / Reset
  system_restart();
  while(true);
}

// TODO: This should probably be a more traditional command parser/lexer/whatever
// currently commands that can have more than one character of input are
//  treated as terminating commands, so that implementing them is easier
//  in the future this could just be solved by returning an int of how many
//  characters were consumed, which is then added to i
LOCAL void ICACHE_FLASH_ATTR h_cmdparse() {
  if(modem.state.cmd_i==0) {
    h_result(OKAY);
    return;
  }
  uint8_t i=0;
  while(i<modem.state.cmd_i) {
    switch(modem.state.cmdbuf[i]) {
      case 'A': // ATA[n] - Answer
        if(i+1 == modem.state.cmd_i) hz_ATA();
        else if(!h_ATA(modem.state.cmdbuf[++i])) i--;
        break;
      case 'D':
        // ATD[LPRS$][n...] - Dial number
        if(i+1 == modem.state.cmd_i) hz_ATD();
        else i+=ht_ATD(++i);
        return;
      case 'E':
        if(i+1 == modem.state.cmd_i) hz_ATE();
        else if(!h_ATE(modem.state.cmdbuf[++i])) i--;
        break;
      // Not implemented: ATF[n] - Online echo
      case 'H':
        if(i+1 == modem.state.cmd_i) htz_ATH();
        else if(!ht_ATH(modem.state.cmdbuf[++i])) i--;
        break;
      case 'I':
        if(i+1 == modem.state.cmd_i) hz_ATI();
        else ht_ATI(++i);
        break;
      case 'L': // Modem speaker volume
      case 'M': // Modem speaker mode
        if(i+1 != modem.state.cmd_i && h_is_num(modem.state.cmdbuf[++i]))
          h_result(OKAY);
        else h_result(ERROR);
        break;
      case 'O':
        if(i+1 == modem.state.cmd_i) ht_ATO();
        else ht_ATO(++i);
        return;
      case 'Q':
        if(i+1 == modem.state.cmd_i) hz_ATQ();
        else if(!h_ATQ(modem.state.cmdbuf[++i])) i--;
        break;
      case 'S':
        // ATS$, ATSn?, ATSn=v
        if(i+1 == modem.state.cmd_i) h_result(ERROR);
        else i+=h_ATS(++i);
        break;
      case 'V':
        if(i+1 == modem.state.cmd_i) hz_ATV();
        else if(!h_ATV(modem.state.cmdbuf[++i])) i--;
        break;
      case 'X':
        if(i+1 == modem.state.cmd_i) hz_ATX();
        else if(!h_ATX(modem.state.cmdbuf[++i])) i--;
        break;
      case 'Z':
        ht_ATZ();
        return;
#ifdef DISABLED_CODE
      case '$':
        if(i>0 && modem.state.cmdbuf[i-1] == '&') ht_ATAMPDOLLAR();
        else ht_ATDOLLAR();
        return;
#endif
    }
    i++;
  }
  h_result(OKAY);
}

LOCAL void ICACHE_FLASH_ATTR h_recv(char c) {
  h_echo(c);
  if(modem.state.in_cmd) {
    if(c == REG_CR) {
      h_cmdparse();
      modem.state.in_cmd = false;
      modem.state.l_cmd_i = modem.state.cmd_i;
      modem.state.cmd_i = 0;
    } else if(c == REG_BS) {
      modem.state.cmd_i--;
    } else if(modem.state.cmd_i == 40) {
      h_result(ERROR);
      modem.state.in_cmd = false;
      modem.state.l_cmd_i = modem.state.cmd_i = 0;
    } else
      modem.state.cmdbuf[modem.state.cmd_i++] = c;
  } else if(modem.state.l_chr == 'A') {
    switch(c) {
      case '/':
        modem.state.in_cmd = true;
        modem.state.cmd_i = modem.state.l_cmd_i;
        h_cmdparse();
        modem.state.in_cmd = false;
        modem.state.cmd_i = 0;
        break;
      case 'T':
        modem.state.in_cmd = true;
        break;
    }
  }
  // Not implemented: bare '/' (Pause)
  // Pause should wait 125ms before processing further input
  // The docs mention 125ms as a default, but don't indicate if it can change
  modem.state.l_chr = c;
}

// Returns true if input was handled
LOCAL bool ICACHE_FLASH_ATTR h_handler(char c) {
  if(!modem.state.online) {
    h_recv(c);
    return true;
  }
  if(c == REG_ESC) {
    if(modem.state.in_esc && modem.state.n_escs == 2) {
      modem.state.in_esc = false;
      modem.state.n_escs = 0;
      modem.state.online = false;
      h_result(OKAY);
      return true;
    }
    if(!modem.state.in_esc)
      modem.state.in_esc = true;
    modem.state.n_escs++;
    return true;
  }
  if(modem.state.in_esc) {
    modem.state.in_esc = false;
    while(modem.state.n_escs>0) {
      slipif_received_byte(&sl_netif, REG_ESC);
      Bytes_out++;
      modem.state.n_escs--;
    }
  }
  return false;
}
#endif

LOCAL void write_to_pbuf(char c) {
    #ifdef ENABLE_HAYES
    if(h_handler(c)) return;
    #endif
    slipif_received_byte(&sl_netif, c);
    Bytes_out++;
#ifdef STATUS_LED
    // Turn LED on on traffic
    GPIO_OUTPUT_SET (STATUS_LED, 0);
#endif
}

static void ICACHE_FLASH_ATTR set_netif(ip_addr_t netif_ip)
{
struct netif *nif;

	for (nif = netif_list; nif != NULL && nif->ip_addr.addr != netif_ip.addr; nif = nif->next);
	if (nif == NULL) return;

	nif->napt = 1;
}
//-------------------------------------------------------------------------------------------------
//Init function
void ICACHE_FLASH_ATTR  user_init()
{
  #ifdef ENABLE_HAYES
  h_init();
  #endif
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

    // Load config
    if (config_load(&config)== 0) {
	// valid config in FLASH, can read portmap table
	blob_load(0, (uint32_t *)ip_portmap_table, sizeof(struct portmap_table) * IP_PORTMAP_MAX);
    } else {

	// clear portmap table
	blob_zero(0, sizeof(struct portmap_table) * IP_PORTMAP_MAX);
    }

    g_bit_rate = config.bit_rate;
    remote_console_disconnect = 0;

    Bytes_in = Bytes_out = 0;

#ifdef STATUS_LED
    // Config pin as GPIO12
    MUX_STATUS_LED;

    // Turn LED on on start
    GPIO_OUTPUT_SET (STATUS_LED, 0);
#endif

    if (config.use_ap) {
	    // Start the AP-Mode
	    wifi_set_opmode(SOFTAP_MODE);
    	user_set_softap_wifi_config();

 #ifdef STATUS_LED
        // Turn LED off when ready
        GPIO_OUTPUT_SET (STATUS_LED, 1);
        // Start LED-off timer
        os_timer_setfn(&ptimer, timer_func, 0);
        os_timer_arm(&ptimer, 100, 1);
#endif

	    dhcps_set_DNS(&config.ap_dns);
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
	netif_add (&sl_netif, &config.ip_addr, &netmask, &config.ip_addr_peer, &int_no, slipif_init, ip_input);
	netif_set_up(&sl_netif);

	// enable NAT on the AP interface
	//IP4_ADDR(&my_ip, 192, 168, 4, 1);
	//ip_napt_enable(my_ip.addr, 1);

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
        os_printf("ALLOC FAIL\r\n");
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

  #ifdef ENABLE_HAYES
  h_result(OKAY);
  #endif

    //Start our user task
    system_os_task(user_procTask, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
}
