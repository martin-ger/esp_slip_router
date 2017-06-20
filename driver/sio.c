#include "lwip/opt.h"
#include "lwip/sio.h"

#include "driver/uart.h"

// UartDev is defined and initialized in rom code.
extern UartDevice    UartDev;
bool uart_poll;

extern u32_t g_bit_rate;

/**
 * Opens a serial device for communication.
 * 
 * @param devnum device number
 * @return handle to serial device if successful, NULL otherwise
 */
sio_fd_t ICACHE_FLASH_ATTR sio_open(u8_t devnum) {
  if (devnum = 2) {
    // Initialize HW UART
    uart_init(g_bit_rate);

    return &UartDev;
  }
  return NULL;
}


/**
 * Sends a single character to the serial device.
 * 
 * @param c character to send
 * @param fd serial device handle
 * 
 * @note This function will block until the character can be sent.
 */
void ICACHE_FLASH_ATTR sio_send(u8_t c, sio_fd_t fd) {
  tx_buff_enq(&c, 1);
}

/**
 * Receives a single character from the serial device.
 * 
 * @param fd serial device handle
 * 
 * @note This function will block until a character is received.
 */
u8_t ICACHE_FLASH_ATTR sio_recv(sio_fd_t fd) {
u8_t c;

  while (rx_buff_deq(&c, 1) == 0);
  return c;  
}

/**
 * Reads from the serial device.
 * 
 * @param fd serial device handle
 * @param data pointer to data buffer for receiving
 * @param len maximum length (in bytes) of data to receive
 * @return number of bytes actually received - may be 0 if aborted by sio_read_abort
 * 
 * @note This function will block until data can be received. The blocking
 * can be cancelled by calling sio_read_abort().
 */
u32_t ICACHE_FLASH_ATTR sio_read(sio_fd_t fd, u8_t *data, u32_t len) {
u32_t r_len;

  uart_poll = true;
  while (r_len = rx_buff_deq(data, len) == 0 && uart_poll);
  return r_len;
}


/**
 * Tries to read from the serial device. Same as sio_read but returns
 * immediately if no data is available and never blocks.
 * 
 * @param fd serial device handle
 * @param data pointer to data buffer for receiving
 * @param len maximum length (in bytes) of data to receive
 * @return number of bytes actually received
 */
u32_t ICACHE_FLASH_ATTR sio_tryread(sio_fd_t fd, u8_t *data, u32_t len) {

  return rx_buff_deq(data, len);
}


/**
 * Writes to the serial device.
 * 
 * @param fd serial device handle
 * @param data pointer to data to send
 * @param len length (in bytes) of data to send
 * @return number of bytes actually sent
 * 
 * @note This function will block until all data can be sent.
 */
u32_t ICACHE_FLASH_ATTR sio_write(sio_fd_t fd, u8_t *data, u32_t len) {

  tx_buff_enq(data, len);
}


/**
 * Aborts a blocking sio_read() call.
 * 
 * @param fd serial device handle
 */
void ICACHE_FLASH_ATTR sio_read_abort(sio_fd_t fd) {
   uart_poll = false;
}


