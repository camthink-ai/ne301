/*
 * Copyright 2022 Morse Micro
 */

/*
 * To run these tests:
 *	make -f morse_test.mk
 */

#include "morse.h"
#include "unity.h"

void setUp (void) {} /* Is run before every test, put unit init calls here. */
void tearDown (void) {} /* Is run after every test, put unit clean-up calls here. */

extern int morse_s1g_verify_ht_chan_pairs(void);

void verify_chan_pairs_default(void)
{
	morse_set_s1g_ht_chan_pairs(NULL);
	TEST_ASSERT_EQUAL(0, morse_s1g_verify_ht_chan_pairs());
}

void verify_chan_pairs_jp(void)
{
	morse_set_s1g_ht_chan_pairs("JP");
	TEST_ASSERT_EQUAL(0, morse_s1g_verify_ht_chan_pairs());
}

/* Convert ht channel to s1g channel */
void test_morse_ht_chan_to_s1g_chan_default(void)
{
	/* test borders and variety of bandwidths in each section */
	TEST_ASSERT_EQUAL(1, morse_ht_chan_to_s1g_chan(132));
	TEST_ASSERT_EQUAL(2, morse_ht_chan_to_s1g_chan(134));
	TEST_ASSERT_EQUAL(3, morse_ht_chan_to_s1g_chan(136));
	TEST_ASSERT_EQUAL(5, morse_ht_chan_to_s1g_chan(36));
	TEST_ASSERT_EQUAL(6, morse_ht_chan_to_s1g_chan(38));
	TEST_ASSERT_EQUAL(8, morse_ht_chan_to_s1g_chan(42));
	TEST_ASSERT_EQUAL(12, morse_ht_chan_to_s1g_chan(50));
	TEST_ASSERT_EQUAL(19, morse_ht_chan_to_s1g_chan(64));
	TEST_ASSERT_EQUAL(21, morse_ht_chan_to_s1g_chan(100));
	TEST_ASSERT_EQUAL(22, morse_ht_chan_to_s1g_chan(102));
	TEST_ASSERT_EQUAL(24, morse_ht_chan_to_s1g_chan(106));
	TEST_ASSERT_EQUAL(28, morse_ht_chan_to_s1g_chan(114));
	TEST_ASSERT_EQUAL(34, morse_ht_chan_to_s1g_chan(126));
	TEST_ASSERT_EQUAL(35, morse_ht_chan_to_s1g_chan(128));
	TEST_ASSERT_EQUAL(37, morse_ht_chan_to_s1g_chan(149));
	TEST_ASSERT_EQUAL(38, morse_ht_chan_to_s1g_chan(151));
	TEST_ASSERT_EQUAL(44, morse_ht_chan_to_s1g_chan(163));
	TEST_ASSERT_EQUAL(48, morse_ht_chan_to_s1g_chan(171));
	TEST_ASSERT_EQUAL(50, morse_ht_chan_to_s1g_chan(175));
	TEST_ASSERT_EQUAL(51, morse_ht_chan_to_s1g_chan(177));

	/* sprinkling of unmapped */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_chan_to_s1g_chan(170));
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_chan_to_s1g_chan(130));
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_chan_to_s1g_chan(51));
}

/* Convert ht frequency to s1g channel */
void test_morse_ht_freq_to_s1g_chan_default(void)
{
	/* test borders and variety of bandwidths in each section */
	TEST_ASSERT_EQUAL(1, morse_ht_freq_to_s1g_chan(5660));
	TEST_ASSERT_EQUAL(2, morse_ht_freq_to_s1g_chan(5670));
	TEST_ASSERT_EQUAL(3, morse_ht_freq_to_s1g_chan(5680));
	TEST_ASSERT_EQUAL(5, morse_ht_freq_to_s1g_chan(5180));
	TEST_ASSERT_EQUAL(6, morse_ht_freq_to_s1g_chan(5190));
	TEST_ASSERT_EQUAL(8, morse_ht_freq_to_s1g_chan(5210));
	TEST_ASSERT_EQUAL(12, morse_ht_freq_to_s1g_chan(5250));
	TEST_ASSERT_EQUAL(19, morse_ht_freq_to_s1g_chan(5320));
	TEST_ASSERT_EQUAL(21, morse_ht_freq_to_s1g_chan(5500));
	TEST_ASSERT_EQUAL(22, morse_ht_freq_to_s1g_chan(5510));
	TEST_ASSERT_EQUAL(24, morse_ht_freq_to_s1g_chan(5530));
	TEST_ASSERT_EQUAL(28, morse_ht_freq_to_s1g_chan(5570));
	TEST_ASSERT_EQUAL(34, morse_ht_freq_to_s1g_chan(5630));
	TEST_ASSERT_EQUAL(35, morse_ht_freq_to_s1g_chan(5640));
	TEST_ASSERT_EQUAL(37, morse_ht_freq_to_s1g_chan(5745));
	TEST_ASSERT_EQUAL(38, morse_ht_freq_to_s1g_chan(5755));
	TEST_ASSERT_EQUAL(44, morse_ht_freq_to_s1g_chan(5815));
	TEST_ASSERT_EQUAL(48, morse_ht_freq_to_s1g_chan(5855));
	TEST_ASSERT_EQUAL(50, morse_ht_freq_to_s1g_chan(5875));
	TEST_ASSERT_EQUAL(51, morse_ht_freq_to_s1g_chan(5885));

	/* sprinkling of unmapped */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_freq_to_s1g_chan(5850));
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_freq_to_s1g_chan(5650));
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_freq_to_s1g_chan(5255));
}

/* Convert s1g channel to ht channel */
void test_morse_s1g_chan_to_ht_chan_default(void)
{
	/* test borders and variety of bandwidths in each section */
	TEST_ASSERT_EQUAL(132, morse_s1g_chan_to_ht_chan(1));
	TEST_ASSERT_EQUAL(134, morse_s1g_chan_to_ht_chan(2));
	TEST_ASSERT_EQUAL(136, morse_s1g_chan_to_ht_chan(3));
	TEST_ASSERT_EQUAL(36, morse_s1g_chan_to_ht_chan(5));
	TEST_ASSERT_EQUAL(38, morse_s1g_chan_to_ht_chan(6));
	TEST_ASSERT_EQUAL(42, morse_s1g_chan_to_ht_chan(8));
	TEST_ASSERT_EQUAL(50, morse_s1g_chan_to_ht_chan(12));
	TEST_ASSERT_EQUAL(64, morse_s1g_chan_to_ht_chan(19));
	TEST_ASSERT_EQUAL(100, morse_s1g_chan_to_ht_chan(21));
	TEST_ASSERT_EQUAL(102, morse_s1g_chan_to_ht_chan(22));
	TEST_ASSERT_EQUAL(106, morse_s1g_chan_to_ht_chan(24));
	TEST_ASSERT_EQUAL(114, morse_s1g_chan_to_ht_chan(28));
	TEST_ASSERT_EQUAL(126, morse_s1g_chan_to_ht_chan(34));
	TEST_ASSERT_EQUAL(128, morse_s1g_chan_to_ht_chan(35));
	TEST_ASSERT_EQUAL(149, morse_s1g_chan_to_ht_chan(37));
	TEST_ASSERT_EQUAL(151, morse_s1g_chan_to_ht_chan(38));
	TEST_ASSERT_EQUAL(163, morse_s1g_chan_to_ht_chan(44));
	TEST_ASSERT_EQUAL(171, morse_s1g_chan_to_ht_chan(48));
	TEST_ASSERT_EQUAL(175, morse_s1g_chan_to_ht_chan(50));
	TEST_ASSERT_EQUAL(177, morse_s1g_chan_to_ht_chan(51));
}

/* Convert s1g channel to bandwidth */
void test_morse_s1g_chan_to_bw_default(void)
{
	unsigned int i;

	for (i = 1; i <= 51; i += 2)
		TEST_ASSERT_EQUAL(1, morse_s1g_chan_to_bw(i));

	for (i = 2; i <= 51; i += 4)
		TEST_ASSERT_EQUAL(2, morse_s1g_chan_to_bw(i));

	for (i = 8; i <= 51; i += 8)
		TEST_ASSERT_EQUAL(4, morse_s1g_chan_to_bw(i));

	for (i = 12; i <= 51; i += 16)
		TEST_ASSERT_EQUAL(8, morse_s1g_chan_to_bw(i));
}

