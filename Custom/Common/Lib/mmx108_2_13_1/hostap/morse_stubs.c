#include "utils/morse.h"
#include "mmosal.h"

int morse_sta_configure_channelization(struct wpa_supplicant *wpa_s, char *country)
{
    (void)wpa_s;
    (void)country;
    return 0;
}

int morse_ap_configure_channelization(char *country, u8 op_class)
{
    (void)country;
    (void)op_class;
    return 0;
}
