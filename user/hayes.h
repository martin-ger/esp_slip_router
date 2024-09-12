#ifndef _HAYES_H_
#define _HAYES_H_
#include "c_types.h"

#define BS 8
#define CR 13
#define LF 10
#define DC1 17
#define DC3 19
#define ESC_CHR 43
#define REG_ESC modem.prefs.regs[2]
#define REG_CR modem.prefs.regs[3]
#define REG_LF modem.prefs.regs[4]
#define REG_BS modem.prefs.regs[5]

static char ERR_NOTIMPL[] = "Not implemented";

static char RESP_OK[] = "OK"; // 0
static char RESP_CON[] = "CONNECT"; // 1
static char RESP_RING[] = "RING"; // 2
static char RESP_NOCAR[] = "NO CARRIER"; // 3
static char RESP_ERR[] = "ERROR"; // 4
static char RESP_NODT[] = "NO DIAL TONE"; // 6
static char RESP_BUS[] = "BUSY"; // 7
static char RESP_NOANS[] = "NO ANSWER"; // 8, if @ in dialstring
static char RESP_RR[] = "RINGING"; // 11
static char RESP_CONBAUD[] = "CONNECT %u"; // 5,10,13,15,...

static char S_PROD[] = "ESP_SR"; // Short product ID
static char S_ID[] = "esp_slip_router Emulated Hayes Modem"; // Long product ID
static char S_REG_C[] = "S%u = %c";
static char S_REG_I[] = "S%u = %u";

typedef enum {
  OKAY = 0,
  CONNECT = 1,
  RING = 2,
  NO_CARRIER = 3,
  ERROR = 4,
  CONNECT_BAUD = 5,
  NO_DIAL_TONE = 6,
  LINE_BUSY = 7,
  NO_ANSWER = 8,
  RINGING = 11
} hayes_result_t;

typedef struct {
  bool echo:1;
  bool quiet:1;
  unsigned int report:3; // ATX0-4 report levels
  bool verbose:1;
  uint8_t regs[39]; // kind of a waste bothering to do bit-offset bools above
                    // only to then allocate 39 bytes on registers.
                    // sad !
} h_prefs_t;

typedef struct {
  bool online:1; // online (not accepting commands) mode, default false
  bool in_cmd:1; // last two input chars were A and T
  bool in_esc:1; // online==true, last char was +
  unsigned int n_escs:2; // 0-3
  
  bool on_hook:1; // line is on-hook or not. cosmetic.
  bool in_call:1; // call is active or not. cosmetic.
  char cmdbuf[40];  // since char is apparently 0-127, this could theoretically
                    // be shortened, but i am not that insane.
  unsigned int cmd_i:6; // 0-63
  unsigned int l_cmd_i:6;
  char l_chr;
} h_state_t;

typedef struct {
  h_prefs_t prefs;
  h_state_t state;
} hayes_t;
#endif
