#ifndef SWUPDATEPACKAGEHANDLER_H
#define SWUPDATEPACKAGEHANDLER_H

#include "main.h"

// UART RX state machine
typedef enum {
  UART_RX_STATE_START,
  UART_RX_STATE_CMD,
  UART_RX_STATE_LENGTH,
  UART_RX_STATE_DATA,
  UART_RX_STATE_CRC,
  UART_RX_STATE_STOP
} uart_rx_state_t;

typedef enum command_t
{
  CMD_NONE = 0x00,
  CMD_START_UPDATE = 0x01,
  CMD_VERSION = 0x02,
  CMD_ERASE = 0x03,
  CMD_WRITE = 0x04,
  CMD_FINISH = 0x05,
  CMD_RESET = 0x06
}command_t;

typedef struct header_t
{
  uint8_t start;
  uint8_t cmd;
  uint16_t length;
}header_t;

typedef struct package_t
{
  header_t header;
  uint16_t packet_index;
  uint16_t flash_offset;
  uint8_t data[256];
  uint16_t crc16;
  uint8_t stop;
}package_t;

void SWUpdatePackageHandler_Init(UART_HandleTypeDef *huart);
void SWUpdatePackageHandler_ReceiveFrame(UART_HandleTypeDef *huart);
void SWUpdateMode_Start(void);


#endif /* SWUPDATEPACKAGEHANDLER_H */
