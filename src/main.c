#include "CH58x_common.h"

// NOTE: Initialisation for UART0 and __write() redirection for printf()
#include "debug.h"

// NOTE: USB descriptors and definitions:
#include "usb_defs.h"
#include "usb_descriptors.h"



// --- Global Variables (Adapted for CH582M) ---
// Extern declarations for USB DMA pointers (defined in SDK's CH58x_usbdev.c)
extern uint8_t *pEP0_RAM_Addr;
extern uint8_t *pEP1_RAM_Addr;

uint8_t DevConfig, Ready;
uint8_t SetupReqCode;
uint16_t SetupReqLen;
const uint8_t *pDescr;

// EP0 setup packet buffer (Needed as pSetupReqPak is usually an alias to this)
__attribute__((aligned(4))) uint8_t UsbSetupBuf[8];
#define pSetupReqPak ((USB_SETUP_REQ *)UsbSetupBuf)

// User-allocated RAM (The CH582M has fewer EPs, so we simplify)
__attribute__((aligned(4))) uint8_t EP0_Databuf[64]; // EP0
__attribute__((aligned(4))) uint8_t EP1_Databuf[64 + 64]; // EP1 OUT (64) + IN (64) - USB requires 64-byte buffers
uint8_t HIDInOutData[DevEP0SIZE] = { 0 }; // Unused, but keep for completeness
#define EP1_TX_Buf (EP1_Databuf + 64)  // TX buffer for EP1 is at offset +64 (IN buffer)

// --- Helper Functions and Macros ---

// Define the LED pin as PB4
#define LED_PIN GPIO_Pin_4


