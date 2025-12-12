#include "CH58x_common.h"

// NOTE: We don't need #include "usb_desc.h" anymore
// because we are defining the descriptors and handlers in this file.

// ====================================================================
// === DESCRIPTOR AND EP0 FRAMEWORK DEFINITIONS (Adapted from Sample) ===
// ====================================================================

#define DevEP0SIZE 0x40 // Endpoint 0 max packet size (64 bytes)

// --- USB Descriptors (Adapted for CH582M) ---
// Note: We are switching back to a standard 8-byte Keyboard Report Descriptor.

// Device Descriptor
const uint8_t MyDevDescr[] = {
    0x12, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, DevEP0SIZE, 
    0x3D, 0x41, 0x0C, 0xFE, 0x00, 0x01, 0x01, 0x02, 
    0x03, 0x01 // bcdDevice, iManufacturer, iProduct, iSerialNumber, bNumConfigurations
}; 
// Configuration Descriptor (Keyboard with 1 endpoint)
const uint8_t MyCfgDescr[] = {  
    0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32, // Config Header
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00, // Interface 0 (HID)
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x2B, 0x00, // HID Descriptor (Report length 0x002B = 43 bytes in sample, but for KB it's 8)
                                                        // Using 0x0008 here for 8 byte keyboard report descriptor length
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A              // Endpoint 1 IN - Interrupt, 8 bytes, 10ms interval
};

// Standard HID Keyboard Report Descriptor (8-byte report)
const uint8_t MyHIDReportDescr[] = {
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x05, 0x07, // Usage Page (Key Codes)
    0x19, 0xE0, // Usage Minimum (224)
    0x29, 0xE7, // Usage Maximum (231)
    0x15, 0x00, // Logical Minimum (0)
    0x25, 0x01, // Logical Maximum (1)
    0x75, 0x01, // Report Size (1)
    0x95, 0x08, // Report Count (8)
    0x81, 0x02, // Input (Data, Variable, Absolute) - Modifier byte
    0x95, 0x01, // Report Count (1)
    0x75, 0x08, // Report Size (8)
    0x81, 0x01, // Input (Constant) - Reserved byte
    0x95, 0x06, // Report Count (6)
    0x75, 0x08, // Report Size (8)
    0x15, 0x00, // Logical Minimum (0)
    0x25, 0x65, // Logical Maximum (101)
    0x05, 0x07, // Usage Page (Key Codes)
    0x19, 0x00, // Usage Minimum (0)
    0x29, 0x65, // Usage Maximum (101)
    0x81, 0x00, // Input (Data, Array) - Key arrays (6 bytes)
    0xC0        // End Collection
};

// String Descriptors
const uint8_t MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 }; // Language 0x0409 (US English)
const uint8_t MyManuInfo[] = { 0x0E, 0x03, 'W', 0, 'C', 0, 'H', 0, '.', 0, 'C', 0, 'N', 0 }; // Manufacturer
const uint8_t MyProdInfo[] = { 0x0C, 0x03, 'C', 0, 'H', 0, '5', 0, '8', 0, 'x', 0 }; // Product

// --- Global Variables (Adapted for CH582M) ---
uint8_t DevConfig, Ready;
uint8_t SetupReqCode;
uint16_t SetupReqLen;
const uint8_t *pDescr;

// EP0 setup packet buffer (Needed as pSetupReqPak is usually an alias to this)
__attribute__((aligned(4))) uint8_t UsbSetupBuf[8]; 
#define pSetupReqPak ((USB_SETUP_REQ *)UsbSetupBuf) 

// User-allocated RAM (The CH582M has fewer EPs, so we simplify)
__attribute__((aligned(4))) uint8_t EP0_Databuf[64]; // EP0
__attribute__((aligned(4))) uint8_t EP1_Databuf[8 + 8]; // EP1 IN (8) + OUT (8) - We only need IN
uint8_t HIDInOutData[DevEP0SIZE] = { 0 }; // Unused, but keep for completeness

// --- Helper Functions and Macros ---

// Define the LED pin as PB4
#define LED_PIN GPIO_Pin_4
#ifndef UEP_T_RES_MASK
#define UEP_T_RES_MASK 0x03
#endif

// --- Your Original Variables ---
#define TOUCH_THRES 1000
#define TOUCH_BASE_SAMPLES 8
const uint8_t tkey_ch[] = { 3, 5, 7 }; 
const uint8_t key_map[] = { 0x04, 0x05, 0x06 }; // A, B, C
uint16_t base_cal[3] = {0};
uint8_t KeyBuf[8] = {0};

/**
 * Direct Register Implementation of Touch Reading
 */
uint16_t TouchKey_Get(uint8_t ch) {
    // 1. Select the ADC channel (TouchKey pins are shared with ADC channels)
    R8_ADC_CHANNEL = (ch & RB_ADC_CH_INX);
    // 2. Ensure TouchKey power is ON
    R8_TKEY_CFG |= RB_TKEY_PWR_ON;
    // 3. Start the TouchKey conversion
    R8_TKEY_CONVERT = RB_TKEY_START;
    // 4. Wait for completion flag
    while (!(R8_ADC_INT_FLAG & RB_ADC_IF_EOC));
    // 5. Return the result
    return (R16_ADC_DATA & RB_ADC_DATA);
}

