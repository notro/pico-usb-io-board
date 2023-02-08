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
    printf("Usage: gpio_pulse gpio polarity\n");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        usage();
        return 1;
    }

    int gpio = to_int(argv[1]);
    if (gpio < 0) {
        usage();
        return 1;
    }

    int polarity = to_int(argv[2]);
    if (polarity != 0 && polarity != 1) {
        usage();
        return 1;
    }

//    printf("gpio=%d polarity=%d\n", gpio, polarity);

    int result = setup();
    if (result != SETUP_OK) {
        fprintf(stderr, "setup() failed: result=%d\n", result);
        return 1;
    }

    output_gpio(gpio, polarity);
    output_gpio(gpio, !polarity);

    cleanup();

    return 0;
}
