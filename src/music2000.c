#include "music2000.h"
#include "acia.h"
#include "config.h"
#include "sound.h"

#define M2000_MAP_SIZE  128     // one entry per even address &FC00-&FCFE
#define M2000_MAP_NONE  0xff

typedef enum {
    MS_GROUND,
    MS_ONE_OF_ONE,
    MS_ONE_OF_TWO,
    MS_TWO_OF_TWO,
    MS_SYSEX
} midi_state_t;

typedef struct {
    midi_dev_t   *dev;
    midi_state_t state;
    uint8_t buffer[4];
    uint8_t rtmsg;
    bool    enabled;
    uint16_t base;              // even address in FRED, &FC00-&FCFE
} m2000_dev_t;

static m2000_dev_t m2000_out[MIDI_NUM_OUTPUTS];

// Address to instance lookup, indexed by (addr & 0xff) >> 1.
static uint8_t m2000_map[M2000_MAP_SIZE];

static void acia_tx(ACIA *acia, uint8_t data) {
    m2000_dev_t *m2000 = acia->udata;

    if (data & 0x80) {               // status byte
        switch(data >> 4) {
            case 0x8: // note off
            case 0x9: // note on
            case 0xa: // polyphonic pressure
            case 0xb: // control change
            case 0xe: // pitch bend
                m2000->buffer[0] = data;
                m2000->state = MS_ONE_OF_TWO;
                break;
            case 0xc: // program change
            case 0xd: // channel pressure
                m2000->buffer[0] = data;
                m2000->state = MS_ONE_OF_ONE;
                break;
            case 0xf:
                if (data & 0x8) { // realtime message.
                    m2000->rtmsg = data;
                    midi_send_msg(m2000->dev, &m2000->rtmsg, 1);
                } else {
                    m2000->buffer[0] = data;
                    switch (data & 0x0f) {
                        case 0xf1: // time code quarter frame
                        case 0xf3: // song select
                            m2000->state = MS_ONE_OF_ONE;
                            break;
                        case 0xf2: // song position
                            m2000->state = MS_ONE_OF_TWO;
                            break;
                        case 0xf4: // undefined.
                        case 0xf5: // undefined.
                        case 0xf6: // tune request
                            midi_send_msg(m2000->dev, m2000->buffer, 1);
                            break;
                        case 0xf0:
                            m2000->state = MS_SYSEX;
                            break;
                        case 0xf7:
                            m2000->state = MS_GROUND;
                    }
                }
        }
    } else {
        switch(m2000->state) {
            case MS_GROUND: // data with no previous status.
            case MS_SYSEX:  // SysEx also ignore for now.
                break;
            case MS_ONE_OF_ONE:
                m2000->buffer[1] = data;
                midi_send_msg(m2000->dev, m2000->buffer, 2);
                break;
            case MS_ONE_OF_TWO:
                m2000->buffer[1] = data;
                m2000->state = MS_TWO_OF_TWO;
                break;
            case MS_TWO_OF_TWO:
                m2000->buffer[2] = data;
                midi_send_msg(m2000->dev, m2000->buffer, 3);
                m2000->state = MS_ONE_OF_TWO;
        }
    }
}

#define M2000_ACIA(n) { \
    .tx_hook = acia_tx, \
    .udata   = &m2000_out[n - 1], \
    .intnum  = 0x100 * n, \
    .name    = "M2000:" #n \
}

static ACIA music2000_acia[] = {
    M2000_ACIA(1),
    M2000_ACIA(2),
    M2000_ACIA(3)
};

_Static_assert(sizeof(music2000_acia) / sizeof(music2000_acia[0]) == MIDI_NUM_OUTPUTS, "one ACIA per MIDI output");

static void music2000_build_map(void) {
    memset(m2000_map, M2000_MAP_NONE, sizeof m2000_map);
    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++) {
        if (m2000_out[i].enabled) {
            unsigned idx = (m2000_out[i].base & 0xff) >> 1;
            if (m2000_map[idx] == M2000_MAP_NONE)
                m2000_map[idx] = i;
            else
                log_warn("music2000: out%d base &%04X already used by out%d, out%d ignored", i + 1, m2000_out[i].base, m2000_map[idx] + 1, i + 1);
        }
    }
}

uint8_t music2000_read(uint32_t addr) {
    uint8_t inst = m2000_map[(addr & 0xff) >> 1];

    if (inst < MIDI_NUM_OUTPUTS)
        return acia_read(&music2000_acia[inst], addr);
    return 0xff;
}

void music2000_write(uint32_t addr, uint8_t val) {
    uint8_t inst = m2000_map[(addr & 0xff) >> 1];

    if (inst < MIDI_NUM_OUTPUTS)
        acia_write(&music2000_acia[inst], addr, val);
}

void music2000_poll(void) {
    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++)
        if (m2000_out[i].enabled)
            acia_poll(&music2000_acia[i]);
}

void music2000_set_output(int num, midi_dev_t *dev) {
    if (num >= 0 && num < MIDI_NUM_OUTPUTS)
        m2000_out[num].dev = dev;
}

bool music2000_enabled(int num) {
    return num >= 0 && num < MIDI_NUM_OUTPUTS && m2000_out[num].enabled;
}

void music2000_set_enabled(int num, bool enabled) {
    if (num >= 0 && num < MIDI_NUM_OUTPUTS) {
        m2000_out[num].enabled = enabled;
        music2000_build_map();
    }
}

uint16_t music2000_get_base(int num) {
    if (num >= 0 && num < MIDI_NUM_OUTPUTS)
        return m2000_out[num].base;
    return 0;
}

void music2000_load_config(void) {
    char key[64];

    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++) {
        unsigned base;

        snprintf(key, sizeof key, "music2000_out%d_enabled", i + 1);
        // If not configured yet, follow the Music 5000 setting so existing setups keep working.
        m2000_out[i].enabled = get_config_bool("midi", key, sound_music5000);
        snprintf(key, sizeof key, "music2000_out%d_base", i + 1);
        base = get_config_int("midi", key, 0xfc08 + 2 * i);
        m2000_out[i].base = 0xfc00 | (base & 0xfe);
        if (m2000_out[i].base != base)
            log_warn("music2000: out%d base &%04X is not an even address in &FC00-&FCFE, using &%04X", i + 1, base, m2000_out[i].base);
    }
    music2000_build_map();
}

void music2000_save_config(void) {
    char key[64];

    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++) {
        snprintf(key, sizeof key, "music2000_out%d_enabled", i + 1);
        set_config_bool("midi", key, m2000_out[i].enabled);
        snprintf(key, sizeof key, "music2000_out%d_base", i + 1);
        set_config_hex("midi", key, m2000_out[i].base);
    }
}
