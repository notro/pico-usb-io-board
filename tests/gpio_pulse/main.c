#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "c_gpio.h"

static int to_int(const char *s)
{
    char *t;
    long l = strtol(s, &t, 10);
    if(s == t)
        return -1;
    return l;
}

static void usage(void)
{
    printf("Usage: gpio_pulse gpio\n");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage();
        return 1;
    }

    int gpio = to_int(argv[1]);
    if (gpio < 0) {
        usage();
        return 1;
    }

//    printf("gpio=%d\n", gpio);

    int result = setup();
    if (result != SETUP_OK) {
        fprintf(stderr, "setup() failed: result=%d\n", result);
        return 1;
    }

//    // The input pin on the Pico is pulled high so start high to avoid generating an extra interrupt
//    output_gpio(gpio, 1);
//    setup_gpio(gpio, OUTPUT, PUD_OFF);

    output_gpio(gpio, 0);
    output_gpio(gpio, 1);

//    setup_gpio(gpio, INPUT, PUD_OFF);
    cleanup();

    return 0;
}
