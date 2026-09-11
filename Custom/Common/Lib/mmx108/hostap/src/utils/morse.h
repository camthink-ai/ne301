/*
 * Copyright 2022 Morse Micro
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef MORSE_H
#define MORSE_H

#include "utils/includes.h"
#include "utils/common.h"
#include "ap/ap_config.h"
#include "ap/hostapd.h"

#define MORSE_S1G_RETURN_ERROR (-1)
#define MORSE_INVALID_CHANNEL (-2)
#define MORSE_SUCCESS (0)
/** The maximum number of country codes that can be assigned to an S1G class */
#define COUNTRY_CODE_MAX (2)
#define COUNTRY_CODE_LEN (2)
#define S1G_CHAN_ENABLED_FLAG(ch) (1LLU << (ch))
#define NUMBER_OF_BITS(x) (sizeof(x) * 8)

#define MIN_S1G_FREQ_KHZ	750000
#define MAX_S1G_FREQ_KHZ	950000

#define MORSE_JP_HT20_NON_OVERLAP_CHAN_OFFSET 12
#define MORSE_JP_HT20_NON_OVERLAP_CHAN_START 50
#define MORSE_JP_HT20_NON_OVERLAP_CHAN_END 60
#define MORSE_JP_S1G_NON_OVERLAP_CHAN 21

#define S1G_OP_CLASS_IE_LEN 3 /* eid + ie len + op class */
extern const unsigned int S1G_OP_CLASSES_LEN;

/* Define Maximum interfaces supported for MBSSID IE */
#define MBSSID_MAX_INTERFACES 2

#define MORSE_OUI	0x0CBF74

enum morse_vendor_events {
	MORSE_VENDOR_EVENT_BCN_VENDOR_IE_FOUND = 0, /* To be deprecated in a future version */
	MORSE_VENDOR_EVENT_OCS_DONE = 1,
	MORSE_VENDOR_EVENT_MGMT_VENDOR_IE_FOUND = 2,
	MORSE_VENDOR_EVENT_MESH_PEER_ADDR = 3
};

enum morse_vendor_attributes {
	MORSE_VENDOR_ATTR_DATA = 0,
	/* Bitmask of type @ref enum morse_vendor_ie_mgmt_type_flags */
	MORSE_VENDOR_ATTR_MGMT_FRAME_TYPE = 1,
};


struct morse_twt {
	u8 enable;
	u8 flow_id;
	u8 setup_command;
	u32 wake_duration_us;
	u64 wake_interval_us;
	u64 target_wake_time;
};

enum s1g_op_class_type {
	OP_CLASS_INVALID = -1,
	OP_CLASS_S1G_LOCAL = 1,
	OP_CLASS_S1G_GLOBAL = 0,
};

/* This table is also in the Morse Micro driver */
enum morse_dot11ah_region {
	MORSE_AU,
	MORSE_CA,
	MORSE_EU,
	MORSE_GB,
	MORSE_IN,
	MORSE_JP,
	MORSE_KR,
	MORSE_NZ,
	MORSE_SG,
	MORSE_US,
	REGION_UNSET = 0xFF,
};

/* Used to define buffer size when running a morse_cli command */
#define MORSE_CTRL_COMMAND_LENGTH (256)

/**
 * Return the pointer to the start of a container when a pointer within
 * the container is known
 */
#define container_of(ptr, type, member) ({\
	const typeof(((type *)0)->member)*__mptr = (const typeof(((type *)0)->member) *)(ptr); \
	(type *)((char *)__mptr - offsetof(type, member)); })

/* RAW limits */
#define MORSE_RAW_MAX_3BIT_SLOTS		(0b111)
#define MORSE_RAW_MIN_SLOT_DUR_US		(500)
#define MORSE_RAW_MAX_SLOT_DUR_US		(MORSE_RAW_MIN_SLOT_DUR_US + (200 * (1 << 11) - 1))
#define MORSE_RAW_MIN_RAW_DUR_US		MORSE_RAW_MIN_SLOT_DUR_US
#define MORSE_RAW_MAX_RAW_DUR_US		(MORSE_RAW_MAX_SLOT_DUR_US * \
							MORSE_RAW_MAX_3BIT_SLOTS)
#define MORSE_RAW_MAX_START_TIME_US		(UINT8_MAX * 2 * 1024)
#define MORSE_RAW_MAX_SLOTS			(63)
#define MORSE_RAW_MAX_PRIORITY			(7)
#define MORSE_RAW_MAX_BEACON_SPREAD		(UINT16_MAX)
#define MORSE_RAW_MAX_NOM_STA_PER_BEACON	(UINT16_MAX)
#define MORSE_RAW_DEFAULT_START_AID		(1)
#define MORSE_RAW_AID_PRIO_SHIFT		(8)
#define MORSE_RAW_AID_DEVICE_MASK		(0xFF)
#define MORSE_MAX_NUM_RAWS_USER_PRIO		(8)	/* Limited by QoS User Priority */
#define MORSE_RAW_ID_HOSTAPD_PRIO_OFFSET	(0x4000)
/* This is an existing limitation which can be removed with native s1g support. */
#define MAX_AID					(2007)


