/* project */
#include "BLE.h"
#include "Button.h"
#include "Display.h"
#include "GUI.h"
#include "Time.h"

/* max32655 + cordio */
#include <max32655.h>
#include <wsf_os.h>

int main(void) {
    BLE_Init();
    Time_Init();
    Button_Init();
    Display_Init();
    GUI_Init();

    WsfOsEnterMainLoop();
    __BKPT();
    return 0;
}
