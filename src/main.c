#include "CH58x_common.h"
#include "usb_desc.h"

// Define the LED pin as PB4
#define LED_PIN  GPIO_Pin_4

// --- Manual Macro Fixes for CH582 NoneOS-SDK ---
#ifndef UEP_T_RES_MASK
#define UEP_T_RES_MASK    0x03
#endif

// --- Configuration ---
#define TOUCH_THRES        1000  // Increase this if the LED flips too easily
#define TOUCH_BASE_SAMPLES 8     // More samples = more stable baseline

// PA10=CH3, PA12=CH5, PA14=CH7
const uint8_t tkey_ch[] = { 3, 5, 7 }; 
const uint8_t key_map[] = { 0x04, 0x05, 0x06 }; // A, B, C

uint16_t base_cal[3] = {0};
uint8_t KeyBuf[8] = {0};

// --- Helper Functions ---

/**
 * Direct Register Implementation of Touch Reading
 * This replaces the missing library function.
 */
uint16_t TouchKey_Get(uint8_t ch) {
    // 1. Select channel and Power On
    // R32_TKEY_CTRL bits 0-4 are channel, bit 24 is Power
    R32_TKEY_CTRL = (ch & 0x1F) | (1 << 24);

    // 2. Wait for completion flag
    // On CH582, we poll the TKEY Interrupt Flag register
    while (!(R8_TKEY_INT_FLAG & RB_TKEY_IF_END));

    // 3. Clear flag for next read
    R8_TKEY_INT_FLAG = RB_TKEY_IF_END;

    // 4. Return result (16-bit count)
    return R16_TKEY_CNT;
}

/**
 * USB Endpoint 1 Transmit
 */
void DevEP1_IN_Transmit(uint16_t len) {
    R8_UEP1_T_LEN = len;
    // Clear response mask and set to ACK
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~UEP_T_RES_MASK) | UEP_T_RES_ACK;
}

__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void) {
    USB_DevTransProcess();
}

void Touch_Setup() {
    GPIOA_ModeCfg(GPIO_Pin_10 | GPIO_Pin_12 | GPIO_Pin_14, GPIO_ModeIN_Floating);
    
    // Enable Touch Module
    TouchKey_ChSampInit(); 
    
    // Initial Calibration
    mDelaymS(100);
    for(int k=0; k<3; k++) {
        uint32_t sum = 0;
        for(int j=0; j<TOUCH_BASE_SAMPLES; j++) {
            sum += TouchKey_Get(tkey_ch[k]);
            mDelayuS(100);
        }
        base_cal[k] = sum / TOUCH_BASE_SAMPLES;
    }
}

int main() {
    SetSysClock(CLK_SOURCE_PLL_60MHz);
   
    // LED Init
    GPIOB_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);

    // USB Init
    pEP0_RAM_Addr = KeyBuf;
    pEP1_RAM_Addr = KeyBuf;
    USB_DeviceInit();
    PFIC_EnableIRQ(USB_IRQn);
    
    Touch_Setup();
   
    while(1) {
        uint8_t current_pressed = 0;
        static uint8_t last_pressed = 0;
        
        for(int i=0; i<3; i++) {
            uint16_t val = TouchKey_Get(tkey_ch[i]);
            
            // If current count is significantly lower than baseline, it's a touch
            if (val < (base_cal[i] - TOUCH_THRES)) {
                current_pressed = key_map[i];
                
                // Only flip LED and send USB when state changes (prevents spamming)
                if (last_pressed == 0) {
                    GPIOB_InverseBits(LED_PIN);
                }
            }
        }

        // HID Keyboard Logic: Send Key Down, then Key Up when released
        if (current_pressed != last_pressed) {
            KeyBuf[2] = current_pressed; 
            
            // Wait for USB Endpoint to be ready
            if ((R8_UEP1_CTRL & UEP_T_RES_MASK) == UEP_T_RES_NAK) {
                DevEP1_IN_Transmit(8);
            }
            last_pressed = current_pressed;
        }
        
        mDelaymS(10); 
    }
}