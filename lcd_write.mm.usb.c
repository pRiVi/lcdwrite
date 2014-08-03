/*
Copyright (C) 2008 [Markus Mueller: (lcdwrite[AT]priv.de)]

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details. */

#include <sys/types.h>
#include <sys/gpio.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <regex.h>

#define _PATH_DEV_GPIO  "/dev/ugen0.00"

int main (int argc, char *argv[]) {
   FILE *f;
   f = fopen(_PATH_DEV_GPIO, "r");
   if (f <= 0) {
     printf("Cannot open for reading: %s\n", strerror(errno));
     exit(1);
   }
   while (1) printf("A:%d:\n", fgetc(f));
}