/* Convert an operating class to channel width */
void test_morse_s1g_oper_class_to_ch_width(void)
{
	/* invalid edge cases */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_s1g_op_class_to_ch_width(-1));
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_s1g_op_class_to_ch_width(0));
	/* valid */
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(1));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(2));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(3));
	TEST_ASSERT_EQUAL(8, morse_s1g_op_class_to_ch_width(4));
	/* no 16MHz support */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_s1g_op_class_to_ch_width(5));
	/* valid */
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(6));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(7));
	/* valid */
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(8));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(9));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(10));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(11));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(12));
	/* reserved */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_s1g_op_class_to_ch_width(13));
	/* valid */
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(14));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(15));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(16));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(17));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(18));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(19));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(20));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(21));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(22));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(23));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(24));
	TEST_ASSERT_EQUAL(8, morse_s1g_op_class_to_ch_width(25));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(26));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(27));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(28));
	TEST_ASSERT_EQUAL(8, morse_s1g_op_class_to_ch_width(29));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(30));
	/* reserved region boundaries */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_s1g_op_class_to_ch_width(32));
	/* valid */
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(65));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(66));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(67));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(68));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(69));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(70));
	TEST_ASSERT_EQUAL(8, morse_s1g_op_class_to_ch_width(71));
	/* fail as no 16Mhz support */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_s1g_op_class_to_ch_width(72));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(73));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(74));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_to_ch_width(75));
	TEST_ASSERT_EQUAL(4, morse_s1g_op_class_to_ch_width(76));
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_to_ch_width(77));
	/* next reserved region boundary / unsupported s1g */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_s1g_op_class_to_ch_width(78));
}

/* Convert an operating class to country code */
#define MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY(expect_cc, class) { \
	char cc[2]; \
	TEST_ASSERT_EQUAL(0, morse_s1g_op_class_to_country(class, cc)); \
	TEST_ASSERT_EQUAL_CHAR_ARRAY(expect_cc, cc, 2); \
}

#define MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY_FAIL(class) { \
	char cc[2]; \
	TEST_ASSERT_LESS_THAN(0, morse_s1g_op_class_to_country(class, cc)); \
}

void test_morse_s1g_oper_class_to_country(void)
{
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("US", 1);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("US", 2);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("US", 3);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("US", 4);
	/* no knowledge of 16 MHz */
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY_FAIL(5);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("EU", 6);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("IN", 31);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("EU", 7);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("JP", 8);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("JP", 9);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("JP", 10);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("JP", 11);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("JP", 12);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY_FAIL(13);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("KR", 14);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("KR", 15);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("KR", 16);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("SG", 17);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("SG", 18);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("SG", 19);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("SG", 20);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("SG", 21);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("AU", 22);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("AU", 23);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("AU", 24);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("AU", 25);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("NZ", 26);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("NZ", 27);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("NZ", 28);
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("NZ", 29);
	/* Set to ZZ till EU class 77 is supported */
	MORSE_TEST_S1G_OPER_CLASS_TO_COUNTRY("ZZ", 30);
}

/* Convert an operating class and s1g channel to frequency (kHz) */
void test_morse_s1g_oper_class_chan_to_freq_default(void)
{
	TEST_ASSERT_EQUAL(922500, morse_s1g_op_class_chan_to_freq(1, 41));
	TEST_ASSERT_EQUAL(907000, morse_s1g_op_class_chan_to_freq(2, 10));
	TEST_ASSERT_EQUAL(906000, morse_s1g_op_class_chan_to_freq(3, 8));
	TEST_ASSERT_EQUAL(916000, morse_s1g_op_class_chan_to_freq(4, 28));

	TEST_ASSERT_EQUAL(866500, morse_s1g_op_class_chan_to_freq(6, 7));

	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_chan_to_freq(8, 19));

	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_chan_to_freq(14, 11));
	TEST_ASSERT_EQUAL(920500, morse_s1g_op_class_chan_to_freq(15, 6));
	TEST_ASSERT_EQUAL(921500, morse_s1g_op_class_chan_to_freq(16, 8));
	TEST_ASSERT_EQUAL(867500, morse_s1g_op_class_chan_to_freq(17, 9));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_chan_to_freq(18, 45));
	TEST_ASSERT_EQUAL(868000, morse_s1g_op_class_chan_to_freq(19, 10));
	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_chan_to_freq(20, 42));
	TEST_ASSERT_EQUAL(922000, morse_s1g_op_class_chan_to_freq(21, 40));
	TEST_ASSERT_EQUAL(927500, morse_s1g_op_class_chan_to_freq(22, 51));
	TEST_ASSERT_EQUAL(927000, morse_s1g_op_class_chan_to_freq(23, 50));
	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_chan_to_freq(24, 48));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_chan_to_freq(25, 44));
	TEST_ASSERT_EQUAL(916500, morse_s1g_op_class_chan_to_freq(26, 29));
	TEST_ASSERT_EQUAL(917000, morse_s1g_op_class_chan_to_freq(27, 30));
	TEST_ASSERT_EQUAL(918000, morse_s1g_op_class_chan_to_freq(28, 32));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_chan_to_freq(29, 44));
	TEST_ASSERT_EQUAL(917900, morse_s1g_op_class_chan_to_freq(30, 33));
	TEST_ASSERT_EQUAL(866500, morse_s1g_op_class_chan_to_freq(31, 7));
}

/* Convert ht channel and s1g operating class to s1g frequency (kHz) */
void test_morse_s1g_op_class_ht_chan_to_s1g_freq_default(void)
{
	TEST_ASSERT_EQUAL(922500, morse_s1g_op_class_ht_chan_to_s1g_freq(1, 157));
	TEST_ASSERT_EQUAL(907000, morse_s1g_op_class_ht_chan_to_s1g_freq(2, 46));
	TEST_ASSERT_EQUAL(906000, morse_s1g_op_class_ht_chan_to_s1g_freq(3, 42));
	TEST_ASSERT_EQUAL(916000, morse_s1g_op_class_ht_chan_to_s1g_freq(4, 114));

	TEST_ASSERT_EQUAL(866500, morse_s1g_op_class_ht_chan_to_s1g_freq(6, 40));

	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_ht_chan_to_s1g_freq(8, 64));

	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_ht_chan_to_s1g_freq(14, 48));
	TEST_ASSERT_EQUAL(920500, morse_s1g_op_class_ht_chan_to_s1g_freq(15, 38));
	TEST_ASSERT_EQUAL(921500, morse_s1g_op_class_ht_chan_to_s1g_freq(16, 42));
	TEST_ASSERT_EQUAL(867500, morse_s1g_op_class_ht_chan_to_s1g_freq(17, 44));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_ht_chan_to_s1g_freq(18, 165));
	TEST_ASSERT_EQUAL(868000, morse_s1g_op_class_ht_chan_to_s1g_freq(19, 46));
	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_ht_chan_to_s1g_freq(20, 159));
	TEST_ASSERT_EQUAL(922000, morse_s1g_op_class_ht_chan_to_s1g_freq(21, 155));
	TEST_ASSERT_EQUAL(927500, morse_s1g_op_class_ht_chan_to_s1g_freq(22, 177));
	TEST_ASSERT_EQUAL(927000, morse_s1g_op_class_ht_chan_to_s1g_freq(23, 175));
	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_ht_chan_to_s1g_freq(24, 171));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_ht_chan_to_s1g_freq(25, 163));
	TEST_ASSERT_EQUAL(916500, morse_s1g_op_class_ht_chan_to_s1g_freq(26, 116));
	TEST_ASSERT_EQUAL(917000, morse_s1g_op_class_ht_chan_to_s1g_freq(27, 118));
	TEST_ASSERT_EQUAL(918000, morse_s1g_op_class_ht_chan_to_s1g_freq(28, 122));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_ht_chan_to_s1g_freq(29, 163));
	TEST_ASSERT_EQUAL(917900, morse_s1g_op_class_ht_chan_to_s1g_freq(30, 124));
	TEST_ASSERT_EQUAL(866500, morse_s1g_op_class_ht_chan_to_s1g_freq(31, 40));
}