// --- Your Original Variables ---
#define TOUCH_THRES 140
#define TOUCH_BASE_SAMPLES 8
const uint8_t tkey_ch[] = { 5, 2, 4 };
const uint8_t key_map[] = { 0x50, 0x52, 0x4F }; // A, B, C
#define NUM_KEYS (sizeof(tkey_ch)/sizeof(tkey_ch[0]))
uint16_t base_cal[NUM_KEYS] = {0};
uint8_t KeyBuf[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // Working buffer for keyboard data - fully initialized

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
    GPIOA_ModeCfg(GPIO_Pin_12 | GPIO_Pin_14 | GPIO_Pin_15, GPIO_ModeIN_Floating);

    TouchKey_ChSampInit();

    // Initial Calibration
    mDelaymS(100);
    for(int k=0; k<NUM_KEYS; k++) {
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

    DebugInit();

    // LED Init
    GPIOB_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);

    // USB Init
    pEP0_RAM_Addr = EP0_Databuf; // Map EP0 data buffer
    pEP1_RAM_Addr = EP1_TX_Buf; // Map EP1 TX buffer to properly aligned buffer

    // Initialize USB hardware
    USB_DeviceInit();

    // Enable USB Interrupt
    PFIC_EnableIRQ(USB_IRQn);

    #ifdef DEBUG_MODE
    // Verify DMA pointers are set correctly
    printf("\n\n=== USB DMA POINTER VERIFICATION ===\n");
    printf("EP1_TX_Buf address: 0x%08X\n", (uint32_t)EP1_TX_Buf);
    printf("R16_UEP1_DMA value: 0x%04X\n", R16_UEP1_DMA);
    printf("pEP1_RAM_Addr:      0x%08X\n", (uint32_t)pEP1_RAM_Addr);

    if ((uint32_t)EP1_TX_Buf != (uint32_t)pEP1_RAM_Addr) {
        printf("!!! WARNING: EP1_TX_Buf != pEP1_RAM_Addr !!!\n");
    }
    if (R16_UEP1_DMA != ((uint16_t)(uint32_t)EP1_TX_Buf)) {
        printf("!!! WARNING: R16_UEP1_DMA doesn't point to EP1_TX_Buf !!!\n");
    }
    #endif //DEBUG_MODE

    Touch_Setup();

    // Wait for USB enumeration to complete and send initial "all keys up" report
    mDelaymS(500);
    printf("\n=== USB ENUMERATION COMPLETE ===\n");
    printf("Sending initial 'all keys up' report...\n");

    // Send initial empty report to clear any garbage state on host
    memset(KeyBuf, 0, 8);
    memcpy(EP1_TX_Buf, KeyBuf, 8);
    R8_UEP1_T_LEN = 8;
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~UEP_T_RES_MASK) | UEP_T_RES_ACK;
    mDelaymS(20); // Give time for transmission

    printf("Begin MainLoop\n\n");

    int flag_did_trasmit = 0;

        if (flag_did_trasmit){
            #ifdef DEBUG_MODE
            printf("\n\nUSB Transmitt occured!\n--------------------------------\n");
            #endif //DEBUG_MODE
            flag_did_trasmit = 0;
        }

        mDelaymS(10);
    while(1) {
        uint8_t current_pressed = 0;
        static uint8_t last_pressed = 0;

        for(int i=0; i<NUM_KEYS; i++) {
            uint16_t val = TouchKey_Get(tkey_ch[i]);

            #ifdef DEBUG_MODE
            // Print the raw values for each channel
            printf("CH%d -)) Base=[ %d ], Current=[ %d ], Diff=[ %d ]\n",
                tkey_ch[i],
                base_cal[i],
                val,
                (base_cal[i] - val));
            #endif //DEBUG_MODE

            if (val < (base_cal[i] - TOUCH_THRES)) {
                current_pressed = key_map[i];
                if (last_pressed == 0) {
                    GPIOB_InverseBits(LED_PIN);
                }
                break; // Only register one key press at a time
            }
        }

        // HID Keyboard Logic: send key change immediately
        if (current_pressed != last_pressed) {
            #ifdef DEBUG_MODE
            printf("\n=== KEY STATE CHANGE ===\n");
            printf("Last: 0x%02X, Current: 0x%02X\n", last_pressed, current_pressed);
            #endif //DEBUG_MODE

            // Build report: NO MODIFIERS, reserved=0, keycode in slot 0 (KeyBuf[2])
            // CRITICAL: Clear the entire buffer first to prevent garbage/ALT modifier issues
            memset(KeyBuf, 0, 8);

            KeyBuf[0] = 0;  // NO modifiers (bit 0=LCtrl, bit 1=LShift, bit 2=LAlt, bit 3=LGui, etc.)
            KeyBuf[1] = 0;  // Reserved
            KeyBuf[2] = current_pressed;  // First key slot (0 = no key pressed)
            // Remaining slots already zeroed by memset

            #ifdef DEBUG_MODE
            printf("KeyBuf prepared: [%02X %02X %02X %02X %02X %02X %02X %02X]\n",
                KeyBuf[0], KeyBuf[1], KeyBuf[2], KeyBuf[3],
                KeyBuf[4], KeyBuf[5], KeyBuf[6], KeyBuf[7]);
            #endif //DEBUG_MODE

            // Send when EP1 is ready (T endpoint result == NAK -> ready to load/send)
            if ((R8_UEP1_CTRL & UEP_T_RES_MASK) == UEP_T_RES_NAK) {
                // Copy to EP1_TX_Buf (which now correctly points to IN buffer at offset +64)
                memcpy(EP1_TX_Buf, KeyBuf, 8);

                DevEP1_IN_Transmit(8);
                flag_did_trasmit = 1;

                #ifdef DEBUG_MODE
                printf(">>> TRANSMISSION INITIATED <<<\n");
                #endif //DEBUG_MODE
            } else {
                #ifdef DEBUG_MODE
                printf("!!! EP1 NOT READY (not NAK) !!!\n");
                #endif //DEBUG_MODE
            }
            last_pressed = current_pressed;
        }

        if (flag_did_trasmit){
            #ifdef DEBUG_MODE
            printf("\n\nUSB Transmitt occured!\n--------------------------------\n");
            #endif //DEBUG_MODE
            flag_did_trasmit = 0;
        }

        mDelaymS(10);
    }
}
