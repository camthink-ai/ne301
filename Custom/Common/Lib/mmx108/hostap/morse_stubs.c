#include "utils/morse.h"
#include "mmosal.h"

int morse_raw_global_enable(const char *ifname, bool enable)
{
    (void)(ifname);

    if (enable)
    {
        mmosal_printf("Raw not supported yet\n");
        MMOSAL_DEV_ASSERT(false);
        return -1;
    }
    else
    {
        return 0;
    }
}

int morse_raw_priority_enable(const char *ifname,
                              bool enable,
                              u8 prio,
                              u32 start_time_us,
                              u32 duration_us,
                              u8 num_slots,
                              bool cross_slot,
                              u16 max_bcn_spread,
                              u16 nom_stas_per_bcn,
                              u8 praw_period,
                              u8 praw_start_offset)
{
    (void)(ifname);
    (void)(prio);
    (void)(start_time_us);
    (void)(duration_us);
    (void)(num_slots);
    (void)(cross_slot);
    (void)(max_bcn_spread);
    (void)(nom_stas_per_bcn);
    (void)(praw_period);
    (void)(praw_start_offset);

    if (enable)
    {
        mmosal_printf("Raw not supported yet\n");
        MMOSAL_DEV_ASSERT(false);
        return -1;
    }
    else
    {
        return 0;
    }
}