/* Convert an operating class and ht frequency into a s1g frequency (kHz) */
void test_morse_s1g_op_class_ht_freq_to_s1g_freq_default(void)
{
	TEST_ASSERT_EQUAL(922500, morse_s1g_op_class_ht_freq_to_s1g_freq(1, 5785));
	TEST_ASSERT_EQUAL(907000, morse_s1g_op_class_ht_freq_to_s1g_freq(2, 5230));
	TEST_ASSERT_EQUAL(906000, morse_s1g_op_class_ht_freq_to_s1g_freq(3, 5210));
	TEST_ASSERT_EQUAL(916000, morse_s1g_op_class_ht_freq_to_s1g_freq(4, 5570));

	TEST_ASSERT_EQUAL(866500, morse_s1g_op_class_ht_freq_to_s1g_freq(6, 5200));

	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_ht_freq_to_s1g_freq(8, 5320));

	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_ht_freq_to_s1g_freq(14, 5240));
	TEST_ASSERT_EQUAL(920500, morse_s1g_op_class_ht_freq_to_s1g_freq(15, 5190));
	TEST_ASSERT_EQUAL(921500, morse_s1g_op_class_ht_freq_to_s1g_freq(16, 5210));
	TEST_ASSERT_EQUAL(867500, morse_s1g_op_class_ht_freq_to_s1g_freq(17, 5220));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_ht_freq_to_s1g_freq(18, 5825));
	TEST_ASSERT_EQUAL(868000, morse_s1g_op_class_ht_freq_to_s1g_freq(19, 5230));
	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_ht_freq_to_s1g_freq(20, 5795));
	TEST_ASSERT_EQUAL(922000, morse_s1g_op_class_ht_freq_to_s1g_freq(21, 5775));
	TEST_ASSERT_EQUAL(927500, morse_s1g_op_class_ht_freq_to_s1g_freq(22, 5885));
	TEST_ASSERT_EQUAL(927000, morse_s1g_op_class_ht_freq_to_s1g_freq(23, 5875));
	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_ht_freq_to_s1g_freq(24, 5855));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_ht_freq_to_s1g_freq(25, 5815));
	TEST_ASSERT_EQUAL(916500, morse_s1g_op_class_ht_freq_to_s1g_freq(26, 5580));
	TEST_ASSERT_EQUAL(917000, morse_s1g_op_class_ht_freq_to_s1g_freq(27, 5590));
	TEST_ASSERT_EQUAL(918000, morse_s1g_op_class_ht_freq_to_s1g_freq(28, 5610));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_ht_freq_to_s1g_freq(29, 5815));
	TEST_ASSERT_EQUAL(917900, morse_s1g_op_class_ht_freq_to_s1g_freq(30, 5620));
	TEST_ASSERT_EQUAL(866500, morse_s1g_op_class_ht_freq_to_s1g_freq(31, 5200));
}

/* Convert a country and ht frequency into a s1g frequency (kHz) */
void test_morse_cc_ht_freq_to_s1g_freq_default(void)
{
	TEST_ASSERT_EQUAL(922500, morse_cc_ht_freq_to_s1g_freq("US", 5785));
	TEST_ASSERT_EQUAL(907000, morse_cc_ht_freq_to_s1g_freq("US", 5230));
	TEST_ASSERT_EQUAL(906000, morse_cc_ht_freq_to_s1g_freq("US", 5210));
	TEST_ASSERT_EQUAL(916000, morse_cc_ht_freq_to_s1g_freq("US", 5570));

	TEST_ASSERT_EQUAL(866500, morse_cc_ht_freq_to_s1g_freq("EU", 5200));

	TEST_ASSERT_EQUAL(926000, morse_cc_ht_freq_to_s1g_freq("JP", 5320));

	TEST_ASSERT_EQUAL(923000, morse_cc_ht_freq_to_s1g_freq("KR", 5240));
	TEST_ASSERT_EQUAL(920500, morse_cc_ht_freq_to_s1g_freq("KR", 5190));
	TEST_ASSERT_EQUAL(921500, morse_cc_ht_freq_to_s1g_freq("KR", 5210));
	TEST_ASSERT_EQUAL(867500, morse_cc_ht_freq_to_s1g_freq("SG", 5220));
	TEST_ASSERT_EQUAL(924500, morse_cc_ht_freq_to_s1g_freq("SG", 5825));
	TEST_ASSERT_EQUAL(868000, morse_cc_ht_freq_to_s1g_freq("SG", 5230));
	TEST_ASSERT_EQUAL(923000, morse_cc_ht_freq_to_s1g_freq("SG", 5795));
	TEST_ASSERT_EQUAL(922000, morse_cc_ht_freq_to_s1g_freq("SG", 5775));
	TEST_ASSERT_EQUAL(927500, morse_cc_ht_freq_to_s1g_freq("AU", 5885));
	TEST_ASSERT_EQUAL(927000, morse_cc_ht_freq_to_s1g_freq("AU", 5875));
	TEST_ASSERT_EQUAL(926000, morse_cc_ht_freq_to_s1g_freq("AU", 5855));
	TEST_ASSERT_EQUAL(924000, morse_cc_ht_freq_to_s1g_freq("AU", 5815));
	TEST_ASSERT_EQUAL(916500, morse_cc_ht_freq_to_s1g_freq("NZ", 5580));
	TEST_ASSERT_EQUAL(917000, morse_cc_ht_freq_to_s1g_freq("NZ", 5590));
	TEST_ASSERT_EQUAL(918000, morse_cc_ht_freq_to_s1g_freq("NZ", 5610));
	TEST_ASSERT_EQUAL(924000, morse_cc_ht_freq_to_s1g_freq("NZ", 5815));
	/* Set to ZZ till EU class 77 is supported */
	TEST_ASSERT_EQUAL(917900, morse_cc_ht_freq_to_s1g_freq("ZZ", 5620));
	TEST_ASSERT_EQUAL(866500, morse_cc_ht_freq_to_s1g_freq("IN", 5200));
}

/* Return the first valid channel from an s1g operating class */
void test_morse_s1g_op_class_first_chan(void)
{
	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_first_chan(1));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_first_chan(2));
	TEST_ASSERT_EQUAL(8, morse_s1g_op_class_first_chan(3));
	TEST_ASSERT_EQUAL(12, morse_s1g_op_class_first_chan(4));

	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_first_chan(6));

	TEST_ASSERT_EQUAL(9, morse_s1g_op_class_first_chan(8));

	TEST_ASSERT_EQUAL(1, morse_s1g_op_class_first_chan(14));
	TEST_ASSERT_EQUAL(2, morse_s1g_op_class_first_chan(15));
	TEST_ASSERT_EQUAL(8, morse_s1g_op_class_first_chan(16));
	TEST_ASSERT_EQUAL(7, morse_s1g_op_class_first_chan(17));
	TEST_ASSERT_EQUAL(37, morse_s1g_op_class_first_chan(18));
	TEST_ASSERT_EQUAL(10, morse_s1g_op_class_first_chan(19));
	TEST_ASSERT_EQUAL(38, morse_s1g_op_class_first_chan(20));
	TEST_ASSERT_EQUAL(40, morse_s1g_op_class_first_chan(21));
	TEST_ASSERT_EQUAL(27, morse_s1g_op_class_first_chan(22));
	TEST_ASSERT_EQUAL(30, morse_s1g_op_class_first_chan(23));
	TEST_ASSERT_EQUAL(32, morse_s1g_op_class_first_chan(24));
	TEST_ASSERT_EQUAL(44, morse_s1g_op_class_first_chan(25));
	TEST_ASSERT_EQUAL(27, morse_s1g_op_class_first_chan(26));
	TEST_ASSERT_EQUAL(30, morse_s1g_op_class_first_chan(27));
	TEST_ASSERT_EQUAL(32, morse_s1g_op_class_first_chan(28));
	TEST_ASSERT_EQUAL(44, morse_s1g_op_class_first_chan(29));
	TEST_ASSERT_EQUAL(31, morse_s1g_op_class_first_chan(30));
	TEST_ASSERT_EQUAL(5, morse_s1g_op_class_first_chan(31));
}

/* Returns the center channel, taking into account VHT channel offsets */
int morse_ht_chan_to_ht_chan_center(struct hostapd_config *conf, int ht_chan);

