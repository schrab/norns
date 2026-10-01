// alsa_ctl.cc
//
// WM8731 codec controls for the Fates shield, routed through the kernel
// ALSA ctl interface (card sndrpiproto). The kernel driver owns the codec,
// so its register cache stays coherent -- unlike the raw i2cset hack in
// /etc/rc.local, which this supersedes.

#include <alsa/asoundlib.h>
#include <stdio.h>

#include "alsa_ctl.h"

#define ALSA_CTL_CARD "hw:sndrpiproto"

static snd_ctl_t *ctl = NULL;

void alsa_ctl_init(void) {
    int err = snd_ctl_open(&ctl, ALSA_CTL_CARD, 0);
    if (err < 0) {
        fprintf(stderr, "alsa_ctl: %s not found (%s); codec controls disabled\n",
                ALSA_CTL_CARD, snd_strerror(err));
        ctl = NULL;
    }
}

static int alsa_ctl_set(const char *name, int enumerated, int value) {
    if (ctl == NULL) {
        return -1;
    }

    snd_ctl_elem_value_t *elem;
    snd_ctl_elem_value_alloca(&elem);
    snd_ctl_elem_value_set_interface(elem, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_value_set_name(elem, name);
    if (enumerated) {
        snd_ctl_elem_value_set_enumerated(elem, 0, value);
    } else {
        // Master Playback Volume is a stereo pair sharing one register value
        snd_ctl_elem_value_set_integer(elem, 0, value);
        snd_ctl_elem_value_set_integer(elem, 1, value);
    }

    int err = snd_ctl_elem_write(ctl, elem);
    if (err < 0) {
        fprintf(stderr, "alsa_ctl: failed to set %s (%s)\n", name, snd_strerror(err));
    }
    return err;
}

void alsa_ctl_set_volume(int level) {
    if (level < 0) {
        level = 0;
    } else if (level > 127) {
        level = 127;
    }
    alsa_ctl_set("Master Playback Volume", 0, level);
}

void alsa_ctl_set_input_mux(int mux) {
    alsa_ctl_set("Input Mux", 1, mux ? 1 : 0);
}
