#include "b-em.h"
#include "config.h"
#include "midi.h"
#include "sound.h"
#include "music2000.h"
#include "music4000.h"

#include <windows.h>

static const char *szMusic4000InDevName;
static HMIDIIN hMusic4000MidiIn;

struct _midi_dev {
    const char *szName;
    HMIDIOUT hMidiOut;
};

midi_dev_t Music2000Out[MIDI_NUM_OUTPUTS];

static inline int MidiOutNum(midi_dev_t *dev) {
    return (int)(dev - Music2000Out) + 1;
}

static bool MidiOutNameInUse(const char *szName) {
    for (int j = 0; j < MIDI_NUM_OUTPUTS; j++)
        if (Music2000Out[j].szName && strcmp(szName, Music2000Out[j].szName) == 0)
            return true;
    return false;
}

void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2) {

    if (wMsg == MIM_DATA) {
        switch(dwParam1 & 0xf0) {
            case 0x80:
                music4000_note_off((dwParam1 >> 8) & 0xff, (dwParam1 >> 16) & 0xff);
                break;
            case 0x90:
                music4000_note_on((dwParam1 >> 8) & 0xff, (dwParam1 >> 16) & 0xff);
                break;
        }
    }
}

static void MidiOpenInInternal(UINT nMidiDevice) {
    MMRESULT rv;

    if ((rv = midiInOpen(&hMusic4000MidiIn, nMidiDevice, (DWORD_PTR)(void*)MidiInProc, 0, CALLBACK_FUNCTION)) == MMSYSERR_NOERROR) {
        log_info("midi-windows: starting MIDI in device #%d, %s", nMidiDevice, szMusic4000InDevName);
        midiInStart(hMusic4000MidiIn);
    }
    else
        log_error("midi-windows: unable to open MIDI device #%d, %s, rv=%d", nMidiDevice, szMusic4000InDevName, rv);
}

static void MidiOpenM400Dev(void) {
    UINT nMidiDevices, i;
    MIDIINCAPS caps;

    if ((nMidiDevices = midiInGetNumDevs()) > 0) {
        if (szMusic4000InDevName) {
            for (i = 0; i < nMidiDevices; i++) {
                midiInGetDevCaps(i, &caps, sizeof(MIDIINCAPS));
                if (strcasecmp(caps.szPname, szMusic4000InDevName) == 0) {
                    MidiOpenInInternal(i);
                    return;
                }
            }
            log_error("midi-windows: no MIDI device with name '%s'", szMusic4000InDevName);
        }
        midiInGetDevCaps(0, &caps, sizeof(MIDIINCAPS));
        szMusic4000InDevName = strdup(caps.szPname);
        MidiOpenInInternal(0);
    } else
        log_warn("midi-windows: no MIDI input devices available for M4000");
}

static void MidiOpenOutInternal(UINT nMidiDevice, midi_dev_t *dev) {
    MMRESULT rv;

    if ((rv = midiOutOpen(&dev->hMidiOut, nMidiDevice, 0, 0, CALLBACK_NULL)) == MMSYSERR_NOERROR)
        log_info("midi-windows: opened MIDI out device #%d, %s for MIDI Out %d", nMidiDevice, dev->szName, MidiOutNum(dev));
    else
        log_warn("midi-windows: unable to open MIDI device #%d, %s for MIDI Out %d, rv=%d", nMidiDevice, dev->szName, MidiOutNum(dev), rv);
}

static void MidiOpenM2000Devs(void) {
    UINT nMidiDevices, i;
    MIDIOUTCAPS caps;
    int j;

    if ((nMidiDevices = midiOutGetNumDevs()) > 0) {
        log_debug("midi-windows: found %d midi out devices", nMidiDevices);

        // First pass: open the devices named in the config.
        for (i = 0; i < nMidiDevices; i++) {
            midiOutGetDevCaps(i, &caps, sizeof(MIDIOUTCAPS));
            log_debug("midi-windows: device #%d has name %s", i, caps.szPname);
            for (j = 0; j < MIDI_NUM_OUTPUTS; j++) {
                midi_dev_t *dev = &Music2000Out[j];
                if (music2000_enabled(j) && dev->szName && !dev->hMidiOut && strcmp(caps.szPname, dev->szName) == 0) {
                    MidiOpenOutInternal(i, dev);
                    log_debug("midi-windows: assigned #%d to out%d on name", i, j + 1);
                    break;
                }
            }
        }

        // Second pass: give any enabled outputs still unopened the remaining devices in order.
        for (i = 0; i < nMidiDevices; i++) {
            midiOutGetDevCaps(i, &caps, sizeof(MIDIOUTCAPS));
            if (!MidiOutNameInUse(caps.szPname)) {
                for (j = 0; j < MIDI_NUM_OUTPUTS && (Music2000Out[j].hMidiOut || !music2000_enabled(j)); j++)
                    ;
                if (j == MIDI_NUM_OUTPUTS)
                    break;
                Music2000Out[j].szName = strdup(caps.szPname);
                MidiOpenOutInternal(i, &Music2000Out[j]);
                log_debug("midi-windows: assigned #%d to out%d by number", i, j + 1);
            }
        }
    } else
        log_warn("midi-windows: no MIDI output devices available for M2000");
}

void midi_init(void) {
    MidiOpenM400Dev();
    MidiOpenM2000Devs();
    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++)
        music2000_set_output(i, &Music2000Out[i]);
}

void midi_close(void) {
    if (hMusic4000MidiIn != NULL)
        midiInClose(hMusic4000MidiIn);
    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++)
        if (Music2000Out[i].hMidiOut != NULL)
            midiOutClose(Music2000Out[i].hMidiOut);
}

void midi_load_config(void) {
    const char *ptr;
    char key[64];

    if ((ptr = get_config_string("midi", "music4000_in_device",   NULL)))
        szMusic4000InDevName = strdup(ptr);
    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++) {
        snprintf(key, sizeof key, "music2000_out%d_device", i + 1);
        if ((ptr = get_config_string("midi", key, NULL)))
            Music2000Out[i].szName = strdup(ptr);
    }
};

void midi_save_config(void) {
    char key[64];

    if (szMusic4000InDevName)
        set_config_string("midi", "music4000_in_device", szMusic4000InDevName);
    for (int i = 0; i < MIDI_NUM_OUTPUTS; i++) {
        if (Music2000Out[i].szName) {
            snprintf(key, sizeof key, "music2000_out%d_device", i + 1);
            set_config_string("midi", key, Music2000Out[i].szName);
        }
    }
}

void midi_send_msg(midi_dev_t *dev, uint8_t *msg, size_t size) {
    DWORD value = msg[0] | (msg[1] << 8) | (msg[2] << 16);
    MMRESULT res;

    log_debug("midi-windows: midi_send_msg(Music 2000 Out %d, %.*s, %d)", MidiOutNum(dev), (int)size, msg, (int)size);
    if ((res = midiOutShortMsg(dev->hMidiOut, value)) != MMSYSERR_NOERROR)
        log_error("midi-windows: unable to send MIDI event on %s: %d", dev->szName, res);
}