#define TEST_HT_CHAN_CENTER(expec, chan, s1g_prim_1mhz_idx, vht) \
{ \
	struct hostapd_config conf = { \
		.ieee80211ac = vht, \
		.vht_oper_chwidth = vht, \
		.s1g_prim_1mhz_chan_index = s1g_prim_1mhz_idx \
	}; \
	TEST_ASSERT_EQUAL(expec, morse_ht_chan_to_ht_chan_center(&conf, chan)); \
}

void test_morse_ht_chan_to_ht_chan_center(void)
{
	TEST_HT_CHAN_CENTER(36, 36, 0, 0);
	TEST_HT_CHAN_CENTER(38, 38, 0, CHANWIDTH_USE_HT);
	TEST_HT_CHAN_CENTER(42, 42, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(58, 58, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(106, 106, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(122, 122, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(155, 155, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(171, 171, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(50, 50, 0, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(114, 114, 0, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(163, 163, 0, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(42, 36, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(58, 52, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(106, 100, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(122, 116, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(155, 149, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(171, 165, 0, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(50, 36, 0, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(114, 100, 0, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(163, 149, 0, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(42, 48, 3, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(58, 64, 3, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(106, 112, 3, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(122, 128, 3, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(155, 161, 3, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(171, 177, 3, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(50, 64, 7, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(114, 128, 7, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(163, 177, 7, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(42, 40, 1, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(58, 56, 1, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(106, 104, 1, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(122, 120, 1, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(155, 153, 1, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(171, 169, 1, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_CENTER(50, 48, 3, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(114, 112, 3, CHANWIDTH_160MHZ);
	TEST_HT_CHAN_CENTER(163, 161, 3, CHANWIDTH_160MHZ);
}

/* Returns the ht channel, taking into account VHT channel offsets */
#define TEST_HT_CHAN_OFFSET(expec, chan, vht) \
{ \
	struct hostapd_config conf = {.ieee80211ac = vht, .vht_oper_chwidth = vht}; \
	TEST_ASSERT_EQUAL(expec, morse_ht_center_chan_to_ht_chan(&conf, chan)); \
}
void test_morse_ht_center_chan_to_ht_chan(void)
{
	TEST_HT_CHAN_OFFSET(36, 36, 0);
	TEST_HT_CHAN_OFFSET(38, 38, CHANWIDTH_USE_HT);
	TEST_HT_CHAN_OFFSET(36, 42, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_OFFSET(52, 58, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_OFFSET(100, 106, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_OFFSET(116, 122, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_OFFSET(149, 155, CHANWIDTH_80MHZ);
	TEST_HT_CHAN_OFFSET(165, 171, CHANWIDTH_80MHZ);
}

/*
 * Verify operating class and country code (no channel).
 * Returns S1G local operating class if valid combination, negative if invalid.
 */

#define MORSE_TEST_S1G_VERIFY_CLASS_CASE(expect_retval, class, cc, s1g_prim_1mhz_chan_index) { \
	TEST_ASSERT_EQUAL(expect_retval, morse_s1g_verify_op_class_country(class, cc, \
					  s1g_prim_1mhz_chan_index)); \
}

void test_morse_s1g_verify_oper_class_country(void)
{
	/* local operating class, no cc */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(19, 19, NULL, 0);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(19, 19, "", 0);
	/* global operating class, valid CC */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(19, 67, "SG", 0);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(31, 66, "IN", 0);
	/* local operating class, valid cc */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(22, 22, "AU", 0);
	/* global operating class, no CC */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 67, NULL, 0);
	/* global operating class, CC not mapped */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 67, "AU", 0);
	/* global operating class, CC mapped */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(19, 67, "SG", 0);
	/* correct s1g 1mhz prim channel index */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(19, 19, NULL, 1);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(19, 19, "", 1);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(19, 67, "SG", 1);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(24, 24, "AU", 3);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(24, 70, "AU", 2);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(31, 66, "IN", 0);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(4, 71, "US", 6);
	/* invalid s1g 1mhz prim channel index */
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 19, NULL, 2);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 19, "", 3);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 67, "SG", 5);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 24, "AU", 4);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 70, "AU", 5);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 66, "IN", 2);
	MORSE_TEST_S1G_VERIFY_CLASS_CASE(MORSE_S1G_RETURN_ERROR, 71, "US", 8);

}

/*
 * Verify operating class, country code and channel.
 * Returns S1G local operating class if valid combination, negative if invalid.
 */

#define MORSE_TEST_S1G_VERIFY_CASE(expect_retval, class, cc, chan, s1g_prim_1mhz_chan_index) { \
	TEST_ASSERT_EQUAL(expect_retval, morse_s1g_verify_op_class_country_channel(class, \
				cc, chan, s1g_prim_1mhz_chan_index)); \
}

void test_morse_s1g_verify_oper_class_country_channel(void)
{
	/* local operating class, valid channel, no cc */
	MORSE_TEST_S1G_VERIFY_CASE(19, 19, NULL, 10, 0);
	/* global operating class, valid channel, valid CC */
	MORSE_TEST_S1G_VERIFY_CASE(19, 67, "SG", 10, 0);
	MORSE_TEST_S1G_VERIFY_CASE(31, 66, "IN", 7, 0);
	/* local operating class, valid channel, valid cc */
	MORSE_TEST_S1G_VERIFY_CASE(22, 22, "AU", 27, 0);
	/* local operating class, invalid channel, valid cc */
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 22, "AU", 28, 0);
	/* global operating class, no CC */
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 67, NULL, 10, 0);
	/* global operating class, CC not mapped */
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 67, "AU", 10, 0);
	/* global operating class, CC mapped, invalid channel */
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 67, "SG", 11, 0);
	/* correct s1g 1mhz prim channel index */
	MORSE_TEST_S1G_VERIFY_CASE(19, 19, NULL, 10, 1);
	MORSE_TEST_S1G_VERIFY_CASE(19, 67, "SG", 10, 1);
	MORSE_TEST_S1G_VERIFY_CASE(31, 66, "IN", 7, 0);
	MORSE_TEST_S1G_VERIFY_CASE(23, 23, "AU", 38, 1);
	MORSE_TEST_S1G_VERIFY_CASE(4, 71, "US", 28, 7);
	/* invalid s1g 1mhz prim channel index */
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 19, NULL, 10, 2);
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 67, "SG", 10, 3);
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 66, "IN", 7, 5);
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 23, "AU", 38, 7);
	MORSE_TEST_S1G_VERIFY_CASE(MORSE_S1G_RETURN_ERROR, 71, "US", 28, 8);
}

void test_morse_ht_freq_to_s1g_chan_jp(void)
{
	TEST_ASSERT_EQUAL(9, morse_ht_freq_to_s1g_chan(5540));
	TEST_ASSERT_EQUAL(13, morse_ht_freq_to_s1g_chan(5180));
	TEST_ASSERT_EQUAL(15, morse_ht_freq_to_s1g_chan(5200));
	TEST_ASSERT_EQUAL(17, morse_ht_freq_to_s1g_chan(5220));
	TEST_ASSERT_EQUAL(19, morse_ht_freq_to_s1g_chan(5240));
	TEST_ASSERT_EQUAL(2, morse_ht_freq_to_s1g_chan(5190));
	TEST_ASSERT_EQUAL(6, morse_ht_freq_to_s1g_chan(5230));
	TEST_ASSERT_EQUAL(36, morse_ht_freq_to_s1g_chan(5210));
	TEST_ASSERT_EQUAL(4, morse_ht_freq_to_s1g_chan(5270));
	TEST_ASSERT_EQUAL(8, morse_ht_freq_to_s1g_chan(5310));
	TEST_ASSERT_EQUAL(38, morse_ht_freq_to_s1g_chan(5290));

	/* Check a few unmapped */
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_freq_to_s1g_chan(5500));
	TEST_ASSERT_EQUAL(MORSE_S1G_RETURN_ERROR, morse_ht_freq_to_s1g_chan(5610));
}

void test_morse_s1g_chan_to_ht_chan_jp(void)
{
	TEST_ASSERT_EQUAL(36, morse_s1g_chan_to_ht_chan(13));
	TEST_ASSERT_EQUAL(40, morse_s1g_chan_to_ht_chan(15));
	TEST_ASSERT_EQUAL(44, morse_s1g_chan_to_ht_chan(17));
	TEST_ASSERT_EQUAL(48, morse_s1g_chan_to_ht_chan(19));
	TEST_ASSERT_EQUAL(64, morse_s1g_chan_to_ht_chan(21));
	TEST_ASSERT_EQUAL(38, morse_s1g_chan_to_ht_chan(2));
	TEST_ASSERT_EQUAL(46, morse_s1g_chan_to_ht_chan(6));
	TEST_ASSERT_EQUAL(54, morse_s1g_chan_to_ht_chan(4));
	TEST_ASSERT_EQUAL(62, morse_s1g_chan_to_ht_chan(8));
	TEST_ASSERT_EQUAL(42, morse_s1g_chan_to_ht_chan(36));
	TEST_ASSERT_EQUAL(58, morse_s1g_chan_to_ht_chan(38));
}

void test_morse_s1g_chan_to_bw_jp(void)
{
	unsigned int i;

	for (i = 13; i <= 21; i += 2)
		TEST_ASSERT_EQUAL(1, morse_s1g_chan_to_bw(i));

	for (i = 2; i <= 8; i += 2)
		TEST_ASSERT_EQUAL(2, morse_s1g_chan_to_bw(i));

	for (i = 36; i <= 38; i += 2)
		TEST_ASSERT_EQUAL(4, morse_s1g_chan_to_bw(i));
}

void test_morse_s1g_oper_class_chan_to_freq_jp(void)
{
	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_chan_to_freq(8, 13));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_chan_to_freq(8, 15));
	TEST_ASSERT_EQUAL(925000, morse_s1g_op_class_chan_to_freq(8, 17));
	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_chan_to_freq(8, 19));
	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_chan_to_freq(8, 19));
	TEST_ASSERT_EQUAL(923500, morse_s1g_op_class_chan_to_freq(9, 2));
	TEST_ASSERT_EQUAL(925500, morse_s1g_op_class_chan_to_freq(9, 6));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_chan_to_freq(10, 4));
	TEST_ASSERT_EQUAL(926500, morse_s1g_op_class_chan_to_freq(10, 8));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_chan_to_freq(11, 36));
	TEST_ASSERT_EQUAL(925500, morse_s1g_op_class_chan_to_freq(12, 38));
}

void test_morse_s1g_op_class_ht_chan_to_s1g_freq_jp(void)
{
	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_ht_chan_to_s1g_freq(8, 36));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_ht_chan_to_s1g_freq(8, 40));
	TEST_ASSERT_EQUAL(925000, morse_s1g_op_class_ht_chan_to_s1g_freq(8, 44));
	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_ht_chan_to_s1g_freq(8, 48));
	TEST_ASSERT_EQUAL(927000, morse_s1g_op_class_ht_chan_to_s1g_freq(8, 64));
	TEST_ASSERT_EQUAL(923500, morse_s1g_op_class_ht_chan_to_s1g_freq(9, 38));
	TEST_ASSERT_EQUAL(925500, morse_s1g_op_class_ht_chan_to_s1g_freq(9, 46));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_ht_chan_to_s1g_freq(10, 54));
	TEST_ASSERT_EQUAL(926500, morse_s1g_op_class_ht_chan_to_s1g_freq(10, 62));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_ht_chan_to_s1g_freq(11, 42));
	TEST_ASSERT_EQUAL(925500, morse_s1g_op_class_ht_chan_to_s1g_freq(12, 58));
}

void test_morse_s1g_op_class_ht_freq_to_s1g_freq_jp(void)
{
	TEST_ASSERT_EQUAL(923000, morse_s1g_op_class_ht_freq_to_s1g_freq(8, 5180));
	TEST_ASSERT_EQUAL(924000, morse_s1g_op_class_ht_freq_to_s1g_freq(8, 5200));
	TEST_ASSERT_EQUAL(925000, morse_s1g_op_class_ht_freq_to_s1g_freq(8, 5220));
	TEST_ASSERT_EQUAL(926000, morse_s1g_op_class_ht_freq_to_s1g_freq(8, 5240));
	TEST_ASSERT_EQUAL(927000, morse_s1g_op_class_ht_freq_to_s1g_freq(8, 5320));
	TEST_ASSERT_EQUAL(923500, morse_s1g_op_class_ht_freq_to_s1g_freq(9, 5190));
	TEST_ASSERT_EQUAL(925500, morse_s1g_op_class_ht_freq_to_s1g_freq(9, 5230));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_ht_freq_to_s1g_freq(10, 5270));
	TEST_ASSERT_EQUAL(926500, morse_s1g_op_class_ht_freq_to_s1g_freq(10, 5310));
	TEST_ASSERT_EQUAL(924500, morse_s1g_op_class_ht_freq_to_s1g_freq(11, 5210));
	TEST_ASSERT_EQUAL(925500, morse_s1g_op_class_ht_freq_to_s1g_freq(12, 5290));
}

void test_morse_cc_ht_freq_to_s1g_freq_jp(void)
{
	TEST_ASSERT_EQUAL(923000, morse_cc_ht_freq_to_s1g_freq("JP", 5180));
	TEST_ASSERT_EQUAL(924000, morse_cc_ht_freq_to_s1g_freq("JP", 5200));
	TEST_ASSERT_EQUAL(925000, morse_cc_ht_freq_to_s1g_freq("JP", 5220));
	TEST_ASSERT_EQUAL(926000, morse_cc_ht_freq_to_s1g_freq("JP", 5240));
	TEST_ASSERT_EQUAL(927000, morse_cc_ht_freq_to_s1g_freq("JP", 5320));
	TEST_ASSERT_EQUAL(923500, morse_cc_ht_freq_to_s1g_freq("JP", 5190));
	TEST_ASSERT_EQUAL(925500, morse_cc_ht_freq_to_s1g_freq("JP", 5230));
	TEST_ASSERT_EQUAL(924500, morse_cc_ht_freq_to_s1g_freq("JP", 5210));
	TEST_ASSERT_EQUAL(924500, morse_cc_ht_freq_to_s1g_freq("JP", 5270));
	TEST_ASSERT_EQUAL(926500, morse_cc_ht_freq_to_s1g_freq("JP", 5310));
	TEST_ASSERT_EQUAL(925500, morse_cc_ht_freq_to_s1g_freq("JP", 5290));
}

void test_morse_cc_verify_get_primary_s1g_channel(void)
{
	/* Validate JP primary channels */
	TEST_ASSERT_EQUAL(15, morse_calculate_primary_s1g_channel_jp(4, 1, 38, 0));
	TEST_ASSERT_EQUAL(21, morse_calculate_primary_s1g_channel_jp(4, 1, 38, 3));
	TEST_ASSERT_EQUAL(4,  morse_calculate_primary_s1g_channel_jp(4, 2, 38, 1));
	TEST_ASSERT_EQUAL(8,  morse_calculate_primary_s1g_channel_jp(4, 2, 38, 2));
	TEST_ASSERT_EQUAL(13, morse_calculate_primary_s1g_channel_jp(4, 1, 36, 0));
	TEST_ASSERT_EQUAL(17, morse_calculate_primary_s1g_channel_jp(4, 1, 36, 2));
	TEST_ASSERT_EQUAL(2,  morse_calculate_primary_s1g_channel_jp(4, 2, 36, 1));
	TEST_ASSERT_EQUAL(6,  morse_calculate_primary_s1g_channel_jp(4, 2, 36, 3));
	TEST_ASSERT_EQUAL(2,  morse_calculate_primary_s1g_channel_jp(2, 2, 2, 0));
	TEST_ASSERT_EQUAL(15, morse_calculate_primary_s1g_channel_jp(2, 1, 2, 1));
	TEST_ASSERT_EQUAL(6,  morse_calculate_primary_s1g_channel_jp(2, 2, 6, 0));
	TEST_ASSERT_EQUAL(19, morse_calculate_primary_s1g_channel_jp(2, 1, 6, 1));
	TEST_ASSERT_EQUAL(4,  morse_calculate_primary_s1g_channel_jp(2, 2, 4, 0));
	TEST_ASSERT_EQUAL(17, morse_calculate_primary_s1g_channel_jp(2, 1, 4, 1));
	TEST_ASSERT_EQUAL(8,  morse_calculate_primary_s1g_channel_jp(2, 2, 8, 0));
	TEST_ASSERT_EQUAL(21, morse_calculate_primary_s1g_channel_jp(2, 1, 8, 1));
	TEST_ASSERT_EQUAL(13, morse_calculate_primary_s1g_channel_jp(1, 1, 13, 0));
	TEST_ASSERT_EQUAL(15, morse_calculate_primary_s1g_channel_jp(1, 1, 15, 0));
	TEST_ASSERT_EQUAL(17, morse_calculate_primary_s1g_channel_jp(1, 1, 17, 0));
	TEST_ASSERT_EQUAL(19, morse_calculate_primary_s1g_channel_jp(1, 1, 19, 0));
	TEST_ASSERT_EQUAL(21, morse_calculate_primary_s1g_channel_jp(1, 1, 21, 0));
	TEST_ASSERT_EQUAL(13, morse_calculate_primary_s1g_channel_jp(2, 1, 2, 0));

	/* Validate all countries other than Japan */
	TEST_ASSERT_EQUAL(37, morse_calculate_primary_s1g_channel(8, 1, 44, 0));
	TEST_ASSERT_EQUAL(41, morse_calculate_primary_s1g_channel(8, 1, 44, 2));
	TEST_ASSERT_EQUAL(47, morse_calculate_primary_s1g_channel(8, 1, 44, 5));
	TEST_ASSERT_EQUAL(51, morse_calculate_primary_s1g_channel(8, 1, 44, 7));
	TEST_ASSERT_EQUAL(38, morse_calculate_primary_s1g_channel(8, 2, 44, 1));
	TEST_ASSERT_EQUAL(42, morse_calculate_primary_s1g_channel(8, 2, 44, 3));
	TEST_ASSERT_EQUAL(46, morse_calculate_primary_s1g_channel(8, 2, 44, 4));
	TEST_ASSERT_EQUAL(50, morse_calculate_primary_s1g_channel(8, 2, 44, 6));
	TEST_ASSERT_EQUAL(21, morse_calculate_primary_s1g_channel(8, 1, 28, 0));
	TEST_ASSERT_EQUAL(25, morse_calculate_primary_s1g_channel(8, 1, 28, 2));
	TEST_ASSERT_EQUAL(31, morse_calculate_primary_s1g_channel(8, 1, 28, 5));
	TEST_ASSERT_EQUAL(35, morse_calculate_primary_s1g_channel(8, 1, 28, 7));
	TEST_ASSERT_EQUAL(22, morse_calculate_primary_s1g_channel(8, 2, 28, 1));
	TEST_ASSERT_EQUAL(26, morse_calculate_primary_s1g_channel(8, 2, 28, 3));
	TEST_ASSERT_EQUAL(30, morse_calculate_primary_s1g_channel(8, 2, 28, 4));
	TEST_ASSERT_EQUAL(34, morse_calculate_primary_s1g_channel(8, 2, 28, 6));
	TEST_ASSERT_EQUAL(7,  morse_calculate_primary_s1g_channel(8, 1, 12, 1));
	TEST_ASSERT_EQUAL(11, morse_calculate_primary_s1g_channel(8, 1, 12, 3));
	TEST_ASSERT_EQUAL(13, morse_calculate_primary_s1g_channel(8, 1, 12, 4));
	TEST_ASSERT_EQUAL(17, morse_calculate_primary_s1g_channel(8, 1, 12, 6));
	TEST_ASSERT_EQUAL(6,  morse_calculate_primary_s1g_channel(8, 2, 12, 0));
	TEST_ASSERT_EQUAL(10, morse_calculate_primary_s1g_channel(8, 2, 12, 2));
	TEST_ASSERT_EQUAL(14, morse_calculate_primary_s1g_channel(8, 2, 12, 5));
	TEST_ASSERT_EQUAL(18, morse_calculate_primary_s1g_channel(8, 2, 12, 7));
	TEST_ASSERT_EQUAL(37, morse_calculate_primary_s1g_channel(4, 1, 40, 0));
	TEST_ASSERT_EQUAL(41, morse_calculate_primary_s1g_channel(4, 1, 40, 2));
	TEST_ASSERT_EQUAL(38, morse_calculate_primary_s1g_channel(4, 2, 40, 0));
	TEST_ASSERT_EQUAL(42, morse_calculate_primary_s1g_channel(4, 2, 40, 2));
	TEST_ASSERT_EQUAL(47, morse_calculate_primary_s1g_channel(4, 1, 48, 1));
	TEST_ASSERT_EQUAL(51, morse_calculate_primary_s1g_channel(4, 1, 48, 3));
	TEST_ASSERT_EQUAL(46, morse_calculate_primary_s1g_channel(4, 2, 48, 1));
	TEST_ASSERT_EQUAL(50, morse_calculate_primary_s1g_channel(4, 2, 48, 3));
	TEST_ASSERT_EQUAL(21, morse_calculate_primary_s1g_channel(4, 1, 24, 0));
	TEST_ASSERT_EQUAL(25, morse_calculate_primary_s1g_channel(4, 1, 24, 2));
	TEST_ASSERT_EQUAL(22, morse_calculate_primary_s1g_channel(4, 2, 24, 1));
	TEST_ASSERT_EQUAL(26, morse_calculate_primary_s1g_channel(4, 2, 24, 3));
	TEST_ASSERT_EQUAL(31, morse_calculate_primary_s1g_channel(4, 1, 32, 1));
	TEST_ASSERT_EQUAL(35, morse_calculate_primary_s1g_channel(4, 1, 32, 3));
	TEST_ASSERT_EQUAL(30, morse_calculate_primary_s1g_channel(4, 2, 32, 0));
	TEST_ASSERT_EQUAL(34, morse_calculate_primary_s1g_channel(4, 2, 32, 2));
	TEST_ASSERT_EQUAL(7,  morse_calculate_primary_s1g_channel(4, 1, 8, 1));
	TEST_ASSERT_EQUAL(11, morse_calculate_primary_s1g_channel(4, 1, 8, 3));
	TEST_ASSERT_EQUAL(6,  morse_calculate_primary_s1g_channel(4, 2, 8, 0));
	TEST_ASSERT_EQUAL(10, morse_calculate_primary_s1g_channel(4, 2, 8, 2));
	TEST_ASSERT_EQUAL(15, morse_calculate_primary_s1g_channel(4, 1, 16, 1));
	TEST_ASSERT_EQUAL(19, morse_calculate_primary_s1g_channel(4, 1, 16, 3));
	TEST_ASSERT_EQUAL(14, morse_calculate_primary_s1g_channel(4, 2, 16, 0));
	TEST_ASSERT_EQUAL(18, morse_calculate_primary_s1g_channel(4, 2, 16, 2));
	TEST_ASSERT_EQUAL(37, morse_calculate_primary_s1g_channel(2, 1, 38, 0));
	TEST_ASSERT_EQUAL(38, morse_calculate_primary_s1g_channel(2, 2, 38, 1));
	TEST_ASSERT_EQUAL(41, morse_calculate_primary_s1g_channel(2, 1, 42, 0));
	TEST_ASSERT_EQUAL(42, morse_calculate_primary_s1g_channel(2, 2, 42, 1));
	TEST_ASSERT_EQUAL(45, morse_calculate_primary_s1g_channel(2, 1, 46, 0));
	TEST_ASSERT_EQUAL(46, morse_calculate_primary_s1g_channel(2, 2, 46, 1));
	TEST_ASSERT_EQUAL(49, morse_calculate_primary_s1g_channel(2, 1, 50, 0));
	TEST_ASSERT_EQUAL(50, morse_calculate_primary_s1g_channel(2, 2, 50, 1));
	TEST_ASSERT_EQUAL(21, morse_calculate_primary_s1g_channel(2, 1, 22, 0));
	TEST_ASSERT_EQUAL(22, morse_calculate_primary_s1g_channel(2, 2, 22, 1));
	TEST_ASSERT_EQUAL(25, morse_calculate_primary_s1g_channel(2, 1, 26, 0));
	TEST_ASSERT_EQUAL(26, morse_calculate_primary_s1g_channel(2, 2, 26, 1));
	TEST_ASSERT_EQUAL(29, morse_calculate_primary_s1g_channel(2, 1, 30, 0));
	TEST_ASSERT_EQUAL(30, morse_calculate_primary_s1g_channel(2, 2, 30, 1));
	TEST_ASSERT_EQUAL(33, morse_calculate_primary_s1g_channel(2, 1, 34, 0));
	TEST_ASSERT_EQUAL(34, morse_calculate_primary_s1g_channel(2, 2, 34, 1));
	TEST_ASSERT_EQUAL(1,  morse_calculate_primary_s1g_channel(2, 1, 2, 0));
	TEST_ASSERT_EQUAL(2,  morse_calculate_primary_s1g_channel(2, 2, 2, 1));
	TEST_ASSERT_EQUAL(5,  morse_calculate_primary_s1g_channel(2, 1, 6, 0));
	TEST_ASSERT_EQUAL(6,  morse_calculate_primary_s1g_channel(2, 2, 6, 1));
	TEST_ASSERT_EQUAL(9,  morse_calculate_primary_s1g_channel(2, 1, 10, 0));
	TEST_ASSERT_EQUAL(10, morse_calculate_primary_s1g_channel(2, 2, 10, 1));
	TEST_ASSERT_EQUAL(13, morse_calculate_primary_s1g_channel(2, 1, 14, 0));
	TEST_ASSERT_EQUAL(14, morse_calculate_primary_s1g_channel(2, 2, 14, 1));
	TEST_ASSERT_EQUAL(17, morse_calculate_primary_s1g_channel(2, 1, 18, 0));
	TEST_ASSERT_EQUAL(18, morse_calculate_primary_s1g_channel(2, 2, 18, 1));
	TEST_ASSERT_EQUAL(37, morse_calculate_primary_s1g_channel(1, 1, 37, 0));
	TEST_ASSERT_EQUAL(41, morse_calculate_primary_s1g_channel(1, 1, 41, 0));
	TEST_ASSERT_EQUAL(43, morse_calculate_primary_s1g_channel(1, 1, 43, 0));
	TEST_ASSERT_EQUAL(51, morse_calculate_primary_s1g_channel(1, 1, 51, 0));
	TEST_ASSERT_EQUAL(23, morse_calculate_primary_s1g_channel(1, 1, 23, 0));
	TEST_ASSERT_EQUAL(29, morse_calculate_primary_s1g_channel(1, 1, 29, 0));
	TEST_ASSERT_EQUAL(1,  morse_calculate_primary_s1g_channel(1, 1, 1, 0));
	TEST_ASSERT_EQUAL(9,  morse_calculate_primary_s1g_channel(1, 1, 9, 0));
	TEST_ASSERT_EQUAL(17, morse_calculate_primary_s1g_channel(1, 1, 17, 0));
}

void test_morse_cc_get_sec_channel_offset(void)
{
	/* Get s1g secondary channel offset for JP */
	TEST_ASSERT_EQUAL(-13, morse_cc_get_sec_channel_offset(3, "JP"));
	TEST_ASSERT_EQUAL(-11, morse_cc_get_sec_channel_offset(1, "JP"));

	/* Get s1g seconday channel offset for all countries */
	TEST_ASSERT_EQUAL(-1, morse_cc_get_sec_channel_offset(3, "US"));
	TEST_ASSERT_EQUAL(1,  morse_cc_get_sec_channel_offset(1, "US"));
	TEST_ASSERT_EQUAL(-1, morse_cc_get_sec_channel_offset(3, "EU"));
	TEST_ASSERT_EQUAL(1,  morse_cc_get_sec_channel_offset(1, "EU"));
	TEST_ASSERT_EQUAL(-1, morse_cc_get_sec_channel_offset(3, "KR"));
	TEST_ASSERT_EQUAL(1,  morse_cc_get_sec_channel_offset(1, "KR"));
	TEST_ASSERT_EQUAL(-1, morse_cc_get_sec_channel_offset(3, "SG"));
	TEST_ASSERT_EQUAL(1,  morse_cc_get_sec_channel_offset(1, "SG"));
	TEST_ASSERT_EQUAL(-1, morse_cc_get_sec_channel_offset(3, "AU"));
	TEST_ASSERT_EQUAL(1,  morse_cc_get_sec_channel_offset(1, "AU"));
	TEST_ASSERT_EQUAL(-1, morse_cc_get_sec_channel_offset(3, "NZ"));
	TEST_ASSERT_EQUAL(1,  morse_cc_get_sec_channel_offset(1, "NZ"));
	TEST_ASSERT_EQUAL(-1, morse_cc_get_sec_channel_offset(3, "IN"));
	TEST_ASSERT_EQUAL(1,  morse_cc_get_sec_channel_offset(1, "IN"));
}

void test_morse_s1g_chan_to_ht20_prim_chan_jp(void)
{
	TEST_ASSERT_EQUAL(36, morse_s1g_chan_to_ht20_prim_chan(36, 13, "JP"));
	TEST_ASSERT_EQUAL(40, morse_s1g_chan_to_ht20_prim_chan(36, 15, "JP"));
	TEST_ASSERT_EQUAL(44, morse_s1g_chan_to_ht20_prim_chan(36, 17, "JP"));
	TEST_ASSERT_EQUAL(48, morse_s1g_chan_to_ht20_prim_chan(36, 19, "JP"));
	TEST_ASSERT_EQUAL(52, morse_s1g_chan_to_ht20_prim_chan(38, 15, "JP"));
	TEST_ASSERT_EQUAL(56, morse_s1g_chan_to_ht20_prim_chan(38, 17, "JP"));
	TEST_ASSERT_EQUAL(60, morse_s1g_chan_to_ht20_prim_chan(38, 19, "JP"));
	TEST_ASSERT_EQUAL(64, morse_s1g_chan_to_ht20_prim_chan(38, 21, "JP"));
	TEST_ASSERT_EQUAL(36, morse_s1g_chan_to_ht20_prim_chan(2, 13, "JP"));
	TEST_ASSERT_EQUAL(40, morse_s1g_chan_to_ht20_prim_chan(2, 15, "JP"));
	TEST_ASSERT_EQUAL(44, morse_s1g_chan_to_ht20_prim_chan(6, 17, "JP"));
	TEST_ASSERT_EQUAL(48, morse_s1g_chan_to_ht20_prim_chan(6, 19, "JP"));
	TEST_ASSERT_EQUAL(52, morse_s1g_chan_to_ht20_prim_chan(4, 15, "JP"));
	TEST_ASSERT_EQUAL(56, morse_s1g_chan_to_ht20_prim_chan(4, 17, "JP"));
	TEST_ASSERT_EQUAL(60, morse_s1g_chan_to_ht20_prim_chan(8, 19, "JP"));
	TEST_ASSERT_EQUAL(64, morse_s1g_chan_to_ht20_prim_chan(8, 21, "JP"));
	TEST_ASSERT_EQUAL(36, morse_s1g_chan_to_ht20_prim_chan(13, 13, "JP"));
	TEST_ASSERT_EQUAL(40, morse_s1g_chan_to_ht20_prim_chan(15, 15, "JP"));
	TEST_ASSERT_EQUAL(44, morse_s1g_chan_to_ht20_prim_chan(17, 17, "JP"));
	TEST_ASSERT_EQUAL(64, morse_s1g_chan_to_ht20_prim_chan(21, 21, "JP"));
}

void test_morse_s1g_chan_to_ht20_prim_chan_default(void)
{
	TEST_ASSERT_EQUAL(149, morse_s1g_chan_to_ht20_prim_chan(44, 37, "US"));
	TEST_ASSERT_EQUAL(153, morse_s1g_chan_to_ht20_prim_chan(40, 39, "US"));
	TEST_ASSERT_EQUAL(157, morse_s1g_chan_to_ht20_prim_chan(42, 41, "US"));
	TEST_ASSERT_EQUAL(161, morse_s1g_chan_to_ht20_prim_chan(43, 43, "US"));
	TEST_ASSERT_EQUAL(165, morse_s1g_chan_to_ht20_prim_chan(48, 45, "US"));
	TEST_ASSERT_EQUAL(169, morse_s1g_chan_to_ht20_prim_chan(46, 47, "US"));
	TEST_ASSERT_EQUAL(173, morse_s1g_chan_to_ht20_prim_chan(50, 49, "US"));
	TEST_ASSERT_EQUAL(177, morse_s1g_chan_to_ht20_prim_chan(51, 51, "US"));
	TEST_ASSERT_EQUAL(100, morse_s1g_chan_to_ht20_prim_chan(28, 21, "US"));
	TEST_ASSERT_EQUAL(104, morse_s1g_chan_to_ht20_prim_chan(24, 23, "US"));
	TEST_ASSERT_EQUAL(108, morse_s1g_chan_to_ht20_prim_chan(26, 25, "US"));
	TEST_ASSERT_EQUAL(112, morse_s1g_chan_to_ht20_prim_chan(27, 27, "US"));
	TEST_ASSERT_EQUAL(116, morse_s1g_chan_to_ht20_prim_chan(32, 29, "US"));
	TEST_ASSERT_EQUAL(120, morse_s1g_chan_to_ht20_prim_chan(30, 31, "US"));
	TEST_ASSERT_EQUAL(124, morse_s1g_chan_to_ht20_prim_chan(34, 33, "US"));
	TEST_ASSERT_EQUAL(128, morse_s1g_chan_to_ht20_prim_chan(35, 35, "US"));
	TEST_ASSERT_EQUAL(132, morse_s1g_chan_to_ht20_prim_chan(2, 1, "US"));
	TEST_ASSERT_EQUAL(136, morse_s1g_chan_to_ht20_prim_chan(3, 3, "US"));
	TEST_ASSERT_EQUAL(36,  morse_s1g_chan_to_ht20_prim_chan(12, 5, "US"));
	TEST_ASSERT_EQUAL(40,  morse_s1g_chan_to_ht20_prim_chan(8, 7, "US"));
	TEST_ASSERT_EQUAL(44,  morse_s1g_chan_to_ht20_prim_chan(10, 9, "US"));
	TEST_ASSERT_EQUAL(48,  morse_s1g_chan_to_ht20_prim_chan(11, 11, "US"));
	TEST_ASSERT_EQUAL(52,  morse_s1g_chan_to_ht20_prim_chan(16, 13, "US"));
	TEST_ASSERT_EQUAL(56,  morse_s1g_chan_to_ht20_prim_chan(14, 15, "US"));
	TEST_ASSERT_EQUAL(60,  morse_s1g_chan_to_ht20_prim_chan(18, 17, "US"));
	TEST_ASSERT_EQUAL(64,  morse_s1g_chan_to_ht20_prim_chan(19, 19, "US"));

}

void test_morse_ht_chan_offset_jp(void)
{
	/* Get offset for ht channels */
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(36, -2, 1));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(40, -2, 1));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(44, -2, 1));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(48, -2, 1));
	TEST_ASSERT_EQUAL(12, morse_ht_chan_offset_jp(52, -2, 1));
	TEST_ASSERT_EQUAL(12, morse_ht_chan_offset_jp(56, -2, 1));
	TEST_ASSERT_EQUAL(12, morse_ht_chan_offset_jp(60, -2, 1));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(64, -2, 1));

	/* Get offset for S1G channels */
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(36, 13, 0));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(36, 19, 0));
	TEST_ASSERT_EQUAL(12, morse_ht_chan_offset_jp(38, 17, 0));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(38, 21, 0));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(2, 15, 0));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(6, 17, 0));
	TEST_ASSERT_EQUAL(12, morse_ht_chan_offset_jp(4, 15, 0));
	TEST_ASSERT_EQUAL(12, morse_ht_chan_offset_jp(8, 19, 0));
	TEST_ASSERT_EQUAL(0,  morse_ht_chan_offset_jp(8, 21, 0));
}