/**
 * USB Endpoint 1 Transmit
 */
void DevEP1_IN_Transmit(uint16_t len) {
    R8_UEP1_T_LEN = len;
    // Clear response mask and set to ACK
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~UEP_T_RES_MASK) | UEP_T_RES_ACK;
}


// ====================================================================
// === CORE USB ENUMERATION HANDLER (USB_DevTransProcess) ===
// ====================================================================
// This function contains the logic you were missing.

void USB_DevTransProcess( void )
{
    uint8_t len, chtype;
    uint8_t intflag, errflag = 0;

    intflag = R8_USB_INT_FG;
    if ( intflag & RB_UIF_TRANSFER )
    {
        if ( ( R8_USB_INT_ST & MASK_UIS_TOKEN ) != MASK_UIS_TOKEN ) // Not an idle/SETUP state
        {
            switch ( R8_USB_INT_ST & ( MASK_UIS_TOKEN | MASK_UIS_ENDP ) )
            {
                // --- IN Token (Host is ready to receive data) ---
                case UIS_TOKEN_IN | 0: // Endpoint 0 IN
                    switch ( SetupReqCode )
                    {
                        case USB_GET_DESCRIPTOR:
                            len = SetupReqLen >= DevEP0SIZE ? DevEP0SIZE : SetupReqLen; 
                            memcpy( pEP0_RAM_Addr, pDescr, len ); /* Load upload data */
                            SetupReqLen -= len;
                            pDescr += len;
                            R8_UEP0_T_LEN = len;
                            R8_UEP0_CTRL ^= RB_UEP_T_TOG; // Toggle PID
                            break;
                        case USB_SET_ADDRESS:
                            R8_USB_DEV_AD = ( R8_USB_DEV_AD & RB_UDA_GP_BIT ) | SetupReqLen;
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                            break;
                        default:
                            R8_UEP0_T_LEN = 0; 
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                            break;
                    }
                    break;

                case UIS_TOKEN_OUT | 0: // Endpoint 0 OUT (Status stage)
                    // The joystick example had LED control here, but for now we just ACK/NAK status
                    R8_UEP0_T_LEN = 0;
                    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK; 
                    break;
                    
                // --- Your Keyboard Endpoint ---
                case UIS_TOKEN_IN | 1 : // Endpoint 1 IN
                    // Toggle the PID and set back to NAK after sending one packet
                    R8_UEP1_CTRL ^= RB_UEP_T_TOG; 
                    R8_UEP1_CTRL = ( R8_UEP1_CTRL & ~UEP_T_RES_MASK ) | UEP_T_RES_NAK;
                    break;
                // No need for Endpoint 1 OUT (unless you want LED feedback)
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER; // Clear Interrupt Flag
        }
        
        // --- SETUP Transaction on Endpoint 0 ---
        if ( R8_USB_INT_ST & RB_UIS_SETUP_ACT )
        {
            // Set EP0 to ACK both IN/OUT, and set PID to DATA1 for the next transfer
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
            
            // Copy setup packet from hardware buffer to UsbSetupBuf (pEP0_RAM_Addr is often mapped to UsbSetupBuf)
            memcpy(UsbSetupBuf, pEP0_RAM_Addr, 8);
            
            SetupReqLen = pSetupReqPak->wLength;
            SetupReqCode = pSetupReqPak->bRequest;
            chtype = pSetupReqPak->bRequestType;

            len = 0;
            errflag = 0;
            
            if ( ( chtype & USB_REQ_TYP_MASK ) == USB_REQ_TYP_STANDARD ) 
            {
                switch ( SetupReqCode )
                {
                    case USB_GET_DESCRIPTOR :
                    {
                        switch ( ( ( pSetupReqPak->wValue ) >> 8 ) )
                        {
                            case USB_DESCR_TYP_DEVICE : // Device
                                pDescr = MyDevDescr;
                                len = MyDevDescr[0];
                                break;
                            case USB_DESCR_TYP_CONFIG : // Configuration
                                pDescr = MyCfgDescr;
                                len = MyCfgDescr[2] + (MyCfgDescr[3] << 8); // Total length
                                break;
                            case USB_DESCR_TYP_REPORT : // HID Report
                                if ( ( ( pSetupReqPak->wIndex ) & 0xff ) == 0 ) // Interface 0 report
                                {
                                    pDescr = MyHIDReportDescr;
                                    len = sizeof( MyHIDReportDescr );
                                }
                                break;
                            case USB_DESCR_TYP_STRING : // String
                            {
                                switch ( ( pSetupReqPak->wValue ) & 0xff )
                                {
                                    case 1 : pDescr = MyManuInfo; len = MyManuInfo[0]; break;
                                    case 2 : pDescr = MyProdInfo; len = MyProdInfo[0]; break;
                                    case 0 : pDescr = MyLangDescr; len = MyLangDescr[0]; break;
                                    default : errflag = 0xFF; break;
                                }
                            }
                            break;
                            default :
                                errflag = 0xff;
                                break;
                        }
                        if ( SetupReqLen > len ) SetupReqLen = len; // Cap requested length
                        len = ( SetupReqLen >= DevEP0SIZE ) ? DevEP0SIZE : SetupReqLen;
                        memcpy( pEP0_RAM_Addr, pDescr, len );
                        pDescr += len;
                    }
                    break;
                    case USB_SET_ADDRESS :
                        SetupReqLen = ( pSetupReqPak->wValue ) & 0xff;
                        break;
                    case USB_SET_CONFIGURATION :
                        DevConfig = ( pSetupReqPak->wValue ) & 0xff;
                        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG; // Set EP1 for data transfer
                        break;
                    case USB_CLEAR_FEATURE :
                        // Endpoint 1 stall clear (required for robust USB)
                        if ( ( (pSetupReqPak->wIndex) & 0xff ) == 0x81 ) 
                            R8_UEP1_CTRL = ( R8_UEP1_CTRL & ~( RB_UEP_T_TOG | MASK_UEP_T_RES ) ) | UEP_T_RES_NAK;
                        break;
                    case USB_GET_INTERFACE :
                    case USB_GET_STATUS :
                    case USB_GET_CONFIGURATION :
                        // Simple requests are handled implicitly or with a short response
                        len = 0; 
                        break;
                    default :
                        errflag = 0xff;
                        break;
                }
            } else if ( ( chtype & USB_REQ_TYP_MASK ) == USB_REQ_TYP_CLASS ) {
                // HID Class requests (SET_IDLE, GET_REPORT, etc.)
                // The joystick sample's `case 0x09` and `case 0x0a` cover SET_REPORT/GET_REPORT (0x01) and SET_IDLE/GET_IDLE (0x0A) implicitly
                // For a simple HID, we can often just ACK these.
                len = 0;
            }

            if ( errflag == 0xff )
            {
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL; // STALL
            }
            else
            {
                // Prepare for the data stage (send data if IN request)
                if ( chtype & 0x80 ) len = ( SetupReqLen > DevEP0SIZE ) ? DevEP0SIZE : SetupReqLen;
                else len = 0; // OUT request (Status stage)
                
                R8_UEP0_T_LEN = len;
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
            }

            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }
    }
    // --- Bus Reset (Re-initialization) ---
    else if ( intflag & RB_UIF_BUS_RST )
    {
        R8_USB_DEV_AD = 0;
        // Reset all endpoints to ACK/NAK
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG; 
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }
    // --- Suspend ---
    else if ( intflag & RB_UIF_SUSPEND )
    {
        // Handle suspend/resume (optional)
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }
    else
    {
        R8_USB_INT_FG = intflag; // Clear any other flags
    }
}


// ====================================================================
// === INTERRUPT HANDLER (The function you originally called) ===
// ====================================================================

// NOTE: Using __HIGH_CODE for your USB_IRQHandler as in the original CH582 code.
__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void) {
    USB_DevTransProcess();
}


