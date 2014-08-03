/*
Copyright (C) 2008 [Markus Mueller: (lcdwrite[AT]priv.de)]

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

This program allows you to access a HD44780 LCD Display via
4 GPIO pins, as the soekris boxes have is. It allows you
to give a multilevel path of settings, which the user can
interactively select with 4 buttons also connected to
further 4 GPIO pins. */

#include <sys/types.h>
#include <sys/gpio.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <regex.h>

#define _PATH_DEV_GPIO  "/dev/gpio1"

#define ARRAY_LEN(x) (sizeof(x) / sizeof ((x) [0]))

/* Tastenbelegung */
const taste1 = 4;
const taste2 = 5;
const taste3 = 10;
const taste4 = 11;

/* LDC Belegung */
const data3 = 16;
const data2 = 17;
const data1 = 18;
const data0 = 19;
const rs    = 20;
const rw    = 21;
const en    = 22;
const curms = 2;

// Displayausstattung
const LINELEN = 40;      // Wieviele Zeichen pro Zeile sind im BUFFER ?
const LINELENSHOW = 16;  // Wieviele Zeichen pro Zeile sind sichtbar?
const LINECOUNT = 2;     // Wieviele Zeilen haben wir (egal ob sichtbar oder nicht!)
const LINECOUNTSHOW = 1; // Wieviele Zeilen sind sichtbar?

// Wie der Cursor blinkt...
const CURSORBLINKON = 150;
const CURSORBLINKOFF = 300;

// Handlerevents
const INIT = -2;
const REPRINT = -1;

/* Funktionsbelegung */
#define runter taste2
#define rauf taste1
#define links taste4
#define rechts taste3

#define HANDLERDEF(x)     struct screendef* (x)(int,       struct screendef*)
#define HANDLERFUNCDEF(x) struct screendef* (x)(int taste, struct screendef* oldscreen)

#define INITSTRUCT(type, name) struct type* name; name = malloc(sizeof(*name)); bzero(name, sizeof(*name));

char *device = _PATH_DEV_GPIO;
int devfd = -1;

void     waitms(int);
void     pinwrite(int, int);
void     pinmode(int, int);
void     trans4bit(int, int, int, int);
void     trans8bit(int, int, int, int, int, int, int, int);
void     cmd(int, int, int, int, int, int, int, int, int);
void     lcd2line();
void     lcd1line();
void     writeChar(char);
void     writeString (char*,int,int);
void     initLCD(int, int);
void     initTasten();
int      pinread(int);
int      timeChanged(long*, int);
long     getTime();
int      potenz(int, int);
void     moveCursor(int);
HANDLERFUNCDEF(edithandler);

struct menulist {
   int pos;
   int anz;
   char *text;
   HANDLERDEF(*handler);
   struct menulist *parent;
   struct menuentry** entries;
};

struct menuentry {
   char *text;
   int type;
   int cursorpos;
   void *value;
   int allowStayOnKey;
   struct menulist *childmenu;
};

/* menuentry->type */
const NOTHING = 0;
const MENU = 1;
const EDITABLE_IP_VALUE = 2;
const EDITABLE_TEXT_VALUE = 3;
const MAX_TEXT_VALUE = 255;
const RUN_COMMAND = 4;

int reinitMS = 5000;
int pollMS = 10;

struct screendef {
   char* show;
   int cursorposition;
   int nocursorblink;
   int allowStayOnKey;
   signed int needupdate;
   struct menulist* menu;
};

void cmd(int a, int b, int c, int d, int e, int f, int g, int h, int nosleep) {
   pinwrite(rs, 0);
   trans8bit(a,b,c,d,e,f,g,h);
   if (nosleep == 0) waitms(curms);
}

void cursorHome() {
/*    # 0,0,0,0,0,0,1,* -> Cursor home
      # Setzt den Cursor auf Adresse 0 und bringt eine gescrollte Displayanzeige
      # zurück auf 0. Der RAM-Inhalt bleibt dabei unverändert. */
      cmd(0,0,0,0,0,0,0,1,0);
}

