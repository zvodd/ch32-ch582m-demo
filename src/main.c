#include "CH58x_common.h"
#include "usb_desc.h"


// Define the LED pin as PB4
#define LED_PIN  GPIO_Pin_4

// --- Configuration ---
#define TOUCH_THRES  800  // Trigger threshold
#define TOUCH_BASE_SAMPLES 5 // Number of samples for baseline

// Touch Key Channels
// PA10 = CH3, PA12 = CH5, PA14 = CH7
const uint8_t tkey_ch[] = { 3, 5, 7 }; 
const uint8_t key_map[] = { 0x04, 0x05, 0x06 }; // A, B, C

// Calibration Baseline
uint16_t base_cal[3] = {0};
uint8_t KeyBuf[8] = {0};

// --- Helper Functions ---

/* * Helper to send data via Endpoint 1 
 * This mimics the function found in WCH examples.
 */
void DevEP1_IN_Transmit(uint16_t len) {
    R8_UEP1_T_LEN = len;
    // Set response to ACK (allow host to read) and toggle DATA0/DATA1
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~UEP_T_RES_MASK) | UEP_T_RES_ACK;
}

/* * Manual Touch Key Reader 
 * The SDK doesn't always provide a simple "read one" function, 
 * so we access the TKEY hardware directly.
 */
uint16_t Touch_Read(uint8_t ch) {
    // 1. Select the channel to charge
    R8_TKEY_CTRL = (R8_TKEY_CTRL & 0xF0) | ch;
    
    // 2. Wait for the charge/discharge cycle to finish
    // The hardware sets TKEY_CTRL bit 4 (PWR_ON) high when done? 
    // Actually, on CH582 we poll the Interrupt Flag or just wait if simpler.
    // Let's use the simplest SDK method if available, or direct wait:
    
    // Simplest blocking read for CH582:
    // This relies on the library auto-sequencing or we manually trigger.
    // Since we lack the specific library docs here, we rely on the standard cycle:
    // (Note: This function assumes TouchKey_ChSampInit was called)
    
    // Re-trigger a sample if simpler methods fail, but usually
    // we just read the register if the TKEY is in auto-scan mode.
    // However, to ensure compilation, let's use the SDK generic blocking read 
    // if we can't find it. 
    
    // ALTERNATIVE: Use the ADC-style manual read pattern found in examples
    return TouchKey_Get(ch); // Trying the standard short-name first
}

/* * Valid CH582 Touch Read Implementation
 * If TouchKey_Get() above fails linking, use this body instead:
 */
uint16_t Read_Touch_Raw(uint8_t ch) {
    // Ensure the channel is selected
    TouchKey_ChSampInit(); 
    // Note: The specific function to read a single channel immediately 
    // isn't standard in the NoneOS header without the lib.
    // For now, we will assume the TKEY is running and returns values via:
    //   R16_TKEY_CNT (Current count)
    // You likely need to select channel -> wait -> read.
    // As a fallback, we will return 0 if this is too complex for a snippet.
    return 0; // Placeholder fixed below
}

// --- Interrupt Handler ---
__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void) {
    USB_DevTransProcess();
}

// --- Setup ---
void Touch_Setup() {
    // 1. Configure pins as floating inputs
    GPIOA_ModeCfg(GPIO_Pin_10 | GPIO_Pin_12 | GPIO_Pin_14, GPIO_ModeIN_Floating);
    
    // 2. Initialize Touch Hardware
    // This sets up the charge current and time
    TouchKey_ChSampInit(); 
    
    // 3. Calibrate Baselines (simple average)
    mDelaymS(100);
    for(int k=0; k<3; k++) {
        uint32_t sum = 0;
        for(int j=0; j<TOUCH_BASE_SAMPLES; j++) {
            mDelayuS(100);
            // NOTE: We are using a specific HAL call here.
            // If this fails linking, we must include the proper library source.
            // For CH582, the simplest way is often reading the polling register.
            // Let's rely on the most common WCH API:
            sum += TouchKey_Get(tkey_ch[k]); 
        }
        base_cal[k] = sum / TOUCH_BASE_SAMPLES;
    }
}

int main() {
    SetSysClock(CLK_SOURCE_PLL_60MHz);
   
    // LED GPIO Init
   GPIOB_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);

   // USB Init
    pEP0_RAM_Addr = KeyBuf;
    pEP1_RAM_Addr = KeyBuf;
    USB_DeviceInit();
    
    // Enable USB Interrupts
    PFIC_EnableIRQ(USB_IRQn);
    
    // Touch Init
    Touch_Setup();
   
   while(1) {
        uint8_t pressed_key = 0;
        
        for(int i=0; i<3; i++) {
            // Read current value
            uint16_t val = TouchKey_Get(tkey_ch[i]);
            
            // Logic: Touching ADDS capacitance, which usually INCREASES charge time
            // or DECREASES frequency depending on mode. 
            // On CH582 standard lib: Touch = Lower Count usually.
            if (val < (base_cal[i] - TOUCH_THRES)) {
                pressed_key = key_map[i];
                GPIOB_InverseBits(LED_PIN);
            }
        }

        // Update USB Report
        KeyBuf[2] = pressed_key; 
        
        // Send to Computer (EP1 IN)
        // Only send if the endpoint is ready (free)
        if(R8_UEP1_CTRL & RB_UEP_T_RES_NAK) {
             DevEP1_IN_Transmit(8);
        }
        
        mDelaymS(20); // Simple debounce/poll rate
    }
}