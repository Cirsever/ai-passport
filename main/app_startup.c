#include "app_startup.h"

app_startup_demo_t app_startup_initial_demo(void) {
    /* Slice F Physical Skills MVP boots straight into the Passport Service
     * page so the Wear/Compose flow can be exercised over USB Serial/JTAG. */
    return APP_STARTUP_PASSPORT_SERVICE;
}
