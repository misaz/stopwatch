/* project */
#include "BLE.h"
#include "Display.h"

/* sdtlib */
#include <string.h>

/* max32655 + mbed + cordio */
#include <wsf_os.h>

int main(void) {
    BLE_Init();
    Display_Init();

    WsfOsEnterMainLoop();

    return 0;
}
