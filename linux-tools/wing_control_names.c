#include "wing_control_names.h"

static const struct wing_control_name fader_names[] = {
    {0x00, "fader-1"},
    {0x01, "fader-2"},
    {0x02, "fader-3"},
    {0x03, "fader-4"},
    {0x04, "fader-5"},
    {0x05, "fader-6"},
    {0x06, "fader-7"},
    {0x07, "fader-8"},
    {0x08, "fader-9"},
    {0x09, "fader-10"},
    {0x0a, "fader-11"},
    {0x0b, "fader-12"},
    {0x0c, "master"},
};

static const struct wing_control_name button_names[] = {
    {0x00, "FADER_1_SELECT"},
    {0x01, "FADER_1_SOLO"},
    {0x02, "FADER_1_MUTE"},
    {0x03, "FADER_2_SELECT"},
    {0x04, "FADER_2_SOLO"},
    {0x05, "FADER_2_MUTE"},
    {0x06, "FADER_3_SELECT"},
    {0x07, "FADER_3_SOLO"},
    {0x08, "FADER_3_MUTE"},
    {0x09, "FADER_4_SELECT"},
    {0x0a, "FADER_4_SOLO"},
    {0x0b, "FADER_4_MUTE"},
    {0x0c, "FADER_5_SELECT"},
    {0x0d, "FADER_5_SOLO"},
    {0x0e, "FADER_5_MUTE"},
    {0x0f, "FADER_6_SELECT"},
    {0x10, "FADER_6_SOLO"},
    {0x11, "FADER_6_MUTE"},
    {0x12, "FADER_7_SELECT"},
    {0x13, "FADER_7_SOLO"},
    {0x14, "FADER_7_MUTE"},
    {0x15, "FADER_8_SELECT"},
    {0x16, "FADER_8_SOLO"},
    {0x17, "FADER_8_MUTE"},
    {0x18, "FADER_9_SELECT"},
    {0x19, "FADER_9_SOLO"},
    {0x1a, "FADER_9_MUTE"},
    {0x1b, "FADER_10_SELECT"},
    {0x1c, "FADER_10_SOLO"},
    {0x1d, "FADER_10_MUTE"},
    {0x1e, "FADER_11_SELECT"},
    {0x1f, "FADER_11_SOLO"},
    {0x20, "FADER_11_MUTE"},
    {0x21, "FADER_12_SELECT"},
    {0x22, "FADER_12_SOLO"},
    {0x23, "FADER_12_MUTE"},
    {0x24, "MASTER_SELECT"},
    {0x25, "MASTER_SOLO"},
    {0x26, "MASTER_MUTE"},
    {0x27, "VIEW Fader Layers"},
    {0x28, "1-12"},
    {0x29, "13-24"},
    {0x2a, "25-36"},
    {0x2b, "37-40 AUX IN"},
    {0x2c, "BUS MASTER"},
    {0x2d, "MAIN MATRIX"},
    {0x2e, "DCA"},
    {0x2f, "USER 1"},
    {0x30, "USER 2"},
    {0x31, "<4"},
    {0x32, "4>"},
    {0x33, "VIEW Custom Buttons"},
    {0x34, "BUS Custom Buttons Screen"},
    {0x35, "DCA Custom Buttons Screen"},
    {0x36, "MAIN/MATRIX Custom Buttons Screen"},
    {0x37, "USER Custom Buttons Screen"},
    {0x38, "Custom Button 1"},
    {0x39, "Custom Button 2"},
    {0x3a, "Custom Button 3"},
    {0x3b, "Custom Button 4"},
    {0x3c, "Custom Button 5"},
    {0x3d, "Custom Button 6"},
    {0x3e, "Custom Button 7"},
    {0x3f, "Custom Button 8"},
    {0x40, "Custom Button 9"},
    {0x41, "Custom Button 10"},
    {0x42, "Custom Button 11"},
    {0x43, "Custom Button 12"},
    {0x44, "Custom Button 13"},
    {0x45, "Custom Button 14"},
    {0x46, "Custom Button 15"},
    {0x47, "Custom Button 16"},
    {0x48, "VIEW USB Port"},
    {0x49, "TALK A"},
    {0x4a, "TALK B"},
    {0x4b, "DIM"},
    {0x4c, "MONO"},
};

static const struct wing_control_name potentiometer_names[] = {
    {0x00, "Talk Level"},
    {0x01, "Phones Level"},
    {0x02, "Monitor Level"},
};

static const struct wing_control_name encoder_names[] = {
    {0x00, "encoder-7"},
    {0x01, "encoder-1"},
    {0x02, "encoder-2"},
    {0x03, "encoder-3"},
    {0x04, "encoder-4"},
    {0x05, "encoder-5"},
    {0x06, "encoder-6"},
};

static const char *lookup_name(const struct wing_control_name *map, unsigned int len, unsigned int id)
{
    for (unsigned int i = 0; i < len; ++i)
    {
        if (map[i].id == id)
            return map[i].name;
    }
    return 0;
}

const char *wing_csc_fader_name(unsigned int id)
{
    return lookup_name(fader_names, sizeof(fader_names) / sizeof(fader_names[0]), id);
}

const char *wing_csc_button_name(unsigned int id)
{
    return lookup_name(button_names, sizeof(button_names) / sizeof(button_names[0]), id);
}

const char *wing_csc_potentiometer_name(unsigned int id)
{
    return lookup_name(potentiometer_names, sizeof(potentiometer_names) / sizeof(potentiometer_names[0]), id);
}

const char *wing_csc_encoder_name(unsigned int id)
{
    return lookup_name(encoder_names, sizeof(encoder_names) / sizeof(encoder_names[0]), id);
}

const char *wing_pnlc_button_name(unsigned int id)
{
    switch (id) {
        case 0: return "HOME";
        case 1: return "ROTARY_7";
        case 2: return "ROTARY_1";
        case 3: return "ROTARY_2";
        case 4: return "ROTARY_3";
        case 5: return "ROTARY_4";
        case 6: return "ROTARY_5";
        case 10: return "CLR_SOLO";
        case 11: return "SELECT";
        case 12: return "LIBRARY";
        case 13: return "SETUP";
        case 14: return "ROUTING";
        case 15: return "METERS";
        case 16: return "UTILITY";
        case 17: return "EFFECTS";
        case 18: return "ROTARY_6";
        default: return 0;
    }
}

const char *wing_pnlc_raw_name(unsigned int byte)
{
    switch (byte) {
        case 0xf8: return "SETUP";
        case 0x2c: return "UTILITY";
        case 0xac: return "LIBRARY";
        default: return 0;
    }
}