void clearDisplay() {
/*    # 0,0,0,0,0,0,0,1 -> Clear display
      # Clear display: Löscht das Display und setzt den Cursor zurück auf Adresse 0 */
      cmd(0,0,0,0,0,0,1,1,0);
}

struct screendef* handlerinit(struct screendef* oldscreen) {
   /* Neue Screendefinition erzeugen. */
   /* ToDo: Man sollte malloc-Fehler abfangen. */
   struct screendef* screen = malloc(sizeof(*screen));
   bzero(screen, sizeof(*screen));

   /* Vom alten übernehmen was wir brauchen. */
   (*screen).menu = (*oldscreen).menu;

   /* Alte Screendefinition freigeben. */
   if ((int) (*oldscreen).show != 0) free((*oldscreen).show);
   free(oldscreen);

   return screen;
}

struct screendef* updateMenu(
   struct screendef** oldscreen,
   struct menulist* newMenu)
{
   /* Wir verweisen nur, wenn wir nicht selbst bereits verwiesen
      werden. Ansonsten haetten wir endlos-loops, und dann gibts
      Probleme, unteranderem beim Anzeigen der stexts für die
      History (IP->Adresse->Mask->...) */
   int foundmyself = 0;
   struct menulist *tmpMenu = (**oldscreen).menu;
   while ((*tmpMenu).parent != NULL) {
      if ((*tmpMenu).parent == newMenu) {
         foundmyself = 1;
         break;
      }
      tmpMenu = (*tmpMenu).parent;
   }
   if (foundmyself == 0) (*newMenu).parent = (**oldscreen).menu;
   (**oldscreen).menu = newMenu;
   *oldscreen = (*(**oldscreen).menu).handler(REPRINT, *oldscreen);
   return *oldscreen;
}

void *initValue(struct menuentry *curentry) {
   if ((*curentry).value == NULL) {
      if ((*curentry).type == EDITABLE_IP_VALUE) {
         (*curentry).value = malloc(4);
         bzero((*curentry).value, 4);
         unsigned char *ip = (*curentry).value;
         *ip = 255;
         *(ip+1) = 255;
         *(ip+2) = 255;
         *(ip+3) = 0;
      } else if  ((*curentry).type == EDITABLE_TEXT_VALUE) {
         (*curentry).value = malloc(MAX_TEXT_VALUE);
         bzero((*curentry).value, MAX_TEXT_VALUE);
      }
      (*curentry).cursorpos = -1;
   }
   if ((*curentry).type == MENU) (*curentry).cursorpos = -1;
   return (*curentry).value;
}

void cutstring(char *text) {
   int max = 0;
   int j = 0;
   for(j=0;j<=strlen(text);j++) { if (((*(text+j)) != ' ') && ((*(text+j)) != 0)) max = j; };
   for(j=max+1;j<=strlen(text);j++) { (*(text+j)) = 0; }
}

