#include "c_types.h"

#define CR 13
#define LF 10
// '+'
#define ESC_CMD 43

static char S_NOTIMPL[] = "Not implemented";
static char S_OK[] = "OK";
static char S_NOCAR[] = "NO CARRIER";
static char S_ERR[] = "ERROR";
static char S_CONN[] = "CONNECT %u";

typedef struct {
	// state
	bool cmdmode; // in command mode, default: true
	bool in_AT; // input was AT, now parsing commands

	bool in_escape; // not in command mode, last character was +
	uint8_t n_plus; // consecutive + inputs

	char last_chr; // stores the previous character
	char cmdbuf[40]; // stores the command buffer
	uint8_t cmdbuf_c; // cursor for command buffer

	bool call_active; //default: false

	bool echo; // default: true
	bool verbose; // default: true
	bool quiet; // default: false
} hayes_t;
