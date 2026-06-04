#ifndef WING_CONTROL_NAMES_H
#define WING_CONTROL_NAMES_H

struct wing_control_name {
    unsigned int id;
    const char *name;
};

const char *wing_csc_fader_name(unsigned int id);
const char *wing_csc_button_name(unsigned int id);
const char *wing_csc_potentiometer_name(unsigned int id);
const char *wing_csc_encoder_name(unsigned int id);
const char *wing_pnlc_button_name(unsigned int id);
const char *wing_pnlc_raw_name(unsigned int byte);

#endif