HANDLERFUNCDEF(menuhandler) {
   struct menulist *parent = (*oldscreen).menu;
   struct screendef *screen = handlerinit(oldscreen);
   struct menulist *menu = (*screen).menu;

   struct menuentry *curentry = (*menu).entries[(*menu).pos];
   (*screen).allowStayOnKey = curentry->allowStayOnKey;

   // Initialisieren

   if (taste == INIT) {
      /* Wir wurden das erste mal aufgerufen, und sollten
         uns initialisieren. Hm... gibts eigentlich nicht viel
         zu tun. Tu wa halt mal das Menü zurückstellen. :-) */
      (*menu).pos = 0;

   // Nur aktualisieren

   } else if (taste == REPRINT) {
      /* Wir aktualisieren nur, das passiert unten sowieso jedesmal,
         also haben wir hier nix zu tun. */

   // Taste NACH UNTEN

   } else if (taste == runter) {
      initValue(curentry);
      if ((*curentry).cursorpos >= 0) {
         if ((*curentry).type == EDITABLE_IP_VALUE) {
            unsigned char *ip = initValue(curentry);
            unsigned char *curip = ip + ((*curentry).cursorpos / 3);
            signed int newvalue = *curip - potenz(10, (2 - (*curentry).cursorpos % 3));
            if (newvalue >= 0) *curip = newvalue;
         } else if ((*curentry).type == EDITABLE_TEXT_VALUE) {
            int j = 0;
            char *text = initValue(curentry);
            for(j=0;j<(*curentry).cursorpos;j++) { if (*(text+j) == 0) *(text+j) = ' '; }
            if ((*(text+(*curentry).cursorpos)) == 0) {
               (*(text+(*curentry).cursorpos)) = '9';
            } else if ((*(text+(*curentry).cursorpos)) == 'a') {
               (*(text+(*curentry).cursorpos)) = ' ';
            } else if ((*(text+(*curentry).cursorpos)) == ' ') {
               (*(text+(*curentry).cursorpos)) = '9';
            } else if ((*(text+(*curentry).cursorpos)) == '0') {
                (*(text+(*curentry).cursorpos)) = 'Z';
            } else if ((*(text+(*curentry).cursorpos)) == 'A') {
                (*(text+(*curentry).cursorpos)) = 'z';
            } else {
               (*(text+(*curentry).cursorpos))--;
            }
            cutstring(text);
         } else {
            printf("ERROR: Unhandled runter key!\n");
         }
      } else {
         if ((*menu).pos < ((*menu).anz-1)) (*menu).pos++;
         curentry = (*menu).entries[(*menu).pos];
      }

   // Taste NACH OBEN

   } else if (taste == rauf) {
      initValue(curentry);
      if ((*curentry).cursorpos >=0) {
         if ((*curentry).type == EDITABLE_IP_VALUE) {
            unsigned char *ip = initValue(curentry);
            unsigned char *curip = ip + ((*curentry).cursorpos / 3);
            signed int newvalue = *curip + potenz(10, (2 - (*curentry).cursorpos % 3));
            if (newvalue < potenz(2, 8)) *curip = newvalue;
         } else if ((*curentry).type == EDITABLE_TEXT_VALUE) {
            int j = 0;
            char *text = initValue(curentry);
            for(j=0;j<(*curentry).cursorpos;j++) { if (*(text+j) == 0) *(text+j) = ' '; }
            if ((*(text+(*curentry).cursorpos)) == 0) {
               (*(text+(*curentry).cursorpos)) = 'a';
            } else if ((*(text+(*curentry).cursorpos)) == 'z') {
               (*(text+(*curentry).cursorpos)) = 'A';
            } else if ((*(text+(*curentry).cursorpos)) == 'Z') {
                (*(text+(*curentry).cursorpos)) = '0';
            } else if ((*(text+(*curentry).cursorpos)) == '9') {
                (*(text+(*curentry).cursorpos)) = ' ';
            } else if ((*(text+(*curentry).cursorpos)) == ' ') {
                (*(text+(*curentry).cursorpos)) = 'a';
            } else {
               (*(text+(*curentry).cursorpos))++;
            }
            cutstring(text);
         } else {
            printf("ERROR: Unhandled rauf key!\n");
         }
      } else {
         if ((*menu).pos > 0) (*menu).pos--;
         curentry = (*menu).entries[(*menu).pos];
      }

   // Taste RECHTS

   } else if (taste == rechts) {
      if ((*curentry).type == EDITABLE_IP_VALUE) {
         if ((*curentry).cursorpos < ((4*3)-1)) {
            (*curentry).cursorpos++;
         } else {
            (*curentry).cursorpos = -1;
         }
      } else if ((*curentry).type == EDITABLE_TEXT_VALUE) {
         if ((*curentry).cursorpos < (MAX_TEXT_VALUE)) {
            (*curentry).cursorpos++;
         } else {
            (*curentry).cursorpos = -1;
         }
      } else if ((*curentry).type == MENU) {
         struct menulist *menup = initValue(curentry);
         if (menup != NULL) {
            return updateMenu(&screen, menup);
         } else {
            printf("menuhandler: WARNING: User tried to get deeper than deepest menu level!\n");
         }
      } else {
         printf("menuhandler: WARNING: Unhandled right key!\n");
      }

   // Taste LINKS

   } else if (taste == links) {
      if ((((*curentry).type == EDITABLE_IP_VALUE) ||
           ((*curentry).type == EDITABLE_TEXT_VALUE)) && ((*curentry).cursorpos >=0)) {
         (*curentry).cursorpos--;
      } else {
         if ((*menu).parent != NULL) {
            return updateMenu(&screen, (*menu).parent);
         } else {
            char *newstring = "No higher level!";
            (*screen).show = malloc(strlen(newstring)+1);
            strncpy((*screen).show, newstring, (strlen(newstring)+1) );
            (*screen).needupdate = 200; // In 200 ms (oder so, je nachdem wie pollMS aussieht)
                                        // werden wir nochmal aufgerufen. :-)
            return screen;
         }
      }
   } else {
      printf("menuhandler: WARNING: Unhandled keycode %d.\n", taste);
   }

   // Was immer gemacht werden muss, also auch ohne eine Taste

   char *newstring = (*curentry).text;
   (*screen).show = malloc(strlen(newstring)+1);
   strncpy((*screen).show, newstring, (strlen(newstring)+1) );

   struct menulist *tmpMenu = menu;
   while (tmpMenu != NULL) {
      if (((*tmpMenu).text != NULL) &&
          (strlen((*tmpMenu).text))) {
             addToString(&((*screen).show), (*tmpMenu).text, "%s>%s", 0);
          }
      tmpMenu = (*tmpMenu).parent;
   }
   if ((*curentry).type == EDITABLE_IP_VALUE) {
      unsigned char *ip = initValue(curentry);
      addToString(&((*screen).show), ":", "%s%s", 1);
      if ((*curentry).cursorpos >= 0) {
         int pointjump = (int) ((*curentry).cursorpos / 3);
         (*screen).cursorposition = strlen((*screen).show)+1+((*curentry).cursorpos+pointjump);
      } else {
         (*screen).cursorposition = 0;
      }
      int i = 0;
      for (i = 0; i<=3; i++) {
         int j = 1;
         if (i) addToString(&((*screen).show), ".", "%s%s", 1);
         for (j=1;j<=2;j++) if (*(ip+i) < potenz(10, j)) addToString(&((*screen).show), "0", "%s%s", 1);
         addToString(&((*screen).show), *(ip+i), "%s%u", 1);
      }
   } else if ((*curentry).type == EDITABLE_TEXT_VALUE) {
      unsigned char *text = initValue(curentry);
      addToString(&((*screen).show), ":", "%s%s", 1);
      if ((*curentry).cursorpos >= 0) (*screen).cursorposition = strlen((*screen).show)+1+((*curentry).cursorpos);
      addToString(&((*screen).show), text, "%s%s", 1);
   } else {
      initValue(curentry);
   }
   return screen;
}

