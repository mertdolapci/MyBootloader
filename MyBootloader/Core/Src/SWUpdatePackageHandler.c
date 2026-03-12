#include "SWUpdatePackageHandler.h"
#include <string.h>
/* USER CODE BEGIN PV */

#define PACKAGE_SIZE        256
#define APP_START_ADDRESS  	0x08020000
#define CONFIG_ADDRESS 		0x0800C000

typedef void (*pFunction)(void);

// State Machine Variable
static uart_rx_state_t uartRxState = UART_RX_STATE_START;

static package_t receivedPackage;

// Temporary fields for receiving data
static uint8_t rxStart;
static uint8_t rxCmd;
static uint8_t rxLength[2];
static uint16_t rxDataLen = 0;
static uint8_t rxData[260];     // Max package size + 4 bytes for packet_index and flash_offset
static uint8_t rxCrc[2];
static uint8_t rxStop;
/* USER CODE END PV */

const uint8_t ACK_BYTE = 0x79;
const uint8_t NACK_BYTE = 0x1F;

static void processReceivedPackage(package_t* pkg);
static void JumpToApplication(void);


void SWUpdatePackageHandler_Init(UART_HandleTypeDef *huart)
{
    HAL_UART_Receive_IT(huart, &rxStart, sizeof(char)); // UART RX interrupt enable
}

void SWUpdateMode_Start(void)
{
    
    for(int i=5; i>=0; i--)
    {
        printf("[BOOT] >>> Waiting for update for %d seconds...\r\n", i);
        
        if(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
        {
            printf("[BOOT] >>> Update request detected. Entering update mode.\r\n");
            return; // Exit the function to start receiving packages
        }
        HAL_Delay(1000); // Wait for 1 second before checking for package
    }
    
    printf("[BOOT] >>> No update package received. Exiting update mode.\r\n");
    JumpToApplication();

    // Here you can add any initialization code needed for the update mode
    // For example, you might want to erase the application area in flash or prepare buffers
}

void JumpToApplication(void)
{
    uint32_t appStack = *(volatile uint32_t*)APP_START_ADDRESS;
    pFunction appEntry = (pFunction)*(volatile uint32_t*)(APP_START_ADDRESS + 4);

    __disable_irq();

    // Set MSP to application stack pointer
    __set_MSP(appStack);

    // Set Vector Table Offset Register (VTOR)
    SCB->VTOR = APP_START_ADDRESS;

    __enable_irq();
    // Jump to Application

    printf("Jumping to appEntry: 0x%08lX\r\n", (uint32_t)appEntry);
    appEntry();
}

void erase_flash_sector(uint32_t sector)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef eraseInitStruct;
    uint32_t SectorError = 0;

    // 1. Unlock Flash
    HAL_FLASH_Unlock();

    // 2. Erase parametrelerini ayarla
    eraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    eraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 2.7V - 3.6V
    eraseInitStruct.Sector = sector; // Sector to be erased
    eraseInitStruct.NbSectors = 1;   // Number of Sector to be erased 

    // 3. Sector erase
    status = HAL_FLASHEx_Erase(&eraseInitStruct, &SectorError);

    // 4. Lock Flash
    HAL_FLASH_Lock();

    if (status == HAL_OK)
    {
        printf("Sector %lu erased successfully.\r\n", sector);
    }
    else
    {
        printf("Sector erase failed! Error: %lu\r\n", SectorError);
    }
}

void program_flash_data(uint32_t base_address, uint16_t offset, uint8_t* data, uint16_t length)
{
    HAL_StatusTypeDef status;

    // 1. Unlock Flash
    HAL_FLASH_Unlock();

    // 2. Program Flash byte by byte
    for (uint16_t i = 0; i < length; i++)
    {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, base_address + offset + i, data[i]);
        if (status != HAL_OK)
        {
            printf("Flash programming failed at address 0x%08lX\r\n", base_address + offset + i);
            break;
        }
    }

    // 3. Lock Flash
    HAL_FLASH_Lock();

    if (status == HAL_OK)
    {
        printf("Flash programmed successfully at address 0x%08lX\r\n", base_address + offset);
    }
}

void processReceivedPackage(package_t* pkg)
{
    switch(pkg->header.cmd) {
    case CMD_START_UPDATE:
        printf("[BOOT] >>> Processing CMD_START_UPDATE\r\n");
        // Handle start update command
        break;
    case CMD_VERSION:
        printf("[BOOT] >>> Processing CMD_VERSION\r\n");
        // Handle version command
        break;
    case CMD_ERASE:
        printf("[BOOT] >>> Processing CMD_ERASE\r\n");
        // Handle erase command
        erase_flash_sector(5);
        break;
    case CMD_WRITE:
        printf("[BOOT] >>> Processing CMD_WRITE\r\n");
        // Handle write command
        program_flash_data(APP_START_ADDRESS, pkg->flash_offset, &pkg->data[0], pkg->header.length - 4);
        break;
    case CMD_FINISH:
        printf("[BOOT] >>> Processing CMD_FINISH\r\n");
        // Handle finish command
        break;
    case CMD_RESET:
        printf("[BOOT] >>> Processing CMD_RESET\r\n");
        // Handle reset command
        JumpToApplication();
        break;
    default:
        printf("[BOOT] >>> Unknown command received: 0x%02X\r\n", pkg->header.cmd);
        break;
    }
}

