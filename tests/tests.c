#include "tests.h"
#include <stdio.h>

int main(void)
{
    int testsuites = 1;
    int outcomes   = 0;

    printf("\n");
    outcomes += mqtt_tests();

    printf("\nTests summary: %d passed, %d failed\n", testsuites + outcomes,
           outcomes == 0 ? 0 : (outcomes * -1));

    return outcomes == 0 ? 0 : -1;
}