int potenz(int base, int exponent) {
   if (exponent == 0) return 1;
   int result = base;
   for(exponent--; exponent > 0; exponent--) result *= base;        /* Potenz x^k */
   return result;
}

int addToString(char **dest, void *add, char *format, int direction) {
   char *newstring;
   if (direction) {
      asprintf(&newstring, format, *dest, add);
   } else {
      asprintf(&newstring, format, add, *dest);
   }
   free(*dest);
   *dest = newstring;
}

/* static char* BASEMENU[] = {
   "IP-Adresse",
   "Reboot",
   "Version",
   "Statusinformation",
   "Letzte Logmeldung"
}; */

struct menulist *readConfig {
   pattern = "^([^.,=]+)(\.([^.=]+))*\s*\=\s*([^.,=]+)(\,([^.=]+)){2}$";
   if (regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB) != 0)  {
      printf("Error compiling regexp pattern :%s:\n", pattern);
      exit(1);
   }
   status = regexec(&re, string, (size_t) 0, NULL, 0);
   regfree(&re);
   if (status != 0)  {
           return(0) ;              /* report error */
   }
   return(1);
}

int main (int argc, char *argv[]) {
   if ((devfd = open(device, O_RDWR)) == -1) {
      err(1, "%s", device);
   }
   initLCD(1,1);
   initTasten();

   INITSTRUCT(screendef, screen);

   INITSTRUCT(menulist, menu);
   INITSTRUCT(menuentry, ip);
   INITSTRUCT(menuentry, sysinfo);
   INITSTRUCT(menulist, ipmenu);
   INITSTRUCT(menuentry, ipaddr);
   INITSTRUCT(menuentry, ipmask);
   INITSTRUCT(menuentry, ipname);

   /* Mainmenu */
   struct menuentry* blubber[255];
   (*menu).entries = blubber;
   (*menu).handler = menuhandler;

   (*ip).text = "IP Zuweisung";
   (*ip).type = MENU;
   (*ip).value = ipmenu;
   blubber[(*menu).anz++] = ip;

   (*sysinfo).text = "Systeminfos";
   blubber[(*menu).anz++] = sysinfo;

   /* Submenu */
   struct menuentry* blubber2[100];
   (*ipmenu).entries = blubber2;
   (*ipmenu).handler = menuhandler;
   (*ipmenu).text = "IP";

   (*ipaddr).text = "Adresse";
   (*ipaddr).type = EDITABLE_IP_VALUE;
   blubber2[(*ipmenu).anz++] = ipaddr;
   (*ipmask).text = "Maske";
   (*ipmask).type = EDITABLE_IP_VALUE;
   blubber2[(*ipmenu).anz++] = ipmask;
   (*ipname).text = "Name";
   (*ipname).type = EDITABLE_TEXT_VALUE;
   (*ipname).allowStayOnKey = 50;
   blubber2[(*ipmenu).anz++] = ipname;


   /* Wir fangen an mit dem Hauptmenue. :-) */
   (*screen).menu = menu;

   int lastTaste = 0;
   int block = 0;
   int cursorposition = 0;
   signed int cursorWaitTime = 0;
   signed int displayResetWaitTime = 0;
   signed int keytime = 0;
   signed int sentKey = 0;
   int curState = 0;

   screen = screen->menu->handler(INIT, screen);
   int needreprint = 1;

   while (1) {
      waitms(pollMS);
      displayResetWaitTime -= pollMS;
      if (displayResetWaitTime <= 0) {
         initLCD(1,1);
         printf("Resetting Display...\n");
         needreprint = 1;
         displayResetWaitTime = reinitMS;
      }
      if ((*screen).cursorposition) {
         cursorWaitTime -= pollMS;
         if (cursorWaitTime <= 0) {
            curState = (*screen).nocursorblink ? 1 : curState ? 0 : 1;
            needreprint = 1;
            cursorWaitTime = curState ? CURSORBLINKON : CURSORBLINKOFF;
         }
      }
      int activeTaste = 0;
      int c = 0;
      if (pinread(taste1) == 0) {
         c++;
         activeTaste = taste1;
      }
      if (pinread(taste2) == 0) {
         c++;
         activeTaste = taste2;
      }
      if (pinread(taste3) == 0) {
         c++;
         activeTaste = taste3;
      }
      if (pinread(taste4) == 0) {
         c++;
         activeTaste = taste4;
      }
      if (c > 1) {
         lastTaste = 0; block = 1; continue;
      }
      if (activeTaste) {
         if (lastTaste && (lastTaste != activeTaste)) {
            lastTaste = 0; block = 1; continue;
         }
         if (block == 0) {
            lastTaste = activeTaste;
            if ((*screen).allowStayOnKey) {
               keytime -= pollMS;
               if (keytime <= 0) {
                  screen = screen->menu->handler(activeTaste, screen);
                  /* Wir haben mit dem Benutzer interagiert, ein Reset des
                     Displays ist im Moment nicht notwendig. Darum last
                     zurücksetzen. */
                  needreprint = 1;
                  sentKey = 1;
               }
            }
         }
         if (!needreprint) continue;
      } else {
         if (lastTaste) {
             screen = screen->menu->handler(lastTaste, screen);
             /* Wir haben mit dem Benutzer interagiert, ein Reset des
                Displays ist im Moment nicht notwendig. Darum last
                zurücksetzen. */
             needreprint = 1;
             lastTaste = 0;
         }
         // if (block) printf("Unblocked.\n");
         block = 0;
         sentKey = 0;
      }
      if ((*screen).needupdate) {
         (*screen).needupdate -= pollMS;
         if ((*screen).needupdate <= 0) {
            (*screen).needupdate = 0;
            screen = screen->menu->handler(REPRINT, screen);
            needreprint = 1;
         }
      }
      if (needreprint != 0) {
         writeString((*screen).show,(*screen).cursorposition,(curState));
         needreprint = 0;
         keytime = sentKey ? (*screen).allowStayOnKey : ((*screen).allowStayOnKey * 4);
         displayResetWaitTime = reinitMS;
      }
   }
}

