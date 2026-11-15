#ifndef __MUSIC_2000_H
#define __MUSIC_2000_H

#include "b-em.h"
#include "acia.h"
#include "midi.h"

typedef enum {
    MUSIC2000_INTERRUPT_NONE,
    MUSIC2000_INTERRUPT_IRQ,
    MUSIC2000_INTERRUPT_NMI
} music2000_interrupt_t;

uint8_t music2000_read(uint32_t addr);
void music2000_write(uint32_t addr, uint8_t val);
void music2000_poll(void);
void music2000_set_output(int num, midi_dev_t *dev);
bool music2000_enabled(int num);
void music2000_set_enabled(int num, bool enabled);
music2000_interrupt_t music2000_get_interrupt(int num);
void music2000_set_interrupt(int num, music2000_interrupt_t route);
uint16_t music2000_get_base(int num);
void music2000_load_config(void);
void music2000_save_config(void);

#endif
