#include "CH58x_common.h"

// Define the LED pin as PB4
#define LED_PIN  GPIO_Pin_4

int main() {
    /* 1. Set System Clock to 60MHz 
       The CH582 relies on the internal PLL for timing. */
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    /* 2. Configure PB4 as Push-Pull Output 
       We use 5mA drive strength which is plenty for a standard LED. */
    GPIOB_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);

    while (1) {
        /* 3. Toggle PB4 state */
        GPIOB_InverseBits(LED_PIN);

        /* 4. Wait 500ms 
           mDelaymS is a built-in HAL function for the CH58x series. */
        mDelaymS(1500);
    }
}