void waitms(int ms) {
   struct timeval blubb;
   bzero(&blubb, sizeof(blubb));
   blubb.tv_sec = 0;
   blubb.tv_usec = ms;
   select(NULL,NULL,NULL,NULL,&blubb);
}

/* Diese Funktion ist boese, weil sie auch einen Reset des
   Displays ueberlebt. Dann ist alles verschoben. Aus diesem
   Grunde haben wir eine eigene Cursorimplementierung. */
/* ALSO NIEMALS DIESE FUNKTION AUFRUFEN!!! */
void moveCursor(int direction) {
/* # 0,0,0,1,x,x,*,* -> Cursor/display shift
   # Bestimmt, ob der Cursor verschoben oder das Display gescrollt werden
   # soll (DisplayMove) und in welcher Richtung das geschieht
   # (DisplayMoveDirection). Der RAM-Inhalt bleibt unverändert. */
   cmd(0,0,0,1,0,direction,0,0,1);
}

void initLCD(int full, int clear) {

   /* First set up right modes, which are for each pin one of...

      GPIO_PIN_INPUT      input direction
      GPIO_PIN_OUTPUT     output direction
      GPIO_PIN_INOUT      bi-directional
      GPIO_PIN_OPENDRAIN  open-drain output
      GPIO_PIN_PUSHPULL   push-pull output
      GPIO_PIN_TRISTATE   output disabled
      GPIO_PIN_PULLUP     internal pull-up enabled
   */

   pinmode(en, GPIO_PIN_PUSHPULL);
   pinmode(rs, GPIO_PIN_PUSHPULL);
   pinmode(rw, GPIO_PIN_PUSHPULL);
   pinwrite(en, 0);
   pinwrite(rs, 0);
   pinwrite(rw, 0);

   pinmode(data0, GPIO_PIN_PUSHPULL);
   pinmode(data1, GPIO_PIN_PUSHPULL);
   pinmode(data2, GPIO_PIN_PUSHPULL);
   pinmode(data3, GPIO_PIN_PUSHPULL);

   pinwrite(en, 0);
   pinwrite(rs, 0);
   pinwrite(rw, 0);

   int eightbitmode = 0; /* # 0 = 4 bit, 1 = 8 bit */
   int DisplayLines = 1; /* # 0 = 1 Zeile, 1 = 2 Zeilen */
   int SchriftArt = 0; /* # ...sofern unterstützt. */
   int DisplayAn = 1;
   int CursorAnzeigen = 0;
   int CursorBlink = 0;
   int CursorBewegung = 1;
   int CursorDisplayScroll = 0;

/* # 3 x 0,0,1,1(,*,*,*,*) -> RESET
   # Da wir die HighBits kontrollieren, koennen wir
   # auch hier schon reseten, und auf 4 Bit setzen
   # 0,0,1,x(,x,x,*,*) -> Function set */
   trans4bit(0,0,1,1);
   trans4bit(0,0,1,1);
   trans4bit(0,0,1,1);
/* # 0,0,1,x(,x,x,*,*) -> Function set
   # Setzt den Übertragungsmodus (eightbitmode), die Anzahl der
   # Displayzeilen (DisplayLines) und die Schriftart (SchriftArt),
   # sofern unterstützt. */
   trans4bit(0,0,1,eightbitmode);
   cmd(0,0,1,eightbitmode,DisplayLines,SchriftArt,0,0,1);
/* # 0,0,0,0,1,x,x,x -> Display On/Off control
   # Schaltet das Display ein (DisplayAn), den Cursor sichtbar
   # (CursorAnzeigen) und bestimmt, ob der Cursor blinken soll
   # (CursorBlink) */
   cmd(0,0,0,0,1,DisplayAn,CursorAnzeigen,CursorBlink,1);
/* # 0,0,0,0,0,1,x,x -> Entry mode set
   # Setzt die Cursor-Bewegungsrichtung(CursorBewegung), und ob
   # das Display mitscrollt (CursorDisplayScroll) */
/*   cmd(0,0,0,0,0,1,CursorBewegung,CursorDisplayScroll,0); */
   if (clear == 0) {
      clearDisplay;
   } else {
      cursorHome;
   }
   return;

   if (full == 0) return;

/* # Umlaute deklarieren
   # set CGRAM address1
   # Set the CG RAM address by sending an instruction byte of 64 */
   trans8bit(0,1,0,0,0,0,0,0);
/* # switch to data mode */
   pinwrite(rs, 1);
/* # define pattern Ä */
   trans8bit(0,0,0,0,1,0,1,0);
   trans8bit(0,0,0,0,0,1,0,0);
   trans8bit(0,0,0,0,1,0,1,0);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,1,1,1,1,1);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,0,0,0,0,0); /* # reserviert f. cursor */