void test_morse_s1g_get_start_freq_for_country(void)
{
	/* Get the start frequency for JP */
	TEST_ASSERT_EQUAL(916500, morse_s1g_get_start_freq_for_country("JP", 923000, 1));
	TEST_ASSERT_EQUAL(922500, morse_s1g_get_start_freq_for_country("JP", 924500, 2));
	TEST_ASSERT_EQUAL(906500, morse_s1g_get_start_freq_for_country("JP", 925500, 4));
	/* Get the start frequency for AU/NZ/US */
	TEST_ASSERT_EQUAL(902000, morse_s1g_get_start_freq_for_country("AU", 924000, 8));
	TEST_ASSERT_EQUAL(902000, morse_s1g_get_start_freq_for_country("NZ", 922000, 4));
	TEST_ASSERT_EQUAL(902000, morse_s1g_get_start_freq_for_country("US", 913000, 2));
	TEST_ASSERT_EQUAL(902000, morse_s1g_get_start_freq_for_country("US", 925500, 1));
	/* Get the start frequency for EU */
	TEST_ASSERT_EQUAL(901400, morse_s1g_get_start_freq_for_country("EU", 922000, 4));
	TEST_ASSERT_EQUAL(863000, morse_s1g_get_start_freq_for_country("EU", 866500, 1));
	/* Get the start frequency for GB */
	TEST_ASSERT_EQUAL(901400, morse_s1g_get_start_freq_for_country("GB", 922000, 4));
	TEST_ASSERT_EQUAL(863000, morse_s1g_get_start_freq_for_country("GB", 866500, 1));
	/* Get the start frequency for IN */
	TEST_ASSERT_EQUAL(863000, morse_s1g_get_start_freq_for_country("IN", 867500, 1));
	/* Get the start frequency for KR */
	TEST_ASSERT_EQUAL(917500, morse_s1g_get_start_freq_for_country("KR", 921500, 8));
	/* Get the start frequency for SG */
	TEST_ASSERT_EQUAL(902000, morse_s1g_get_start_freq_for_country("SG", 933000, 2));
	TEST_ASSERT_EQUAL(863000, morse_s1g_get_start_freq_for_country("SG", 868500, 1));
}

