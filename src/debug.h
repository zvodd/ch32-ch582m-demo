#include "stdio.h" 
#include "CH58x_common.h" // Ensure this is included for the register definitions

// Function to redirect printf output to UART1
__attribute__((used)) 
int _write(int fd, char *buf, int size) {
    int i;
    for (i = 0; i < size; i++) {
        // 1. Wait until the Transmit FIFO is NOT full
        // R8_UART1_TFC is the Transmitter FIFO Count (how many bytes are waiting to be sent)
        while (R8_UART1_TFC == UART_FIFO_SIZE); 
        
        // 2. Write the byte to the Transmit Holding Register (THR)
        R8_UART1_THR = *buf++; // Use the correct THR register
    }
    return size;
}


void DebugInit(void)
{
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
}