// ====================================================================
// === MAIN APPLICATION LOGIC (Your TouchKey Code) ===
// ====================================================================

void Touch_Setup() {
    GPIOA_ModeCfg(GPIO_Pin_10 | GPIO_Pin_12 | GPIO_Pin_14, GPIO_ModeIN_Floating);
    
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
    // Set system clock
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    
    // LED Init
    GPIOB_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);

    // USB Init
    pEP0_RAM_Addr = EP0_Databuf; // Map EP0 data buffer
    pEP1_RAM_Addr = KeyBuf; // Map KeyBuf as EP1 TX buffer (Direct access is easier)
    
    // Initialize USB hardware
    USB_DeviceInit(); 
    
    // Enable USB Interrupt
    PFIC_EnableIRQ(USB_IRQn);
    
    Touch_Setup();
    
    while(1) {
        uint8_t current_pressed = 0;
        static uint8_t last_pressed = 0;
        
        for(int i=0; i<3; i++) {
            uint16_t val = TouchKey_Get(tkey_ch[i]);
            
            if (val < (base_cal[i] - TOUCH_THRES)) {
                current_pressed = key_map[i];
                if (last_pressed == 0) {
                    GPIOB_InverseBits(LED_PIN);
                }
                break; // Only register one key press at a time
            }
        }

        // HID Keyboard Logic: Send Key Down, then Key Up when released
        if (current_pressed != last_pressed) {
            // KeyBuf is mapped to pEP1_RAM_Addr
            // KeyBuf[0] = Modifiers, KeyBuf[1] = Reserved, KeyBuf[2] = Key Code
            KeyBuf[2] = current_pressed; 
            
            // Check if EP1 is NAK (ready to receive new data)
            if ((R8_UEP1_CTRL & UEP_T_RES_MASK) == UEP_T_RES_NAK) {
                DevEP1_IN_Transmit(8);
            }
            last_pressed = current_pressed;
        }
        
        mDelaymS(10); 
    }
}