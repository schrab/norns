#pragma once

// WM8731 codec controls for the Fates shield, set through the kernel ALSA
// ctl interface (card sndrpiproto) so the driver's register cache stays
// coherent -- unlike the raw i2cset in /etc/rc.local. No-ops where the
// card is absent (stock norns uses i2c.h / the TPA6130A2 instead).

extern void alsa_ctl_init(void);

// "Master Playback Volume" (LOUT1V/ROUT1V), 0-127. Feeds the line outs and
// the headphone driver alike; the codec has no second volume register.
extern void alsa_ctl_set_volume(int level);

// "Input Mux" enum: 0 = Line In, 1 = Mic.
extern void alsa_ctl_set_input_mux(int mux);
