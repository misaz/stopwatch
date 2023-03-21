/* project */
#include "BLE.h"

/* sdtlib */
#include <string.h>

/* max32655 + mbed + cordio */
#include <wsf_os.h>

int main(void) {
    BLE_Init();

    WsfOsEnterMainLoop();

    return 0;
}