/* # set CGRAM address2 */
   pinwrite(rs, 0);
/* # Set the CG RAM address by sending an instruction byte of 72 */
   trans8bit(0,1,0,0,1,0,0,0);

/* # switch to data mode */
   pinwrite(rs, 1);
/* # define pattern Ö */
   trans8bit(0,0,0,0,1,0,1,0);
   trans8bit(0,0,0,0,0,0,0,0);
   trans8bit(0,0,0,0,1,1,1,0);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,0,1,1,1,0);
   trans8bit(0,0,0,0,0,0,0,0);

/* # set CGRAM address3 */
   pinwrite(rs, 0);
/* # Set the CG RAM address by sending an instruction byte of 80 */
   trans8bit(0,1,0,1,0,0,0,0);

/* # switch to data mode */
   pinwrite(rs, 1);
   # define pattern ü
   trans8bit(0,0,0,0,0,0,0,0);
   trans8bit(0,0,0,0,1,0,1,0);
   trans8bit(0,0,0,0,0,0,0,0);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,1,0,0,0,1);
   trans8bit(0,0,0,1,0,0,1,1);
   trans8bit(0,0,0,0,1,1,0,1);
   trans8bit(0,0,0,0,0,0,0,0);

/* # set CGRAM address4 */
   pinwrite(rs, 0);
