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
    BLE_Init();
    Time_Init();
    Button_Init();
    Display_Init();
    WS2812B_init();
    FuelGauge_Init();
    GUI_Init();

    WsfOsEnterMainLoop();
    __BKPT();
    return 0;
}