int main(void)
{
	UNITY_BEGIN();

	/* Test the default channel map (AU/US etc)*/
	morse_set_s1g_ht_chan_pairs("NULL");
	RUN_TEST(verify_chan_pairs_default);
	RUN_TEST(test_morse_ht_chan_to_s1g_chan_default);
	RUN_TEST(test_morse_ht_freq_to_s1g_chan_default);
	RUN_TEST(test_morse_s1g_chan_to_ht_chan_default);
	RUN_TEST(test_morse_s1g_chan_to_bw_default);
	RUN_TEST(test_morse_s1g_oper_class_chan_to_freq_default);
	RUN_TEST(test_morse_s1g_op_class_ht_chan_to_s1g_freq_default);
	RUN_TEST(test_morse_s1g_op_class_ht_freq_to_s1g_freq_default);
	RUN_TEST(test_morse_cc_ht_freq_to_s1g_freq_default);
	RUN_TEST(test_morse_s1g_chan_to_ht20_prim_chan_default);

	/* Test alternate channel map for JP */
	morse_set_s1g_ht_chan_pairs("JP");
	RUN_TEST(verify_chan_pairs_jp);
	RUN_TEST(test_morse_ht_freq_to_s1g_chan_jp);
	RUN_TEST(test_morse_s1g_chan_to_ht_chan_jp);
	RUN_TEST(test_morse_s1g_chan_to_bw_jp);
	RUN_TEST(test_morse_s1g_oper_class_chan_to_freq_jp);
	RUN_TEST(test_morse_s1g_op_class_ht_chan_to_s1g_freq_jp);
	RUN_TEST(test_morse_s1g_op_class_ht_freq_to_s1g_freq_jp);
	RUN_TEST(test_morse_cc_ht_freq_to_s1g_freq_jp);
	RUN_TEST(test_morse_s1g_chan_to_ht20_prim_chan_jp);
	RUN_TEST(test_morse_ht_chan_offset_jp);

	/* Channel map independent-functions */
	RUN_TEST(test_morse_ht_chan_to_ht_chan_center);
	RUN_TEST(test_morse_ht_center_chan_to_ht_chan);
	RUN_TEST(test_morse_s1g_oper_class_to_ch_width);
	RUN_TEST(test_morse_s1g_oper_class_to_country);
	RUN_TEST(test_morse_s1g_op_class_first_chan);
	RUN_TEST(test_morse_s1g_verify_oper_class_country);
	RUN_TEST(test_morse_s1g_verify_oper_class_country_channel);
	RUN_TEST(test_morse_cc_verify_get_primary_s1g_channel);
	RUN_TEST(test_morse_cc_get_sec_channel_offset);
	RUN_TEST(test_morse_s1g_get_start_freq_for_country);

	return UNITY_END();
}
