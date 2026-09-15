#include <assert.h>

#include "app_startup.h"

int main(void) {
    assert(app_startup_initial_demo() == APP_STARTUP_PASSPORT_SERVICE);
    return 0;
}
