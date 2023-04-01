/* project */
#include "BLE.h"
#include "Button.h"
#include "Display.h"
#include "FuelGauge.h"
#include "GUI.h"
#include "Time.h"
#include "Ws2812b.h"

/* max32655 + cordio */
#include <max32655.h>
#include <wsf_os.h>

int main(void) {
    WS2812B_init();
    WS2812B_SetColor(0, 0, 20, 0);
    WS2812B_Transmit();

    BLE_Init();
    Time_Init();
    Button_Init();
    Display_Init();
    FuelGauge_Init();
    GUI_Init();

    WsfOsEnterMainLoop();
    __BKPT();
    return 0;
}
