#include "unity.h"

void app_main(void);
void setUp(void) {}
void tearDown(void) {}

int main(void) {
    app_main();
    return Unity.TestFailures == 0 ? 0 : 1;
}