int morse_s1g_validate_csa_params(struct hostapd_iface *iface, struct csa_settings *settings);


/* Return the configured 1 or 2MHz primary channel */
int morse_s1g_get_primary_channel(struct hostapd_config *conf, int bw);

/* Defined in driver/driver/h */
enum wnm_oper;

#ifdef CONFIG_MORSE_WNM
/**
 * Handle wnm_oper from driver.
 *
 * @param ifname	The name of the interface that this operation applies to.
 * @param oper		The operation type.
 *
 * @returns 0 on success, else an error code.
 *
 * @see wpa_driver_ops::wnm_oper
 */
int morse_wnm_oper(const char *ifname, enum wnm_oper oper);
#endif

/**
 * Issue a Morse control command to enable or disable long sleep (i.e., sleep through DTIMs).
 *
 * @param iface		The name of the interface (e.g., wlan0)
 * @param enabled	Boolean indicating whether long sleep should be enabled or not.
 *
 * @returns 0 on success, else an error code.
 */
int morse_set_long_sleep_enabled(const char *iface, bool enabled);

/**
 * Issue a morse_cli command to store session information after succesful association
 *
 * @param ifname	The name of the interface (e.g., wlan0)
 * @param bssid		The BSSID for the association
 * @param dir		The directory path for storing Standby session information
 */
void morse_standby_session_store(const char* ifname, const u8* bssid,
	const char* standby_session_dir);

#ifdef CONFIG_MORSE_KEEP_ALIVE_OFFLOAD

/**
 * Issue a morse_cli command to set/offload the bss keep-alive frames.
 *
 * @param iface			The name of the interface (e.g., wlan0)
 * @param bss_max_idle_period	The BSS max idle period as derived directly
 *				from the WLAN_EID_BSS_MAX_IDLE_PERIOD
 * @param as_11ah		Intepret BSS max idle period as per the 11ah spec.
 *
 * @returns 0 on success, else an error code.
 */
int morse_set_keep_alive(const char* ifname, u16 bss_max_idle_period, bool as_11ah);

#endif /* CONFIG_MORSE_KEEP_ALIVE_OFFLOAD */

int morse_twt_conf(const char* ifname, struct morse_twt *twt_config);

/**
 * Issue a morse_cli command to set ecsa channel parameters
 *
 * @param ifname		The name of the interface (e.g., wlan0)
 * @param global_oper_class	Global operating class for the operating country
 *				and operating bandwidth (eg: for AU 68, 69, 70, 71)
 * @param prim_chwidth		Primary channel width in MHz (1, 2)
 * @param oper_chwidth		Operating channel width in MHz (1, 2, 4, 8)
 * @param oper_freq		Frequency of operating channel in kHz
 * @param prim_1mhz_ch_idx	1MHz channel index of primary channel
 * @param prim_global_op_class	Global operating class for primary channel
 * @param s1g_capab  User configured S1G capabilities.
 *
 * @returns 0 on success, else an error code.
 */
int morse_set_ecsa_params(const char* ifname, u8 global_oper_class, u8 prim_chwidth,
			int oper_chwidth, int oper_freq, u8 prim_1mhz_ch_idx,
			u8 prim_global_op_class, u32 s1g_capab);

int morse_set_mbssid_info(const char *ifname, const char *tx_iface_idx,
					u8 max_bss_index);
/**
 * Issue a morse_cli command to enable or disable CAC
 *
 * @param ifname		The name of the interface (e.g., wlan0)
 * @param enable		True to enable CAC, false to disable CAC
 *
 * @returns 0 on success, else an error code.
 */
int morse_cac_conf(const char* ifname, bool enable);

/**
 * Globally enable / disable RAW
 *
 * @param ifname the name of the interface
 * @param enable true to enable RAW, false to disable RAW
 *
 * @returns 0 on success, else error code
 */
int morse_raw_global_enable(const char *ifname, bool enable);

/**
 * Enable or disable RAW priorities.
 *
 * @param ifname the name of the interface
 * @param enable true to enable the priority, false to disable
 * @param prio index of the priority
 * @param start_time_us Start time from last beacon or RAW
 * @param duration_us RAW duration time
 * @param num_slots number of slots
 * @param cross_slot Cross slot boundary bleed allowed
 * @param max_bcn_spread maximum beacons to spread over (0 for no limit)
 * @param nom_stas_per_bcn Nominal number of STAs per beacon (0 for no spreading)
 * @param praw_period the period of the PRAW in beacons (0 for PRAW disabled)
 * @param praw_start_offset the beacon offset of the PRAW within the period
 *
 * @return 0 on success, else error code
 */
int morse_raw_priority_enable(const char *ifname, bool enable, u8 prio, u32 start_time_us,
	u32 duration_us, u8 num_slots, bool cross_slot, u16 max_bcn_spread, u16 nom_stas_per_bcn,
	u8 praw_period, u8 praw_start_offset);

#endif /* MORSE_H */