void SWUpdatePackageHandler_ReceiveFrame(UART_HandleTypeDef *huart)
{
    switch(uartRxState) {
    case UART_RX_STATE_START:
        printf("[BOOT] >>> Start byte received: 0x%02X\r\n", rxStart);
        if(rxStart == 0xA5) 
        {
            HAL_UART_Transmit(huart, (uint8_t*)&ACK_BYTE, 1, HAL_MAX_DELAY);
            uartRxState = UART_RX_STATE_CMD;
            HAL_UART_Receive_IT(huart, &rxCmd, 1);
        }
        else 
        {
            HAL_UART_Transmit(huart, (uint8_t*)&NACK_BYTE, 1, HAL_MAX_DELAY);
            uartRxState = UART_RX_STATE_START;
            HAL_UART_Receive_IT(huart, &rxStart, 1);
        }
        break;
    case UART_RX_STATE_CMD:
        printf("[BOOT] >>> Command byte received: 0x%02X\r\n", rxCmd);
        HAL_UART_Transmit(huart, (uint8_t*)&ACK_BYTE, 1, HAL_MAX_DELAY);
        uartRxState = UART_RX_STATE_LENGTH;
        HAL_UART_Receive_IT(huart, rxLength, 2);
        break;
    case UART_RX_STATE_LENGTH:
        printf("[BOOT] >>> Length bytes received: %d, %d\r\n", rxLength[0], rxLength[1]);
        HAL_UART_Transmit(huart, (uint8_t*)&ACK_BYTE, 1, HAL_MAX_DELAY);
        rxDataLen = rxLength[0] | (rxLength[1] << 8);

        if(rxDataLen > 4 && rxDataLen <= PACKAGE_SIZE + 4) 
        {
            uartRxState = UART_RX_STATE_DATA;
            HAL_UART_Receive_IT(huart, rxData, rxDataLen);
        } 
        else 
        {
            uartRxState = UART_RX_STATE_CRC;
            HAL_UART_Receive_IT(huart, rxCrc, 2);
        }
        break;
    case UART_RX_STATE_DATA:
        printf("[BOOT] >>> Data bytes received: %d bytes\r\n", rxDataLen);
        HAL_UART_Transmit(huart, (uint8_t*)&ACK_BYTE, 1, HAL_MAX_DELAY);
        uartRxState = UART_RX_STATE_CRC;
        HAL_UART_Receive_IT(huart, rxCrc, 2);
        break;
    case UART_RX_STATE_CRC:
        printf("[BOOT] >>> CRC bytes received: 0x%02X 0x%02X\r\n", rxCrc[0], rxCrc[1]);
        HAL_UART_Transmit(huart, (uint8_t*)&ACK_BYTE, 1, HAL_MAX_DELAY);
        uartRxState = UART_RX_STATE_STOP;
        HAL_UART_Receive_IT(huart, &rxStop, 1);
        break;
    case UART_RX_STATE_STOP:
        printf("[BOOT] >>> Stop byte received: 0x%02X\r\n", rxStop);
        HAL_UART_Transmit(huart, (uint8_t*)&ACK_BYTE, 1, HAL_MAX_DELAY);
        // Packet completed, copy to struct
        receivedPackage.header.start = rxStart;
        receivedPackage.header.cmd = rxCmd;
        receivedPackage.header.length = rxDataLen;
        if (rxDataLen >= 4)
        {
            receivedPackage.packet_index = rxData[0] | (rxData[1]<< 8);
            receivedPackage.flash_offset = rxData[2] | (rxData[3] << 8);
            memcpy(receivedPackage.data, &rxData[4], rxDataLen - 4);
        }
        else
        {
            receivedPackage.packet_index = 0;
            receivedPackage.flash_offset = 0;
            memset(receivedPackage.data, 0, sizeof(receivedPackage.data));
        }
        receivedPackage.crc16 = (rxCrc[0] << 8) | rxCrc[1];
        receivedPackage.stop = rxStop;

        // Packet processing
        if(receivedPackage.header.start == 0xA5 && receivedPackage.stop == 0x5A)
        {
            printf("[BOOT] >>> Package received: CMD=0x%02X, LENGTH=%d\r\n", receivedPackage.header.cmd, receivedPackage.header.length);
            // ...command processing switch/case...
            processReceivedPackage(&receivedPackage);

            uartRxState = UART_RX_STATE_START;
            HAL_UART_Receive_IT(huart, &rxStart, 1);
        } 
        else 
        {
            printf("[BOOT] >>> Invalid Package Received.\r\n");
            uartRxState = UART_RX_STATE_START;
            HAL_UART_Receive_IT(huart, &rxStart, 1);
        }
        // Return to start for next packet
        uartRxState = UART_RX_STATE_START;
        HAL_UART_Receive_IT(huart, &rxStart, 1);
        break;
    }
}