/* # Set the CG RAM address by sending an instruction byte of 88 */
   trans8bit(0,1,0,1,1,0,0,0);

/* # switch to data mode */
   pinwrite(rs, 1);
/* # define arrow_up | */
   trans8bit(0,0,0,0,0,1,0,0);
   trans8bit(0,0,0,0,1,1,1,0);
   trans8bit(0,0,0,1,0,1,0,1);
   trans8bit(0,0,0,0,0,1,0,0);
   trans8bit(0,0,0,0,0,1,0,0);
   trans8bit(0,0,0,0,0,1,0,0);
   trans8bit(0,0,0,0,0,1,0,0);
   trans8bit(0,0,0,0,0,0,0,0);
}

void initTasten() {
   /* First set up right modes, which are for each pin one of...

         GPIO_PIN_INPUT      input direction
         GPIO_PIN_OUTPUT     output direction
         GPIO_PIN_INOUT      bi-directional
         GPIO_PIN_OPENDRAIN  open-drain output
         GPIO_PIN_PUSHPULL   push-pull output
         GPIO_PIN_TRISTATE   output disabled
         GPIO_PIN_PULLUP     internal pull-up enabled
   */

   pinmode(taste1, GPIO_PIN_PUSHPULL);
   pinmode(taste2, GPIO_PIN_PUSHPULL);
   pinmode(taste3, GPIO_PIN_PUSHPULL);
   pinmode(taste4, GPIO_PIN_PUSHPULL);

/* # Hm... das folgende ist irgendwie schwachsinn. Wir lesen,
   # trotzdem geht es nur wenn eine 1 geschrieben ist. */
/* # Trotzdem gehts nur so!!! Also da lassen. */
   pinwrite(taste1, 1);
   pinwrite(taste2, 1);
   pinwrite(taste3, 1);
   pinwrite(taste4, 1);
}

