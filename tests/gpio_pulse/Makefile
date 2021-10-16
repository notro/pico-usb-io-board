CC=gcc
CFLAGS=-I.
DEPS = c_gpio.h
OBJ = main.o c_gpio.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

gpio_pulse: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)
