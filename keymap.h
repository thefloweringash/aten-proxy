#ifndef KEYMAP_H
#define KEYMAP_H

#include <rfb/keysym.h>

void keymap_init();
uint8_t keymap_usageForKeysym(rfbKeySym c);

#endif