void writeString (char *str, int cursorposistion, int showcursor) {
   int j;
   int strl = strlen(str);
   int k = 0;
   int l = 0;
   int numberOfPrintableChars = (LINELENSHOW * LINECOUNTSHOW);
   if (cursorposistion > numberOfPrintableChars) {
      k = cursorposistion - numberOfPrintableChars;
   }
   int c = 0;
   for (l=0;l<LINECOUNT;l++) for (j = 0; j<(LINELEN); j++) {
      c++;
      if ((j >= LINELENSHOW) || (l>=(LINECOUNTSHOW))) {
         writeChar(' ');
         continue;
      }
      if ((k+1) == cursorposistion) {
         if (showcursor) {
            writeChar('_');
         } else if (k < strl) {
            writeChar(str[k]);
         } else {
            writeChar(' ');
         }
      } else if ((strl > numberOfPrintableChars) && (c == numberOfPrintableChars)) {
         writeChar('>');
      } else if (k < strl) {
         writeChar(str[k]);
      } else {
         writeChar(' ');
      }
      k++;
   }
}

void writeChar(char mychar) {
   int k;
   int feld[129];
   for (k=128; k != 0; k=k>>1) {
      int bit;
      bit = (mychar & k) != 0 ? 1 : 0;
      feld[k] = bit;
   }
   // set pin 20 (rs) to data mode
   pinwrite(rs, 1);
   trans8bit(feld[128], feld[64], feld[32], feld[16], feld[8], feld[4], feld[2], feld[1]);
}

void trans8bit(a, b, c, d, e, f, g, h) {
    trans4bit(a, b, c, d);
    trans4bit(e, f, g ,h);
}

void trans4bit(a, b, c, d) {
    pinwrite(data0, a);
    pinwrite(data1, b);
    pinwrite(data2, c);
    pinwrite(data3, d);
    pinwrite(en, 1);
    pinwrite(en, 0);
}

void pinmode(pin, flag) {
   struct gpio_pin_ctl op;
   bzero(&op, sizeof(op));
   op.gp_pin = pin;
   op.gp_flags = flag;
   if (ioctl(devfd, GPIOPINCTL, &op) == -1) {
      err(1, "GPIOPINCTL");
   }
}

void pinwrite(pin, value) {
    struct gpio_pin_op op;
    bzero(&op, sizeof(op));
    op.gp_pin = pin;
    op.gp_value = (value == 0 ? GPIO_PIN_LOW : GPIO_PIN_HIGH);
    if (ioctl(devfd, GPIOPINWRITE, &op) == -1) {
        err(1, "GPIOPINWRITE");
   }
}

int pinread(int pin) {
   struct gpio_pin_op op;
   bzero(&op, sizeof(op));
   op.gp_pin = pin;

   if (ioctl(devfd, GPIOPINREAD, &op) == -1) {
       err(1, "GPIOPINWRITE");
   }
   return op.gp_value;
}

