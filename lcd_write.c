/*
Copyright (C) 2008 [Markus Mueller: (lcdwrite[AT]priv.de)]

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

This program allows you to access a HD44780 LCD Display, simply
by allowing you to send the commands over the GPIO port. If you
need a abstracted tool which implements a menu, look at lcd_write_mm.c.
*/

//convert stream to bit for writing to display
#include <sys/types.h>
#include <sys/gpio.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define _PATH_DEV_GPIO  "/dev/gpio1"

char *device = _PATH_DEV_GPIO;
int devfd = -1;

void     pinwrite(int, int);
void     trans4bit(int, int, int, int);
void     lcd2line();
void     lcd1line();

int
main (int argc, char *argv[])
{
if ((devfd = open(device, O_RDWR)) == -1) {
                err(1, "%s", device);
}

// set pin 20 (rs) to data mode
int pin = 20, value = 1;
pinwrite(pin, value);

int feld[129];
int i;
for (i=1; i<argc; i++) {
    char *str = argv[i];
    int j;
    for (j=0; str[j] != 0; j++) {
    if (j==16) {  //nexte zeile
        lcd2line();
    }
    if (j==32) {
        lcd1line();
    }
    if (j==48) {
        lcd2line();
    }
        char mychar = str[j];
        int k;
        for (k=128; k != 0; k=k>>1) {
            int bit;
            bit = (mychar & k) != 0 ? 1 : 0;
            feld[k] = bit;
         }
         trans4bit(feld[128], feld[64], feld[32], feld[16]);
         trans4bit(feld[8], feld[4], feld[2], feld[1]);
   }
 }
 return (0);
}

void
trans4bit(a, b, c, d) {
    int pin; int value;
    pin=19; value=a;
    pinwrite(pin, value);
    pin=18; value=b;
    pinwrite(pin, value);
    pin=17; value=c;
    pinwrite(pin, value);
    pin=16; value=d;
    pinwrite(pin, value);
    pin=22; value=1;
    pinwrite(pin, value);
    pin=22; value=0;
    pinwrite(pin, value);
}
void
lcd1line() {
    int pin; int value;
    //rs=0
    pin = 20; value = 0;
    pinwrite(pin, value);
    pin = 19; value = 1;
    pinwrite(pin, value);
    pin = 18; value = 0;
    pinwrite(pin, value);
    pin = 17; value = 0;
    pinwrite(pin, value);
    pin = 16; value = 0;
    pinwrite(pin, value);
    pin = 22; value = 1;
    pinwrite(pin, value);
    pin = 22; value = 0;
    pinwrite(pin, value);
    pin = 19; value = 0;
    pinwrite(pin, value);
    pin = 18; value = 0;
    pinwrite(pin, value);
    pin = 17; value = 0;
    pinwrite(pin, value);
    pin = 16; value = 0;
    pinwrite(pin, value);
    pin = 22; value = 1;
    pinwrite(pin, value);
    pin = 22; value = 0;
    pinwrite(pin, value);
    pin = 20; value = 1;
    pinwrite(pin, value);
}
void
lcd2line() {
    int pin; int value;
    pin = 20; value = 0;
    pinwrite(pin, value);
    pin = 19; value = 1;
    pinwrite(pin, value);
    pin = 18; value = 1;
    pinwrite(pin, value);
    pin = 17; value = 0;
    pinwrite(pin, value);
    pin = 16; value = 0;
    pinwrite(pin, value);
    pin = 22; value = 1;
    pinwrite(pin, value);
    pin = 22; value = 0;
    pinwrite(pin, value);
    pin = 19; value = 0;
    pinwrite(pin, value);
    pin = 18; value = 0;
    pinwrite(pin, value);
    pin = 17; value = 0;
    pinwrite(pin, value);
    pin = 16; value = 0;
    pinwrite(pin, value);
    pin = 22; value = 1;
    pinwrite(pin, value);
    pin = 22; value = 0;
    pinwrite(pin, value);
    pin = 20; value = 1;
    pinwrite(pin, value);
}
void pinwrite(pin, value)
{
    struct gpio_pin_op op;
    bzero(&op, sizeof(op));
    op.gp_pin = pin;
    op.gp_value = (value == 0 ? GPIO_PIN_LOW : GPIO_PIN_HIGH);
    if (ioctl(devfd, GPIOPINWRITE, &op) == -1) {
        err(1, "GPIOPINWRITE");
   }
}
