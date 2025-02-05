/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2021, Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Developed by:
 *
 *                 AMD Research and AMD Software Development
 *
 *                 Advanced Micro Devices, Inc.
 *
 *                 www.amd.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimers.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimers in
 *    the documentation and/or other materials provided with the distribution.
 *  - Neither the names of <Name of Development Group, Name of Institution>,
 *    nor the names of its contributors may be used to endorse or promote
 *    products derived from this Software without specific prior written
 *    permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS WITH THE SOFTWARE.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <esmi_oob/apml64Config.h>
#include <esmi_oob/esmi_cpuid_msr.h>
#include <esmi_oob/apml.h>
#include <esmi_oob/esmi_mailbox.h>
#include <esmi_oob/esmi_rmi.h>
#include <esmi_oob/esmi_tsi.h>

#define RED "\x1b[31m"
#define RESET "\x1b[0m"
#define ARGS_MAX 64
#define APML_SLEEP 10000
#define SCALING_FACTOR	0.25
/* CPUID function for max threads per l3 */
#define THREADS_L3_FUNC         0x8000001D
/* CPUID extended function for max threads per l3 */
#define THREADS_L3_EXTD         0x3

static int flag;

static oob_status_t apml_get_sockpower(uint8_t soc_num)
{
	uint32_t power = 0;
	oob_status_t ret;

	ret = read_socket_power(soc_num, &power);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get power, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("---------------------------------------------");
	printf("\n| Power (Watts)\t\t |");
	printf(" %-17.3f|", (double)power/1000);

	/* Get the PowerLimit for a given soc_num index */
	ret = read_socket_power_limit(soc_num, &power);
	if (ret != OOB_SUCCESS) {
		printf("\nFailed to get powerlimit, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("\n| PowerLimit (Watts)\t |");
	printf(" %-17.3f|", (double)power/1000);

	/* Get the maxpower for a given soc_num index */
	ret = read_max_socket_power_limit(soc_num, &power);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get maxpower, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("\n| PowerLimitMax (Watts)\t |");
	printf(" %-17.3f|", (double)power/1000);
	printf("\n---------------------------------------------\n");

	return OOB_SUCCESS;
}

static oob_status_t apml_get_socktdp(uint8_t soc_num)
{
	uint32_t buffer = 0;
	oob_status_t ret;

	ret = read_tdp(soc_num, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get tdp, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("---------------------------------------------\n");
	printf("| TDP (Watts)\t\t| %-17.03f |\n", (double)buffer/1000);

	/* Get min tdp value for a given soc_num */
	ret = read_min_tdp(soc_num, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get min tdp, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("| Min_TDP (Watts)\t| %-17.03f |\n", (double)buffer/1000);

	/* Get max tdp value for a given soc_num */
	ret = read_max_tdp(soc_num, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get max_tdp, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("| Max_TDP (Watts)\t| %-17.03f |\n", (double)buffer/1000);
	printf("---------------------------------------------\n");

	return OOB_SUCCESS;
}

static oob_status_t apml_setpower_limit(uint8_t soc_num,
					uint32_t power)
{
	uint32_t max_power = 0;
	oob_status_t ret;

	ret = read_max_socket_power_limit(soc_num, &max_power);
	if ((ret == OOB_SUCCESS) && (power > max_power)) {
		printf("Input power is not within accepted limit,\n"
			"So value set to default max %.3f Watts\n",
			(double)max_power/1000);
		power = max_power;
	}
	ret = write_socket_power_limit(soc_num, power);
	if (ret != OOB_SUCCESS) {
		printf("Failed to set power_limit, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("\nSet power_limit : %16.03f Watts "
		"successfully\n", (double)power/1000);
	return OOB_SUCCESS;
}

static void apml_get_ddr_bandwidth(uint8_t soc_num)
{
	struct max_ddr_bw max_ddr;
	oob_status_t ret;

	ret = read_ddr_bandwidth(soc_num, &max_ddr);
	if (ret != OOB_SUCCESS) {
		printf("Failed:to get DDR Bandwidth, "
		       "Err[%d]:%s\n", ret,
		       esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------------------");
	printf("\n| DDR Max BW (GB/s)\t |");
	printf(" %-17d|", max_ddr.max_bw);
	printf("\n| DDR Utilized BW (GB/s) |");
	printf(" %-17d|", max_ddr.utilized_bw);
	printf("\n| DDR Utilized Percent(%%)|");
	printf(" %-17d|", max_ddr.utilized_pct);
	printf("\n---------------------------------------------\n");
}

static oob_status_t get_boostlimit(uint8_t soc_num,
				   uint32_t core_id)
{
	uint32_t buffer = 0;
	oob_status_t ret;

	ret = read_esb_boost_limit(soc_num, core_id, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to get core[%d] apml_boostlimit,"
		       " Err[%d]: %s\n", core_id, ret,
		       esmi_get_err_msg(ret));
		return ret;
	}

	printf("------------------------------------------------------"
		"-------\n");
	printf("| core[%03d] apml_boostlimit (MHz)\t | %-17u|\n",
	       core_id, buffer);

	usleep(APML_SLEEP);
	/* Get the Bios boostlimit for a given soc_num index */
	ret = read_bios_boost_fmax(soc_num, core_id, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get core[%d] bios_boostlimit, "
		       "Err[%d]: %s\n", core_id, ret,
			esmi_get_err_msg(ret));
		return ret;
	}
	printf("| core[%03d] bios_boostlimit (MHz)\t | %-17u|\n",
		core_id, buffer);
	printf("------------------------------------------------------"
		"-------\n");

	return OOB_SUCCESS;
}

static oob_status_t set_apml_boostlimit(uint8_t soc_num, uint32_t core_id,
					uint32_t boostlimit)
{
	oob_status_t ret;

	ret = write_esb_boost_limit(soc_num, core_id, boostlimit);
	if (ret != OOB_SUCCESS) {
		printf("Failed to set core[%d] apml_boostlimit "
		       "Err[%d]: %s\n", core_id, ret,
			esmi_get_err_msg(ret));
		return ret;
	}

	printf("core[%d] apml_boostlimit set successfully\n", core_id);
	return OOB_SUCCESS;
}

static oob_status_t set_apml_socket_boostlimit(uint8_t soc_num,
					       uint32_t boostlimit)
{
	oob_status_t ret;

	ret = write_esb_boost_limit_allcores(soc_num, boostlimit);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set apml_boostlimit for all cores Err[%d]: "
		       "%s\n", ret, esmi_get_err_msg(ret));
		return ret;
	}

	printf("apml_boostlimit for all cores set successfully\n");
	return OOB_SUCCESS;
}

static oob_status_t set_and_verify_dram_throttle(uint8_t soc_num,
						 uint32_t dram_thr)
{
	uint32_t limit = 0;
	oob_status_t ret;

	ret = write_dram_throttle(soc_num, dram_thr);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set DRAM throttle, Err[%d]: %s\n",
		       ret, esmi_get_err_msg(ret));
		return ret;
	}
	usleep(APML_SLEEP);
	ret = read_dram_throttle(soc_num, &limit);
	if (ret == OOB_SUCCESS) {
		if (limit < dram_thr)
			printf("Set to max dram throttle: %u %%\n", limit);
		else if (limit > dram_thr)
			printf("Set to min dram throttle: %u %%\n", limit);
	}
	printf("Set and Verify Success %u %%\n", limit);
	return OOB_SUCCESS;
}

static oob_status_t set_and_verify_apml_socket_uprate(uint8_t soc_num,
						      float uprate)
{
	float rduprate;
	oob_status_t ret;

	ret = write_sbtsi_updaterate(soc_num, uprate);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set Update rate for addr, "
		       "Err[%d]: %s\n", ret, esmi_get_err_msg(ret));
		return ret;
	}
	usleep(APML_SLEEP);

	if (read_sbtsi_updaterate(soc_num, &rduprate) == 0) {
		if (uprate != rduprate)
			return OOB_TRY_AGAIN;
		printf("Set and verify Success %f\n", rduprate);
	}

	return OOB_SUCCESS;
}

static oob_status_t set_high_temp_threshold(uint8_t soc_num, float temp)
{
	float temp_dec;
	int temp_int;
	oob_status_t ret;

	ret = sbtsi_set_hitemp_threshold(soc_num, temp);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set Higher Temp threshold limit, "
		       "Err[%d]: %s\n", ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Set Success\n");
	return OOB_SUCCESS;
}

static oob_status_t set_low_temp_threshold(uint8_t soc_num, float temp)
{
	float temp_dec;
	int temp_int;
	oob_status_t ret;

	if (temp < 0 || temp > 70) {
		printf("Invalid temp, please mention temp between 0 and 70\n");
		return OOB_INVALID_INPUT;
	}

	ret = sbtsi_set_lotemp_threshold(soc_num, temp);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set Lower Temp threshold limit, "
		       "Err[%d]: %s\n", ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Set Success\n");
	return OOB_SUCCESS;
}

static oob_status_t set_temp_offset(uint8_t soc_num, float temp)
{
	oob_status_t ret;

	ret = write_sbtsi_cputempoffset(soc_num, temp);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set Temp offset, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Set CPU temp offset success\n");
	return OOB_SUCCESS;
}

static oob_status_t set_timeout_config(uint8_t soc_num, int value)
{
	oob_status_t ret;

	ret = sbtsi_set_timeout_config(soc_num, value);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set timeout config, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Set timeout config success\n");
	return OOB_SUCCESS;
}

static oob_status_t set_alert_threshold(uint8_t soc_num, int value)
{
	oob_status_t ret;

	ret = sbtsi_set_alert_threshold(soc_num, value);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set alert threshold sample, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Set alert threshold success\n");
	return OOB_SUCCESS;
}

static oob_status_t set_alert_config(uint8_t soc_num, int value)
{
	oob_status_t ret;

	ret = sbtsi_set_alert_config(soc_num, value);
	if (ret != OOB_SUCCESS) {
		printf("Failed: to set alert config, Err[%d]: %s\n",
			ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Set alert config success\n");
	return OOB_SUCCESS;
}

static oob_status_t set_tsi_config(uint8_t soc_num, int value,
				   const char check)
{
	oob_status_t ret;

	switch (check) {
	case 'k':
		ret = sbtsi_set_configwr(soc_num, value,
					 ALERTMASK_MASK);
		if (ret != OOB_SUCCESS) {
			printf("Failed: to set tsi config alert_mask, "
				"Err[%d]: %s\n", ret,
				esmi_get_err_msg(ret));
			return ret;
		}
		printf("ALERT_L pin %s\n", value ? "Disabled" : "Enabled");
		break;
	case 'm':
		ret = sbtsi_set_configwr(soc_num, value,
					 RUNSTOP_MASK);
		if (ret != OOB_SUCCESS) {
			printf("Failed: to set tsi config runstop_mask, "
				"Err[%d]: %s\n", ret,
				esmi_get_err_msg(ret));
			return ret;
		}
		printf("runstop bit %s\n", value ? "Comparisions Disabled"
		       : "Comparisions Enabled");
		break;
	case 'n':
		ret = sbtsi_set_configwr(soc_num, value,
					   READORDER_MASK);
		if (ret != OOB_SUCCESS) {
			printf("Failed: to set tsi config readorder_mask, "
				"Err[%d]: %s\n", ret,
				esmi_get_err_msg(ret));
			return ret;
		}
		printf("Atomic read bit %s\n", value ? "Decimal Latches "
			"Integer" : "Integer Latches Decimal");
		break;
	case 'o':
		ret = sbtsi_set_configwr(soc_num, value,
					   ARA_MASK);
		if (ret != OOB_SUCCESS) {
			printf("Failed: to set tsi config ara_mask, "
				"Err[%d]: %s\n", ret, esmi_get_err_msg(ret));
			return ret;
		}
		printf("ARA Disable bit %s\n", value ? "Disabled" : "Enabled");
	}
	return OOB_SUCCESS;
}

static oob_status_t get_apml_rmi_access(uint8_t soc_num)
{
	int i, range;
	uint8_t buf, rev;
	uint8_t *buffer;
	oob_status_t ret;

	printf("------------------------------------------------------------"
		"----\n");
	printf("\n\t\t\t *** SB-RMI REGISTER SUMMARY ***\n");
	printf("------------------------------------------------------------"
		"----\n");
	printf("\t FUNCTION [register] \t\t\t| Value [Units]\n");
	printf("------------------------------------------------------------"
		"----\n");
	ret = read_sbrmi_revision(soc_num, &rev);
	if (ret != 0) {
		printf("Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("_RMI_REVISION [0x%x]		\t\t| %#4x\n",
	       SBRMI_REVISION, rev);

	usleep(APML_SLEEP);
	if (read_sbrmi_control(soc_num, &buf) == 0)
		printf("_RMI_CONTROL [0x%x]		\t\t| %#4x\n",
		       SBRMI_CONTROL, buf);

	usleep(APML_SLEEP);
	if (read_sbrmi_status(soc_num, &buf) == 0)
		printf("_RMI_STATUS [0x%x]		\t\t| %#4x\n",
		       SBRMI_STATUS, buf);

	usleep(APML_SLEEP);
	if (read_sbrmi_readsize(soc_num, &buf) == 0)
		printf("_RMI_READSIZE [0x%x]		\t\t| %#4x\n",
		       SBRMI_READSIZE, buf);

	usleep(APML_SLEEP);
	if (rev == 0x10)
		range = sizeof(thread_en_reg_v10);
	else
		range = sizeof(thread_en_reg_v20);
	buffer = malloc(range * sizeof(uint8_t));
	if (read_sbrmi_multithreadenablestatus(soc_num,
					       buffer) == 0) {
		printf("_RMI_THREADENSTATUS \t\t\t\t|\n");
		for (i = 0; i < range; i++)
			printf("\t[0x%x] Thread[%d:%d]	\t\t| %#4x\n",
			       thread_en_reg_v20[i], (i * 8) + 7, i * 8, buffer[i]);
	}
	free(buffer);

	usleep(APML_SLEEP);
	if (rev == 0x10)
		range = sizeof(alert_status_v10);
	else
		range = sizeof(alert_status_v20);
	buffer = malloc(range * sizeof(uint8_t));
	if (!buffer)
		return OOB_NO_MEMORY;
	if (read_sbrmi_alert_status(soc_num, buffer) == 0) {
		if (rev == 0x10) {
			printf("_RMI_ALERTSTATUS [0x%x ~ 0x%x]	\t\t|\n",
			       SBRMI_ALERTSTATUS0, SBRMI_ALERTSTATUS15);
			for (i = 0; i < range; i++)
				printf("\tThread[%d, %d, %d, %d]	\t\t| %#4x\n",
				       i + range * 3, i + range * 2, i + range, i, buffer[i]);
		} else {
			printf("_RMI_ALERTSTATUS [0x%x ~ 0x%x] [0x%x ~ 0x%x] \t|\n",
			       SBRMI_ALERTSTATUS0, SBRMI_ALERTSTATUS15,
			       SBRMI_ALERTSTATUS16, SBRMI_ALERTSTATUS31);
			for (i = 0; i < range; i++) {
				printf("\t[ ");
				for (int j = 15; j >= 0; j--) {
					switch (j % 16) {
					case 4 ... 7:
						if (i / 16)
							printf("%d ", 16 * (j % 16) + (i - 16));
						break;
					case 0 ... 3:
					case 8 ... 11:
						if (i / 16 == 0)
							printf("%d ", 16 * (j % 16) + i);
					}
				}
				if (i < 10)
					printf("] \t\t| %#4x\n", buffer[i]);
				else if (i <  16)
					printf("] \t| %#4x\n", buffer[i]);
				else
					printf("] \t\t\t| %#4x\n", buffer[i]);
			}
		}
	}
	free(buffer);

	usleep(APML_SLEEP);
	if (rev == 0x10)
		range = sizeof(alert_mask_v10);
	else
		range = sizeof(alert_mask_v20);
	buffer = malloc(range * sizeof(uint8_t));
	if (!buffer)
		return OOB_NO_MEMORY;
	if (read_sbrmi_alert_mask(soc_num, buffer) == 0) {
		if (rev == 0x10) {
			printf("_RMI_ALERTMASK [0x%x ~ 0x%x] \t\t\t|\n",
			       SBRMI_ALERTMASK0, SBRMI_ALERTMASK15);
			for (i = 0; i < range; i++)
				printf("\tThread[%d, %d, %d, %d]	\t\t| %#4x\n",
				       i + range * 3, i + range * 2, i + range, i, buffer[i]);
		} else {
			printf("_RMI_ALERTMASK [0x%x ~ 0x%x] [0x%x ~ 0x%x] \t|\n",
			       SBRMI_ALERTMASK0, SBRMI_ALERTMASK15,
			       SBRMI_ALERTMASK16, SBRMI_ALERTMASK31);
			for (i = 0; i < range; i++) {
				printf("\t[ ");
				for (int j = 15; j >= 0; j--) {
					switch (j % 16) {
					case 4 ... 7:
						if (i / 16)
							printf("%d ", 16 * (j % 16) + (i - 16));
						break;
					case 0 ... 3:
					case 8 ... 11:
						if (i / 16 == 0)
							printf("%d ", 16 * (j % 16) + i);
					}
				}
				if (i < 10)
					printf("] \t\t| %#4x\n", buffer[i]);
				else if (i <  16)
					printf("] \t| %#4x\n", buffer[i]);
				else
					printf("] \t\t\t| %#4x\n", buffer[i]);
			}
		}
	}
	free(buffer);

	usleep(APML_SLEEP);
	range = SBRMI_OUTBNDMSG7 - SBRMI_OUTBNDMSG0 + 1;
	buffer = malloc(range * sizeof(uint8_t));
	if (!buffer)
		return OOB_NO_MEMORY;
	if (read_sbrmi_outbound_msg(soc_num, buffer) == 0) {
		printf("_RMI_OUTBOUNDMSG [0x%x ~ 0x%x]	\t\t|\n",
		       SBRMI_OUTBNDMSG0, SBRMI_OUTBNDMSG7);
		for (i = 0; i < range; i++)
			printf("\tOUTBNDMSG[%d]	\t\t\t| %#4x\n", i, buffer[i]);
	}
	free(buffer);

	usleep(APML_SLEEP);
	range = SBRMI_INBNDMSG7 - SBRMI_INBNDMSG0 + 1;
	buffer = malloc(range * sizeof(uint8_t));
	if (!buffer)
		return OOB_NO_MEMORY;
	if (read_sbrmi_inbound_msg(soc_num, buffer) == 0) {
		printf("_RMI_INBOUNDMSG [0x%x ~ 0x%x]	\t\t|\n",
		       SBRMI_INBNDMSG0, SBRMI_INBNDMSG7);
		for (i = 0; i < range; i++)
			printf("\tINBNDMSG[%d]	\t\t\t| %#4x\n", i, buffer[i]);
	}
	free(buffer);

	usleep(APML_SLEEP);
	if (read_sbrmi_swinterrupt(soc_num, &buf) == 0)
		printf("_RMI_SWINTERRUPT [0x%x]	\t\t\t| %#4x\n",
		       SBRMI_SOFTWAREINTERRUPT, buf);

	usleep(APML_SLEEP);
	if (rev == 0x10) {
		if (read_sbrmi_threadnumber(soc_num, &buf) == 0)
			printf("_RMI_THREADNUMEBER [0x%x]	\t\t| %#4x\n",
			       SBRMI_THREADNUMBER, buf);
	} else {
		if (read_sbrmi_threadnumberlow(soc_num, &buf) == 0)
			printf("_RMI_THREADNUMEBERLOW [0x%x]	\t\t| %#4x\n",
			       SBRMI_THREADNUMBERLOW, buf);
		if (read_sbrmi_threadnumberhi(soc_num, &buf) == 0)
			printf("_RMI_THREADNUMEBERHIGH [0x%x]	\t\t| %#4x\n",
			       SBRMI_THREADNUMBERHIGH, buf);
	}

	usleep(APML_SLEEP);
	if (read_sbrmi_thread_cs(soc_num, &buf) == 0)
		printf("_RMI_THREADCS [0x%x]	\t\t\t| %#4x\n",
		       SBRMI_THREAD128CS, buf);

	usleep(APML_SLEEP);
	if (read_sbrmi_ras_status(soc_num, &buf) == 0)
		printf("_RMI_RASSTATUS [0x%x]	\t\t\t| %#4x\n",
		       SBRMI_RASSTATUS, buf);

	usleep(APML_SLEEP);
	range = SBRMI_MP0OUTBNDMSG7 - SBRMI_MP0OUTBNDMSG0 + 1;
	buffer = malloc(range * sizeof(uint8_t));
	if (!buffer)
		return OOB_NO_MEMORY;
	if (read_sbrmi_mp0_msg(soc_num, buffer) == 0) {
		printf("_RMI_MP0 [0x%x ~ 0x%x]	\t\t\t|\n",
		       SBRMI_MP0OUTBNDMSG0, SBRMI_MP0OUTBNDMSG7);
		for (i = 0; i < range; i++)
			printf("\tOUTBNDMSG[%d]	\t\t\t| %#4x\n", i, buffer[i]);
	}
	free(buffer);
	printf("------------------------------------------------------------"
		"----\n");
	return OOB_SUCCESS;
}

static oob_status_t get_apml_tsi_register_descriptions(uint8_t soc_num)
{
	float temp_value[3];
	float dec;
	float uprate;
	uint8_t lowalert, hialert;
	uint8_t al_mask, run_stop, read_ord, ara;
	uint8_t timeout;
	uint8_t intr;
	int8_t intr_offset;
	uint8_t id, buf;
	oob_status_t ret;

	usleep(APML_SLEEP);
	ret = sbtsi_get_cputemp(soc_num, &temp_value[0]);
	if (ret)
		return ret;

	usleep(APML_SLEEP);
	ret = read_sbtsi_cpuinttemp(soc_num, &intr);
	if (ret)
		return ret;
	ret = read_sbtsi_cputempdecimal(soc_num, &dec);
	if (ret)
		return ret;

	printf("\n\t\t *** SB-TSI REGISTER SUMMARY ***\n");
	printf("------------------------------------------------------------"
		"----\n");
	printf("\t FUNCTION [register] \t| \tValue [Units]\n");
	printf("------------------------------------------------------------"
		"----\n");
	printf("_CPUTEMP\t\t\t| %.3f °C\n", temp_value[0]);
	printf("\tCPU_INT [0x%x]\t\t| %u °C\n", SBTSI_CPUTEMPINT, intr);
	printf("\tCPU_DEC [0x%x]\t\t| %.3f °C\n", SBTSI_CPUTEMPDEC, dec);

	usleep(APML_SLEEP);
	ret = sbtsi_get_temp_status(soc_num, &lowalert, &hialert);
	if (ret)
		return ret;
	printf("_STATUS [0x%x]\t\t\t| ", SBTSI_STATUS);
	if (lowalert)
		printf("CPU Temp Low Alert\n");
	else if (hialert)
		printf("CPU Temp Hi Alert\n");
	else
		printf("No Temp Alert\n");

	usleep(APML_SLEEP);
	ret = sbtsi_get_config(soc_num, &al_mask, &run_stop,
			       &read_ord, &ara);
	if (ret)
		return ret;

	printf("_CONFIG [0x%x]\t\t\t|\n", SBTSI_CONFIGURATION);
	printf("\tALERT_L pin\t\t| %s\n", al_mask ? "Disabled" : "Enabled");
	printf("\tRunstop\t\t\t| %s\n", run_stop ? "Comparison Disabled" :
		"Comparison Enabled");
	printf("\tAtomic Rd order\t\t| %s\n", read_ord ? "Decimal Latches "
		"Integer" : "Integer latches Decimal");
	printf("\tARA response\t\t| %s\n", ara ? "Disabled" : "Enabled");

	usleep(APML_SLEEP);
	ret = read_sbtsi_updaterate(soc_num, &uprate);
	if (ret)
		return ret;
	printf("_TSI_UPDATERATE [0x%x]\t\t| %.3f Hz\n", SBTSI_UPDATERATE,
		uprate);

	usleep(APML_SLEEP);
	ret = sbtsi_get_hitemp_threshold(soc_num, &temp_value[1]);
	if (ret)
		return ret;

	usleep(APML_SLEEP);
	ret = read_sbtsi_hitempint(soc_num, &intr);
	if (ret)
		return ret;

	usleep(APML_SLEEP);
	ret = read_sbtsi_hitempdecimal(soc_num, &dec);
	if (ret)
		return ret;

	printf("_HIGH_THRESHOLD_TEMP\t\t| %.3f °C\n", temp_value[1]);
	printf("\tHIGH_INT [0x%x]\t\t| %u °C\n", SBTSI_HITEMPINT, intr);
	printf("\tHIGH_DEC [0x%x]\t\t| %.3f °C\n", SBTSI_HITEMPDEC, dec);

	usleep(APML_SLEEP);
	ret = sbtsi_get_lotemp_threshold(soc_num, &temp_value[2]);
	if (ret)
		return ret;

	usleep(APML_SLEEP);
	ret = read_sbtsi_lotempint(soc_num, &intr);
	if (ret)
		return ret;
	ret = read_sbtsi_lotempdecimal(soc_num, &dec);
	if (ret)
		return ret;
	printf("_LOW_THRESHOLD_TEMP \t\t| %.3f °C\n", temp_value[2]);
	printf("\tLOW_INT [0x%x]\t\t| %u °C\n", SBTSI_LOTEMPINT, intr);
	printf("\tLOW_DEC [0x%x]\t\t| %.3f °C\n", SBTSI_LOTEMPDEC, dec);

	usleep(APML_SLEEP);
	ret = read_sbtsi_cputempoffset(soc_num, &dec);
	if (ret)
		return ret;
	printf("_TEMP_OFFSET\t\t\t| %.3f °C\n", dec);

	usleep(APML_SLEEP);
	ret = read_sbtsi_cputempoffint(soc_num, &intr_offset);
	if (ret)
		return ret;

	usleep(APML_SLEEP);
	ret = read_sbtsi_cputempoffdec(soc_num, &dec);
	if (ret)
		return ret;
	printf("\tOFF_INT [0x%x]\t\t| %d °C\n", SBTSI_CPUTEMPOFFINT, intr_offset);
	printf("\tOFF_DEC [0x%x]\t\t| %.3f °C\n", SBTSI_CPUTEMPOFFDEC, dec);

	usleep(APML_SLEEP);
	ret = sbtsi_get_timeout(soc_num, &timeout);
	if (ret)
		return ret;
	printf("_TIMEOUT_CONFIG [0x%x]\t\t| %s\n",
	       SBTSI_TIMEOUTCONFIG, timeout ? "Enabled" : "Disabled");

	usleep(APML_SLEEP);
	ret = read_sbtsi_alertthreshold(soc_num, &buf);
	if (ret)
		return ret;
	printf("_THRESHOLD_SAMPLE [0x%x]\t| %d\n", SBTSI_ALERTTHRESHOLD, buf);

	usleep(APML_SLEEP);
	ret = read_sbtsi_alertconfig(soc_num, &buf);
	if (ret)
		return ret;
	printf("_TSI_ALERT_CONFIG [0x%x]\t| %s\n",
	       SBTSI_ALERTCONFIG, buf ? "Enabled" : "Disabled");

	usleep(APML_SLEEP);
	ret = read_sbtsi_manufid(soc_num, &id);
	if (ret)
		return ret;
	printf("_TSI_MANUFACTURE_ID [0x%x]\t| %#x\n", SBTSI_MANUFID, id);

	usleep(APML_SLEEP);
	ret = read_sbtsi_revision(soc_num, &id);
	if (ret)
		return ret;
	printf("_TSI_REVISION [0x%x]\t\t| %#x\n", SBTSI_REVISION, id);

	printf("------------------------------------------------------------"
	       "----\n");
	return OOB_SUCCESS;
}

static oob_status_t get_apml_tsi_access(uint8_t soc_num)
{
	oob_status_t ret;

	printf("------------------------------------------------------------"
		"----\n");
	ret = get_apml_tsi_register_descriptions(soc_num);
	if (ret)
		printf("Failed: TSI Err[%d]: %s\n", ret, esmi_get_err_msg(ret));
	return ret;
}

static void apml_set_dimm_power(uint8_t soc_num, struct dimm_power d_soc_num)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = write_bmc_report_dimm_power(soc_num, d_soc_num);
	if (ret != OOB_SUCCESS) {
		printf("Failed to set dimm power, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}

	printf("Dimm power set successfully\n");
}

static int encode_dimm_temp(float temp, uint16_t *raw)
{
	if (temp >= 0 && temp <= 255.75)
		*raw = temp / SCALING_FACTOR;
	else if (temp < 0 && temp >= -256)
		*raw = 0x800 + (temp / SCALING_FACTOR);
	else
		return OOB_INVALID_INPUT;
	return 0;

}

static void apml_set_thermal_sensor(uint8_t soc_num,
				    struct dimm_thermal d_soc_num,
				    float temp)
{
	uint32_t buffer;
	uint16_t raw;
	oob_status_t ret;

	ret = encode_dimm_temp(temp, &raw);
	if (ret) {
		printf("Error: Temperature value out of range\n");
		return;
	}

	d_soc_num.sensor = raw;
	ret = write_bmc_report_dimm_thermal_sensor(soc_num, d_soc_num);
	if (ret != OOB_SUCCESS) {
		printf("Failed to set dimm  thermal sensor, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("Dimm thermal sensor set successfully\n");
}

static void apml_get_ras_pcie_config_data(uint8_t soc_num,
					  struct pci_address pci_addr)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_bmc_ras_pcie_config_access(soc_num, pci_addr, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get data from PCIe config space, Err[%d]:"
		       "%s\n", ret, esmi_get_err_msg(ret));
		return;
	}
	printf("-----------------------------------\n");
	printf("| Data PCIe | 0x%-17x |\n", buffer);
	printf("-----------------------------------\n");
}

static void apml_get_ras_valid_mca_banks(uint8_t soc_num)
{
	uint16_t bytespermca;
	uint16_t numbanks;
	oob_status_t ret;

	ret = read_bmc_ras_mca_validity_check(soc_num,
					      &bytespermca, &numbanks);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get MCA banks with valid status "
			"after a fatal error, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------------\n");
	printf("| Valid MCA banks |");
	printf(" %-17u |\n", numbanks);
	printf("| Bytes per bank  |");
	printf(" %-17u |\n", bytespermca);
	printf("---------------------------------------\n");
}

static void apml_get_ras_mca_msr(uint8_t soc_num, struct mca_bank mca_dump)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_bmc_ras_mca_msr_dump(soc_num, mca_dump, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get MCA bank data, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------------\n");
	printf("| Data MCA bank | 0x%-17x |\n", buffer);
	printf("---------------------------------------\n");
}

static void apml_get_fch_reset_reason(uint8_t soc_num, uint32_t fchid)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_bmc_ras_fch_reset_reason(soc_num, fchid, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get previous reset reason, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------------------------\n");
	printf("| FCH Previous reset reason | 0x%-17x |\n", buffer);
	printf("---------------------------------------------------\n");
}

static void apml_get_temp_range_and_refresh_rate(uint8_t soc_num,
						 uint8_t dimm_addr)
{
	struct temp_refresh_rate rate;
	oob_status_t ret;

	ret = read_dimm_temp_range_and_refresh_rate(soc_num, dimm_addr, &rate);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get dimm temp range and refresh rate, "
			"Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| Range\t\t\t |");
	printf(" %-17u |\n", rate.range);
	printf("| Refresh rate\t\t |");
	printf(" %-17u |\n", rate.ref_rate);
	printf("----------------------------------------------\n");
}

static void apml_get_dimm_power(uint8_t soc_num, uint8_t dimm_addr)
{
	struct dimm_power d_power;
	oob_status_t ret;

	ret = read_dimm_power_consumption(soc_num, dimm_addr, &d_power);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get dimm power, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| DIMM Power (mW)\t |");
	printf(" %-17u |\n", d_power.power);
	printf("| Update rate (ms)\t |");
	printf(" %-17u |\n", d_power.update_rate);
	printf("----------------------------------------------\n");
}

static void decode_dimm_temp(uint16_t raw, float *temp)
{
	if (raw <= 0x3FF)
		*temp = raw * SCALING_FACTOR;
	else
		*temp = (raw - 0x800) * SCALING_FACTOR;
}

static void apml_get_dimm_temp(uint8_t soc_num, uint8_t dimm_addr)
{
	struct dimm_thermal d_sensor;
	oob_status_t ret;
	float temp;

	ret = read_dimm_thermal_sensor(soc_num, dimm_addr, &d_sensor);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get dimm temp, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	decode_dimm_temp(d_sensor.sensor, &temp);
	printf("-----------------------------------------------\n");
	printf("| DIMM Temp (ºC)(raw)\t |");
	printf(" %-10.3f(0x%-4x) |\n", temp, d_sensor.sensor);
	printf("| Update rate (ms)\t |");
	printf(" %-17u  |\n", d_sensor.update_rate);
	printf("-----------------------------------------------\n");
}

static void display_freq_limit_src_names(char **source_type)
{
	uint8_t index = 0;

	while (source_type[index]) {
		printf(" %-17s ", source_type[index]);
		index++;
	}
	if (index == 0)
		printf(" %-17s ", "Reserved");
}

static void apml_get_freq_limit(uint8_t soc_num)
{
	uint16_t freq;
	char *source_type[ARRAY_SIZE(freqlimitsrcnames)] = {NULL};
	oob_status_t ret;

	ret = read_pwr_current_active_freq_limit_socket(soc_num,
							&freq, source_type);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get socket freq limit, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("------------------------------------------------------\n");
	printf("| Frequency (MHz)\t\t |");
	printf(" %-17u |\n", freq);
	printf("| Source\t\t\t |");
	display_freq_limit_src_names(source_type);
	printf("|\n");
	printf("------------------------------------------------------\n");
}

static void apml_get_cclklimit(uint8_t soc_num, uint32_t thread)
{
	uint16_t buffer;
	oob_status_t ret;

	ret = read_pwr_current_active_freq_limit_core(soc_num,
						      thread, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get core freq limit, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| Frequency [%03u] (MHz)\t | %-17u |\n", thread, buffer);
	printf("----------------------------------------------\n");
}

static void apml_get_pwr_telemetry(uint8_t soc_num)
{
	uint32_t power;
	oob_status_t ret;

	ret = read_pwr_svi_telemetry_all_rails(soc_num, &power);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get svi based telemetry "
		       "for all rails, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}
	printf("------------------------------------------------------------"
		"--\n");
	printf("| Telemetry Power (Watts)\t\t |");
	printf(" %-17.03f |\n", (float)power/1000);
	printf("------------------------------------------------------------"
		"--\n");
}

static void apml_get_sock_freq_range(uint8_t soc_num)
{
	uint16_t fmax;
	uint16_t fmin;
	oob_status_t ret;

	ret = read_socket_freq_range(soc_num, &fmax, &fmin);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get Fmax and Fmin, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| Fmax (MHz)\t\t |");
	printf(" %-17u |\n", fmax);
	printf("| Fmin (MHz)\t\t |");
	printf(" %-17u |\n", fmin);
	printf("----------------------------------------------\n");
}

static void convert_to_upper_case(char *str)
{
	uint8_t index;

	for (index = 0; str[index]; index++) {
		if (str[index] >= 'a' && str[index] <= 'z')
			str[index] -= 32;
	}
}

static oob_status_t validate_bw_link_id(char *link_id, char *bw_type,
					bool is_xgmi_bw,
					struct link_id_bw_type *link)
{
	const char *link_id_list[8] = {"P0", "P1", "P2", "P3",
				       "G0", "G1", "G2", "G3"};
	const char *bw_type_list[3] = {"AGG_BW", "RD_BW", "WR_BW"};
	const char *io_bw_type = "AGG_BW";
	uint8_t index, arr_size;
	oob_status_t ret;

	link->bw_type = 0;
	link->link_id = 0;
	convert_to_upper_case(link_id);
	convert_to_upper_case(bw_type);

	if (is_xgmi_bw)
		arr_size = ARRAY_SIZE(bw_type_list);
	else
		arr_size = 1;

	if (!is_xgmi_bw) {
		if (strcmp(bw_type, io_bw_type) == 0)
			link->bw_type = 1;
	} else {
		for (index = 0; index < arr_size; index++) {
			if (strcmp(bw_type, bw_type_list[index]) == 0) {
				link->bw_type = 1 << index;
				break;
			}
		}
	}

	arr_size = ARRAY_SIZE(link_id_list);
	for (index = 0; index < arr_size; index++) {
		if (strcmp(link_id, link_id_list[index]) == 0) {
			link->link_id = 1 << index;
			break;
		}
	}

	return OOB_SUCCESS;
}

static void apml_get_iobandwidth(uint8_t soc_num, char *link_id,
				 char *bw_type)
{
	struct link_id_bw_type link;
	uint32_t buffer;
	oob_status_t ret;

	ret = validate_bw_link_id(link_id, bw_type, false, &link);
	if (ret) {
		printf("Failed to get current IO bandwidth, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	ret = read_current_io_bandwidth(soc_num, link, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get current IO bandwidth, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| IO bandwidth (Mbps)\t | %-17u |\n", buffer);
	printf("----------------------------------------------\n");
}

static void apml_get_xgmibandwidth(uint8_t soc_num, char *link_id,
				   char *bw_type)
{
	struct link_id_bw_type link;
	uint32_t buffer;
	oob_status_t ret;

	ret = validate_bw_link_id(link_id, bw_type, true, &link);
	if (ret) {
		printf("Failed to get current bandwidth on xGMI link, "
		       "Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
		return;
	}

	ret = read_current_xgmi_bandwidth(soc_num, link, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get current  bandwidth on xGMI link, "
			"Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
		return;
	}
	printf("-------------------------------------------------------"
		"-------\n");
	printf("| xGMI Bandwidth (Mbps)\t\t\t | %-17u |\n",
		buffer);
	printf("--------------------------------------------------------"
		"------\n");
}

static void apml_set_gmi3link_width(uint8_t soc_num,
				    uint16_t minwidth,
				    uint16_t maxwidth)
{
	oob_status_t ret;

	ret = write_gmi3_link_width_range(soc_num, minwidth,
					  maxwidth);
	if (ret != OOB_SUCCESS) {
		printf("Failed to write GMI3 link width range, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("GMI3 link width set successfully\n");
}

static void apml_set_xgmilink_width(uint8_t soc_num,
				    uint16_t minwidth,
				    uint16_t maxwidth)
{
	oob_status_t ret;

	ret = write_xgmi_link_width_range(soc_num, minwidth,
					  maxwidth);
	if (ret != OOB_SUCCESS) {
		printf("Failed to write xGMI link width range, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("xGMI link width set successfully\n");
}

static void apml_set_dfpstate(uint8_t soc_num, uint8_t pstate)
{
	oob_status_t ret;
	bool prochot_asserted = false;

	ret = write_apb_disable(soc_num, pstate, &prochot_asserted);
	if (ret != OOB_SUCCESS) {
		printf("Failed to set data fabric pstate, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	if (prochot_asserted)
		printf("PROCHOT_L is asserted,"
		       " lowest DF-Pstate is enforced.\n");
	else
		printf("Data fabric pstate set successfully\n");
}

static void apml_get_fclkmclkuclk(uint8_t soc_num)
{
	struct pstate_freq df_pstate;
	oob_status_t ret;

	ret = read_current_dfpstate_frequency(soc_num, &df_pstate);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get data fabric clock, "
			"memory clock and UMC clock divider, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| FCLK (MHz)\t\t |");
	printf(" %-17u |\n", df_pstate.fclk);
	printf("| MEMCLK (MHz)\t\t |");
	printf(" %-17u |\n", df_pstate.mem_clk);
	printf("| UCLK Divider\t\t |");
	printf(" %-17u |\n", df_pstate.uclk);
	printf("----------------------------------------------\n");
}

static void apml_apb_enable(uint8_t soc_num)
{
	oob_status_t ret;
	bool prochot_asserted = false;

	ret = write_apb_enable(soc_num, &prochot_asserted);
	if (ret != OOB_SUCCESS) {
		printf("Failed to write apb enable, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	if (prochot_asserted)
		printf("PROCHOT_L is asserted,"
		       " lowest DF-Pstate is enforced.\n");
	else
		printf("Successfully set to dynamic data fabric pstate"
		       " control\n");
}

static void apml_set_lclk_dpm_level(uint8_t soc_num,
				    struct lclk_dpm_level_range lclk)
{
	oob_status_t ret;

	ret = write_lclk_dpm_level_range(soc_num, lclk);
	if (ret != OOB_SUCCESS) {
		printf("Failed to write dpm level, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("Lclk dpm level set successfully\n");
}

static void apml_get_cpu_base_freq(uint8_t soc_num)
{
	uint16_t buffer;
	oob_status_t ret;

	ret = read_bmc_cpu_base_frequency(soc_num, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get cpu base freq, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------------\n");
	printf("| Frequency (MHz) | %-17u |\n", buffer);
	printf("---------------------------------------\n");
}

static void apml_set_pciegen5_control(uint8_t soc_num, uint8_t val)
{
	uint8_t buffer;
	oob_status_t ret;

	ret = read_bmc_control_pcie_gen5_rate(soc_num, val, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to write PCIegen5 rate control, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| Previous Mode\t\t | %-17u |\n", buffer);
	printf("----------------------------------------------\n");

}

static void apml_set_pwr_efficiency_mode(uint8_t soc_num, uint8_t mode)
{
	oob_status_t ret;

	ret = write_pwr_efficiency_mode(soc_num, mode);
	if (ret != OOB_SUCCESS) {
		printf("Failed to set pwr efficiecy profile policy, "
			"Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
		return;
	}

	printf("Mode set successfully\n");
}

static void apml_get_core_energy(uint8_t soc_num, uint32_t thread)
{
	double buffer;
	oob_status_t ret;

	ret = read_rapl_core_energy_counters(soc_num, thread,
					     &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get core energy, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("----------------------------------------------\n");
	printf("| Core[%03d] Energy (KJ)\t | %-17lf |\n", thread, buffer);
	printf("----------------------------------------------\n");
}

static void apml_get_pkg_energy(uint8_t soc_num)
{
	double buffer;
	oob_status_t ret;

	ret = read_rapl_pckg_energy_counters(soc_num, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get package energy, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("-----------------------------------------------------\n");
	printf("| Package energy (MJ)\t\t | %-17lf |\n", buffer);
	printf("-----------------------------------------------------\n");
}

static void apml_set_df_pstate_range(uint8_t soc_num, uint8_t max_pstate,
				     uint8_t min_pstate)
{
	oob_status_t ret;

	ret = write_df_pstate_range(soc_num, max_pstate, min_pstate);
	if (ret != OOB_SUCCESS) {
		printf("Failed to set data fabric pstate range, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}
	printf("Data fabric pstate range set successfully\n");
}

static void read_register(uint8_t soc_num, uint32_t reg, char *file_name)
{
	uint8_t buffer;
	oob_status_t ret;

	ret = esmi_oob_read_byte(soc_num, reg, file_name, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to read register %x, Err[%d]:%s\n",
		       reg, ret, esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------");
	printf("\n| Register \t| Value \t|");
	printf("\n---------------------------------");
	printf("\n| 0x%x \t\t| 0x%x \t\t|", reg, buffer);
	printf("\n---------------------------------\n");
}

static void write_register(uint8_t soc_num, uint32_t reg, char *file_name,
			   uint32_t value)
{
	uint8_t buffer;
	oob_status_t ret;

	ret = esmi_oob_write_byte(soc_num, reg, file_name, value);
	if (ret != OOB_SUCCESS) {
		printf("Failed to writeregister %x, Err[%d]:%s\n",
		       reg, ret, esmi_get_err_msg(ret));
		return;
	}
	printf("Write to register 0x%x is successful\n", reg);
}

static void read_msr_register(uint8_t soc_num, uint32_t addr,
			      uint32_t thread)
{
	uint64_t buffer;
	oob_status_t ret;

	ret = esmi_oob_read_msr(soc_num, thread, addr, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to read MSR register, Err[%d]:%s\n",
			ret, esmi_get_err_msg(ret));
		return;
	}

	printf("-----------------------------------------\n");
	printf("| MSR register \t| Value\t\t\t|\n");
	printf("-----------------------------------------\n");
	printf("| [0x%08x]  | 0x%-17llx\t|\n", addr, buffer);
	printf("-----------------------------------------\n");
}

static void read_cpuid_register(uint8_t soc_num, uint32_t func,
				uint32_t ex_func, uint32_t thread)
{
	uint32_t eax, ebx, ecx, edx;
	oob_status_t ret;

	eax = func;
	ecx = ex_func;

	ret = esmi_oob_cpuid(soc_num, thread, &eax, &ebx,
			     &ecx, &edx);
	if (ret != OOB_SUCCESS) {
		printf("Failed to read CPUID register[0x%x][0x%x], "
			"Err[%d]:%s\n", func, ex_func, ret,
			esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------------------------------\n");
	printf("| CPUID register[0x%08x][0x%x]  | Value\t\t|\n", func, ex_func);
	printf("---------------------------------------------------------\n");
	printf("| \t\teax \t\t   | 0x%-17x|\n", eax);
	printf("| \t\tebx \t\t   | 0x%-17x|\n", ebx);
	printf("| \t\tecx \t\t   | 0x%-17x|\n", ecx);
	printf("| \t\tedx \t\t   | 0x%-17x|\n", edx);
	printf("---------------------------------------------------------\n");
}

static oob_status_t read_max_threads_per_l3(uint8_t soc_num,
					    uint32_t *threads_l3)
{
	uint32_t thread;
	oob_status_t ret;

	/* Get maximum threads per l3 */
	thread = 0;
	ret = esmi_oob_cpuid_eax(soc_num, thread, THREADS_L3_FUNC,
				 THREADS_L3_EXTD, threads_l3);
	if (ret)
		return ret;
	*threads_l3 = (*threads_l3 >> 14) & 0xFFF;
	return ret;
}

static oob_status_t read_ccx_info(uint8_t soc_num,
				  uint16_t *max_cores_per_ccx,
				  uint16_t *ccx_instances)
{
	uint32_t threads_c, threads_s, threads_l3;
	oob_status_t ret;

	/* Get threads per core */
	ret = esmi_get_threads_per_core(soc_num, &threads_c);
	if (ret)
		return ret;

	/* Get maximum threads per l3 */
	ret = read_max_threads_per_l3(soc_num, &threads_l3);
	if (ret)
		return ret;

	/* Get Maximum threads per socket */
	ret = esmi_get_threads_per_socket(soc_num, &threads_s);
	if (ret)
		return ret;

	/* Max number of cores per ccx */
	*max_cores_per_ccx = threads_l3 / threads_c + 1;
	/* Logical CCX instances */
	*ccx_instances = threads_s / threads_l3;

	return ret;
}

static void apml_get_iod_bist_status(uint8_t soc_num)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_iod_bist(soc_num, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get the iod bist status, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("-----------------------------------\n");
	printf("| IOD BIST STATUS | \t%s |\n",
	       buffer == 0 ? "BIST PASS" : "BIST FAIL");
	printf("-----------------------------------\n");
}

static void apml_get_ccd_bist_status(uint8_t soc_num, uint32_t instance)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_ccd_bist_result(soc_num, instance, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get the ccd bist status, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("-----------------------------------\n");
	printf("| CCD BIST STATUS | \t%s |\n",
		buffer == 0 ? "BIST PASS" : "BIST FAIL");
	printf("-----------------------------------\n");
}

static void apml_get_ccx_bist_status(uint8_t soc_num, uint32_t instance)
{
	uint32_t bist_res;
	uint16_t max_cores_per_ccx, ccx_instances;
	uint8_t index = 0, rev = 0;
	oob_status_t ret;

	ret = read_ccx_bist_result(soc_num, instance, &bist_res);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get the ccx bist status, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	ret = read_sbrmi_revision(soc_num, &rev);
	if (ret) {
		printf("Failed to get the ccx bist status, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------\n");
	if (rev == 0x10)
		printf("| CCX BIST RESULT | \t0x%-8x|\n", bist_res);
	else {
		ret = read_ccx_info(soc_num, &max_cores_per_ccx,
				    &ccx_instances);
		if (ret) {
			printf("Failed to get the CCX info, Err[%d]:%s\n",
			       ret, esmi_get_err_msg(ret));
			return;
		}
		printf("| L3 BIST \t| %s\t|\n",
		       bist_res & 1 ? "Bist pass": "Bist fail");
		for (index = 0; index < max_cores_per_ccx; index++)
			printf("| CORE[%d] \t| %s\t|\n", index,
			       ((bist_res >> (index + 16)) & 1)
			       ? "Bist pass": "Bist fail");
	}
	printf("---------------------------------\n");
}

static void apml_get_nbio_error_log_reg(uint8_t soc_num,
					struct nbio_err_log nbio)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_nbio_error_logging_register(soc_num, nbio, &buffer);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get the nbio error log register,"
			"Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
		return;
	}

	printf("-----------------------------------\n");
	printf("| NBIO ERROR LOG REG | \t%-10u |\n", buffer);
	printf("-----------------------------------\n");
}

static void apml_get_dram_throttle(uint8_t soc_num)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_dram_throttle(soc_num, &buffer);

	if (ret != OOB_SUCCESS) {
		printf("Failed to get the dram throttle, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("------------------------------------\n");
	printf("| DRAM THROTTLE (%%) | \t%-10u |\n", buffer);
	printf("------------------------------------\n");
}

static void apml_get_prochot_status(uint8_t soc_num)
{
	uint32_t buffer;
	oob_status_t ret;

	ret = read_prochot_status(soc_num, &buffer);

	if (ret != OOB_SUCCESS) {
		printf("Failed to get the prochot status, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("-------------------------------------------\n");
	printf("| PROCHOT STATUS | \t%-17s |\n", buffer ?
	       "PROCHOT" : "NOT_PROCHOT");
	printf("-------------------------------------------\n");
}

static void apml_get_prochot_residency(uint8_t soc_num)
{
	float buffer;
	oob_status_t ret;

	ret = read_prochot_residency(soc_num, &buffer);

	if (ret != OOB_SUCCESS) {
		printf("Failed to get the prochot residency, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("--------------------------------------------\n");
	printf("| PROCHOT RESIDENCY (%%) | \t%-10.2f |\n", buffer);
	printf("--------------------------------------------\n");
}

static void apml_get_lclk_dpm_level_range(uint8_t soc_num,
					  uint8_t nbio_id)
{
	struct dpm_level dpm;
	oob_status_t ret;

	ret = read_lclk_dpm_level_range(soc_num, nbio_id, &dpm);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get the lclk dpm level range, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}
	printf("--------------------------------------------\n");
	printf("| MIN DPM \t\t| \t%-10u |\n", dpm.min_dpm_level);
	printf("| MAX DPM \t\t| \t%-10u |\n", dpm.max_dpm_level);
	printf("--------------------------------------------\n");
}

static void apml_get_ras_last_transact_addr(uint8_t soc_num)
{
	uint64_t last_transact_addr = 0;
	oob_status_t ret;

	ret = read_ras_last_transaction_address(soc_num, &last_transact_addr);
	if (ret) {
		printf("Failed to get the RAS last transact addr, Err[%d]:%s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}
	printf("------------------------------------------------------------\n");
	printf("| RAS LAST TRANSACT ADDR\t\t\t| x%-15llx |\n", last_transact_addr);
	printf("------------------------------------------------------------\n");
}

static void apml_get_power_consumed(uint8_t soc_num)
{
	uint32_t pow;
	oob_status_t ret;

	ret = read_socket_power(soc_num, &pow);
	if (ret != OOB_SUCCESS) {
		printf("Failed to get power, Err[%d]: %s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("---------------------------------------------\n");
	printf("| Power (Watts)\t\t |");
	printf(" %-17.3f|\n", (double)pow/1000);
	printf("---------------------------------------------\n");
}

static void apml_get_smt_status(uint8_t soc_num)
{
	uint32_t threads_per_core;
	oob_status_t ret;

	ret = esmi_get_threads_per_core(soc_num, & threads_per_core);
	if (ret) {
		printf(" Failed to SMT status  Err[%d]: %s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}
	printf("---------------------------------------------\n");
	printf("| SMT STATUS \t\t | %15s  |\n",
	       threads_per_core > 1 ? "ENABLED" : "DISBALED");
	printf("---------------------------------------------\n");
}

static void apml_get_threads_per_core_and_soc(uint8_t soc_num)
{
	uint32_t threads_per_core, threads_per_soc;
	oob_status_t ret;

	ret = esmi_get_threads_per_core(soc_num, &threads_per_core);
	if (ret) {
		printf("\n Failed to get threads per core Err[%d]: %s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	ret = esmi_get_threads_per_socket(soc_num, &threads_per_soc);
	if (ret) {
		printf("\n Failed to get threads per socket Err[%d]: %s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("-----------------------------------------------\n");
	printf("| THREADS PER CORE \t | %17d  |\n", threads_per_core);
	printf("| THREADS PER SOCKET \t | %17d  |\n", threads_per_soc);
	printf("-----------------------------------------------\n");
}

static void apml_get_ccx_info(uint8_t soc_num)
{
	uint16_t max_cores_per_ccx, ccx_instances;
	oob_status_t ret;

	ret = read_ccx_info(soc_num, &max_cores_per_ccx, &ccx_instances);
	if (ret) {
		printf("\n Failed to get the ccx information Err[%d]: %s\n",
		       ret, esmi_get_err_msg(ret));
		return;
	}

	printf("----------------------------------------------\n");
	printf("| No of cores per CCX \t | %17d |\n", max_cores_per_ccx);
	printf("| No of CCX instances \t | %17d |\n", ccx_instances);
	printf("----------------------------------------------\n");
}

static void show_usage(char *exe_name)
{
	printf("Usage: %s [soc_num] [Option<s> / [--help] "
		"[module-name]\n", exe_name);
	printf("Where:  soc_num : socket number 0 or 1\n");
	printf("Description:\n");
	printf("%s -v \t\t\t- Displays tool version\n", exe_name);
	printf("%s --help <MODULE>\t- Displays help on the options for "
		"the specified module\n", exe_name);
	printf("%s <option/s>\t\t- Runs the specified option/s."
	       "\nUsage: %s [soc_num]"
	       " [Option] params\n\n", exe_name, exe_name);
	printf("\tMODULES:\n");
	printf("\t1. mailbox\n");
	printf("\t2. sbrmi\n");
	printf("\t3. sbtsi\n");
	printf("\t4. reg-access\n");
	printf("\t5. cpuid\n\n");
}

static void show_module_commands(char *exe_name, char *command)
{
	if (!strcmp(command, "mailbox") || !strcmp(command, "1"))
		printf("Usage: %s  [SOC_NUM] [Option]"
			"\nOption:\n"
			"\n< MAILBOX COMMANDS [params] >:\n"
			"  --showmailboxsummary\t\t\t\t\t\t\t\t "
			"Get summary of the mailbox commands\n"
			"  -p, (--showpower)\t\t\t\t\t\t\t\t "
			"Get Power for a given socket in Watts\n"
			"  -t, (--showtdp)\t\t\t\t\t\t\t\t "
			"Get TDP for a given socket in Watts\n"
			"  -s, (--setpowerlimit)\t\t\t  [POWER]\t\t\t\t "
			"Set powerlimit for a given socket in mWatts\n"
			"  -b, (--showboostlimit)\t\t  [THREAD]\t\t\t\t "
			"Get APML and BIOS boostlimit for a given core index "
			"in MHz\n"
			"  -d, (--setapmlboostlimit)\t\t  [THREAD]"
			"[BOOSTLIMIT]\t\t\t Set APML boostlimit for a given "
			"core in MHz\n"
			"  -a, (--setapmlsocketboostlimit)\t  [BOOSTLIMIT]"
			"\t\t\t\t Set APML boostlimit for all cores in a "
			"socket in MHz\n"
			"  --showddrbandwidth\t\t\t\t\t\t\t\t Show "
			"DDR Bandwidth of a system\n"
			"  --set_and_verify_dramthrottle\t\t  [0 to 80%%]"
			"\t\t\t\t Set DRAM THROTTLE for a given socket\n"
			"  --setdimmpower\t\t\t  [DIMM_ADDR][POWER(mW)]"
			"[UPDATERATE(ms)] Set dimm power reported"
			" by bmc\n"
			"  --setdimmthermalsensor\t\t  [DIMM_ADDR][TEMP(°C)]"
			"[UPDATERATE(ms)]  Set dimm temperature "
			"reported by bmc\n"
			"  --showdimmpower\t\t\t  [DIMM_ADDR]\t\t\t\t "
			"Show per dimm power consumption\n"
			"  --showdimmthermalsensor\t\t  [DIMM_ADDR]\t\t\t"
			"\t Show per dimm thermal sensor\n"
			"  --showdimmtemprangeandrefreshrate\t  [DIMM_ADDR]"
			"\t\t\t\t Show per dimm temp range and refresh rate\n"
			"  --showPCIeconfigspacedata\t\t  [SEGMENT][OFFSET]\n"
			"\t\t\t\t\t  [BUS(HEX)][DEVICE(HEX)][FUNC]\t\t Show "
			"32 bit data from extended PCI config space\n"
			"  --showvalidmcabanks\t\t\t\t\t\t\t\t "
			"Show number of MCA banks & bytes/bank with valid "
			"status after a fatal error\n"
			"  --showrasmcamsr\t\t\t  [MCA_BANK_INDEX][OFFSET]"
			"\t\t Show 32 bit data from specified MCA bank and "
			"offset\n"
			"  --showfchresetreason\t\t\t  [FCHID(0 or 1)]\t\t"
			"\t Show previous reset reason from FCH register\n"
			"  --showsktfreqlimit\t\t\t\t\t\t\t\t "
			"Show per socket current active freq limit\n"
			"  --showcclklimit\t\t\t  [THREAD]\t\t\t\t "
			"Show core clock limit\n"
			"  --showsvitelemetryallrails\t\t\t\t\t\t\t "
			"Show svi based pwr telemetry for all rails\n"
			"  --showsktfreqrange\t\t\t\t\t\t\t\t "
			"Show per socket fmax fmin\n"
			"  --showiobandwidth\t\t\t  "
			"[LINKID(P0-P3,G0-G3)][BW(AGG_BW)]"
			"\t Show IO bandwidth\n"
			"  --showxGMIbandwidth\t\t\t  [LINKID(P0-P3,G0-G3)]"
			"[BW(AGG_BW,RD_BW,WR_BW)]\t Show current xGMI bandwidth\n"
			"  --setGMI3linkwidthrange\t\t  [MIN(0,1,2)]"
			"[MAX(0,1,2)]\t\t Set GMI3link width, max value >= "
			"min value\n"
			"  --setxGMIlinkwidthrange\t\t  [MIN(0,1,2)]"
			"[MAX(0,1,2)]\t\t Set xGMIlink width, max value >= "
			"min value\n"
			"  --APBDisable\t\t\t\t  [PSTATE(0,1,2)]\t\t\t"
			" APB Disable specifies DFP-State, 0 is highest & 2 is"
			" the lowest DF P-state\n"
			"  --enabledfpstatedynamic\t\t  \t\t\t\t\t "
			"Set df pstate dynamic\n"
			"  --showfclkmclkuclk\t\t\t  \t\t\t\t\t "
			"Show df clock, memory clock and umc clock divider\n"
			"  --setlclkdpmlevel\t\t\t  [NBIOID(0-3)][MAXDPM]"
			"[MINDPM]\t\t Set dpm level range, valid dpm "
			"values from 0 - 3, max value >= min value\n"
			"  --showcpubasefreq\t\t\t  \t\t\t\t\t "
			"Show cpu base frequency\n"
			"  --setPCIegenratectrl\t\t\t  [MODE(0,1,2)]\t\t\t\t "
			"Set PCIe link rate control\n"
			"  --setpwrefficiencymode\t\t  [MODE(0,1,2)]\t\t\t\t "
			"Set power efficiency profile policy\n"
			"  --showraplcore\t\t\t  [THREAD]\t\t\t\t "
			"Show runnng average power on specified core\n"
			"  --showraplpkg\t\t\t\t  \t\t\t\t\t "
			"Show running average power on pkg\n"
			"  --setdfpstaterange\t\t\t  [MAX_PSTATE]"
			"[MIN_PSTATE]\t\t Set data fabric pstate range, valid "
			"value 0 - 2. max pstate <= min pstate\n"
			"  --showiodbist\t\t\t\t  \t\t\t\t\t "
			"Show IOD bist status\n"
			"  --showccdbist\t\t\t\t  [CCDINSTANCE]\t\t\t\t "
			"Show CCD bist status\n"
			"  --showccxbist\t\t\t\t  [CCXINSTANCE]\t\t\t\t "
			"Show CCX bist status\n"
			"  --shownbioerrorloggingregister\t  "
			"[QUADRANT(HEX)][OFFSET(HEX)]\t\t Show nbio error "
			"logging register\n"
			"  --showdramthrottle\t\t\t  \t\t\t\t\t "
			"Show dram throttle\n"
			"  --showprochotstatus\t\t\t  \t\t\t\t\t "
			"Show prochot status\n"
			"  --showprochotresidency\t\t  \t\t\t\t\t "
			"Show prochot residency\n"
			"  --showlclkdpmlevelrange\t\t  [NBIOID(0~3)]\t\t\t\t "
			"Show LCLK DPM level range\n"
			"  --showraslasttransactaddr\t\t\t  \t\t\t\t "
			"Show RAS last transaction address\n"
			"  --showpowerconsumed\t\t\t  \t\t\t\t\t "
			"Show consumed power\n", exe_name);
	else if (!strcmp(command, "sbrmi") || !strcmp(command, "2"))
		printf("Usage: %s [SOC_NUM] [Option]"
			"\nOption:\n"
			"\n< SB-RMI COMMANDS >:\n"
			"  --showrmiregisters\t\t\t Get "
			"values of SB-RMI reg commands for a given socket\n"
			,exe_name);
	else if (!strcmp(command, "sbtsi") || !strcmp(command, "3"))
		printf("Usage: %s [SOC_NUM] [Option]"
			"\nOption:\n"
			"\n< SB-TSI COMMANDS [params] >:\n"
			"  --showtsiregisters\t\t\t  \t\t\t\t\t Get "
			"values of SB-TSI reg commands for a given socket\n"
			"  --set_verify_updaterate\t	  [UPDATERATE]"
			"\t\t\t\t Set APML Freq Update rate."
			"Valid values are 2^i, i=[-4,6]\n"
			"  --sethightempthreshold\t	  [TEMP(°C)]\t\t"
			"\t\t Set APML High Temp Threshold\n"
			"  --setlowtempthreshold\t\t	  [TEMP(°C)]\t\t"
			"\t\t Set APML Low Temp Threshold\n"
			"  --settempoffset\t\t	  [VALUE]\t\t\t\t Set "
			"APML CPU Temp Offset, VALUE = [-CPU_TEMP(°C), 127 "
			"°C]\n"
			"  --settimeoutconfig\t\t	  [VALUE]\t\t"
			"\t\t Set/Reset APML CPU timeout config, VALUE = 0 or "
			"1\n"
			"  --setalertthreshold\t\t\t  [VALUE]\t\t\t\t "
			"Set APML CPU alert threshold sample, VALUE = 1 to 8\n"
			"  --setalertconfig\t\t	  [VALUE]\t\t\t\t "
			"Set/Reset APML CPU alert config, VALUE = 0 or 1\n"
			"  --setalertmask\t\t	  [VALUE]\t\t\t\t "
			"Set/Reset APML CPU alert mask, VALUE = 0 or 1\n"
			"  --setrunstop\t\t\t	  [VALUE]\t\t\t\t "
			"Set/Reset APML CPU runstop, VALUE = 0 or 1\n"
			"  --setreadorder\t\t	  [VALUE]\t\t\t\t "
			"Set/Reset APML CPU read order, VALUE = 0 or 1\n"
			"  --setara\t\t\t	  [VALUE]\t\t\t\t "
			"Set/Reset APML CPU ARA, VALUE = 0 or 1\n",
			exe_name);
	else if (!strcmp(command, "reg-access") || !strcmp(command, "4"))
		printf("Usage: %s [SOC_NUM] [Option]"
			"\nOption:\n"
			"\n< REG-ACCESS [params] >:\n"
			"  --readregister\t\t\t  [sbrmi/sbtsi][REGISTER(hex)]\t\t\t "
			"Read a register\n"
			"  --writeregister\t\t\t  [sbrmi/sbtsi][REGISTER(hex)]"
			"[VALUE(int)]\t Write to a register\n"
			"  --readmsrregister\t\t\t  [REGISTER(hex)]"
			"[thread]\t\t\t Read MSR register\n"
			"  --readcpuidregister\t\t\t  [FUN(hex)]"
			"[EXT_FUN(hex)][thread]\t\t Read CPUID register\n",
			exe_name);
	else if (!strcmp(command, "cpuid") || !strcmp(command, "5"))
		printf("Usage: %s [SOC_NUM] [Option]"
		       "\nOption:\n"
		       "\n< CPUID [params] >:\n"
		       "  --showthreadspercoreandsocket\t  \t\t\t\t "
		       "Show threads per core and socket\n"
			"  --showccxinfo\t\t\t\t\t "
			"\t\t Show max num of cores per ccx and "
			"ccx instances\n"
		       "  --showSMTstatus\t\t\t  \t\t\t "
		       "Show SMT enabled status\n", exe_name);
	else
		printf("Failed: Invalid command, Err[%d]: %s\n",
			OOB_INVALID_INPUT,
			esmi_get_err_msg(OOB_INVALID_INPUT));
}

static oob_status_t show_apml_mailbox_cmds(uint8_t soc_num)
{
	struct max_ddr_bw max_ddr;
	struct nbio_err_log nbio;
	struct pstate_freq df_pstate;
	double energy;
	float uprat, prochot_res;
	uint32_t core_id, instance, nbio_reg, buffer;
	uint32_t power_avg, power_cap, power_max;
	uint32_t tdp_avg, tdp_min, tdp_max;
	uint32_t cclk, residency, threads_per_core;
	uint32_t max_bw, utilized_bw, utilized_pct;
	uint32_t bios_boost, esb_boost, threads_per_soc;
	uint32_t dram_thr, prochot, power;
	uint16_t ccx_instances, max_cores_per_ccx;
	uint32_t nbio_data, iod, ccd, ccx_res;
	uint16_t bytespermca;
	uint16_t numbanks;
	uint16_t freq;
	uint16_t urate;
	uint16_t fmax;
	uint16_t fmin;
	uint16_t fclk;
	uint16_t mclk;
	uint8_t uclk, index, rev;
	char *source_type[ARRAY_SIZE(freqlimitsrcnames)] = {NULL};
	oob_status_t ret;

	nbio.quadrant = 0x03;
	nbio.offset = 0x20;

	printf("\n\t\t *** SB-RMI MAILBOX SUMMARY ***\n");
	printf("------------------------------------------------------------"
	       "----\n");
	printf("| Function [INPUT VALUE] (UNITS)\t | VALUE");
	printf("\n------------------------------------------------------------"
	       "----\n");

	usleep(APML_SLEEP);
	printf("| Power (Watts)\t\t\t\t |");
	ret = read_socket_power(soc_num, &power_avg);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.3f", (double)power_avg/1000);

	usleep(APML_SLEEP);
	printf("\n| PowerLimit (Watts)\t\t\t |");
	ret = read_socket_power_limit(soc_num, &power_cap);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.3f", (double)power_cap/1000);

	usleep(APML_SLEEP);
	printf("\n| PowerLimitMax (Watts)\t\t\t |");
	ret = read_max_socket_power_limit(soc_num, &power_max);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.3f", (double)power_max/1000);

	usleep(APML_SLEEP);
	printf("\n| TDP Avg (Watts)\t\t\t |");
	ret = read_tdp(soc_num, &tdp_avg);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.3f", (double)tdp_avg/1000);

	usleep(APML_SLEEP);
	printf("\n| TDP Min (Watts)\t\t\t |");
	ret = read_min_tdp(soc_num, &tdp_min);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.3f", (double)tdp_min/1000);

	usleep(APML_SLEEP);
	printf("\n| TDP Max (Watts)\t\t\t |");
	ret = read_max_tdp(soc_num, &tdp_max);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.3f", (double)tdp_max/1000);

	usleep(APML_SLEEP);
	printf("\n| DDR BANDWIDTH \t\t\t |");
	ret = read_ddr_bandwidth(soc_num, &max_ddr);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else {
		printf("\n| \tDDR Max BW (GB/s)\t\t |");
		printf(" %-17d", max_ddr.max_bw);
		printf("\n| \tDDR Utilized BW (GB/s)\t\t |");
		printf(" %-17d", max_ddr.utilized_bw);
		printf("\n| \tDDR Utilized Percent(%%)\t\t |");
		printf(" %-17d", max_ddr.utilized_pct);
	}

	usleep(APML_SLEEP);
	core_id = 0x0;
	printf("\n| BIOS Boostlimit [0x%x] (MHz)\t\t |", core_id);
	ret = read_bios_boost_fmax(soc_num, core_id, &bios_boost);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17u", bios_boost);

	usleep(APML_SLEEP);
	printf("\n| APML Boostlimit [0x%x] (MHz)\t\t |", core_id);
	ret = read_esb_boost_limit(soc_num, core_id, &esb_boost);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17u", esb_boost);

	usleep(APML_SLEEP);
	printf("\n| DRAM_Throttle  (%%)\t\t\t |");
	ret = read_dram_throttle(soc_num, &dram_thr);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17u", dram_thr);

	usleep(APML_SLEEP);
	printf("\n| PROCHOT Status\t\t\t |");
	ret = read_prochot_status(soc_num, &prochot);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17s", prochot ? "PROCHOT" : "NOT_PROCHOT");

	usleep(APML_SLEEP);
	printf("\n| PROCHOT Residency (%%)\t\t\t |");
	ret = read_prochot_residency(soc_num, &prochot_res);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.2f", prochot_res);

	usleep(APML_SLEEP);
	nbio_reg = (((uint32_t)(nbio.quadrant) << 24) | nbio.offset);
	printf("\n| NBIO_Err_Log_Reg [0x%x]\t\t |", nbio_reg);
	ret = read_nbio_error_logging_register(soc_num, nbio, &nbio_data);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17u", nbio_data);

	usleep(APML_SLEEP);
	printf("\n| IOD_Bist_Result\t\t\t |");
	ret = read_iod_bist(soc_num, &iod);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17s", iod ? "Bist fail" : "Bist pass");

	usleep(APML_SLEEP);
	instance = 0x0;
	printf("\n| CCD_Bist_Result [0x%x]\t\t\t |", instance);
	ret = read_ccd_bist_result(soc_num, instance, &ccd);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17s", ccd ? "Bist fail" : "Bist pass");

	usleep(APML_SLEEP);
	printf("\n| CCX_Bist_Result [0x%x]\t\t\t |", instance);
	ret = read_ccx_bist_result(soc_num, instance, &ccx_res);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" 0x%-15x", ccx_res);
	usleep(APML_SLEEP);
	printf("\n| Curr_Active_Freq_Limit\t\t |");
	ret = read_pwr_current_active_freq_limit_socket(soc_num,
							&freq, source_type);

	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else {
		printf("\n| \tFreqlimit (MHz)\t\t\t | %u", freq);
		printf("\n| \tSource \t\t\t\t |");
		display_freq_limit_src_names(source_type);
	}
	usleep(APML_SLEEP);
	printf("\n| Power_Telemetry (Watts)\t\t |");
	ret = read_pwr_svi_telemetry_all_rails(soc_num, &power);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17.3f", (float)power / 1000);

	usleep(APML_SLEEP);
	printf("\n| Socket_Freq_Range (MHz)\t\t |");
	ret = read_socket_freq_range(soc_num, &fmax, &fmin);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else {
		printf("\n| \tFmax \t\t\t\t | %u", fmax);
		printf("\n| \tFmin \t\t\t\t | %u", fmin);
	}
	usleep(APML_SLEEP);
	printf("\n| Data_Fabric_Freq\t\t\t |");
	ret = read_current_dfpstate_frequency(soc_num, &df_pstate);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else {
		printf("\n| \tFclk \t\t\t\t | %u", df_pstate.fclk);
		printf("\n| \tMclk \t\t\t\t | %u", df_pstate.mem_clk);
		printf("\n| \tUclk \t\t\t\t | %u", df_pstate.uclk);
	}
	usleep(APML_SLEEP);
	printf("\n| CPU_Base_Freq (MHz)\t\t\t |");
	ret = read_bmc_cpu_base_frequency(soc_num, &freq);
	if (ret)
		printf(" Err[%d]:%s", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17u", freq);

	usleep(APML_SLEEP);
	printf("\n| Package_Energy (MJ)\t\t\t |");
	ret = read_rapl_pckg_energy_counters(soc_num, &energy);
	if (ret)
		printf(" Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17f", energy);

	usleep(APML_SLEEP);
	printf("\n| THREADS_PER_CORE\t\t\t |");
	ret = esmi_get_threads_per_core(soc_num, &threads_per_core);
	if (ret)
		printf(" Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17d", threads_per_core);

	usleep(APML_SLEEP);
	printf("\n| THREADS_PER_SOCKET\t\t\t |");
	ret = esmi_get_threads_per_socket(soc_num, &threads_per_soc);
	if (ret)
		printf(" Err[%d]:%s\n", ret, esmi_get_err_msg(ret));
	else
		printf(" %-17d\n", threads_per_soc);

	printf("------------------------------------------------------------"
	       "----\n");
	return OOB_SUCCESS;
}

static void show_smi_parameters(uint8_t soc_num)
{
	oob_status_t ret;

	ret = show_apml_mailbox_cmds(soc_num);
	if (ret)
		printf("Failed: For RMI Err[%d]: %s\n", ret,
		       esmi_get_err_msg(ret));

	ret = get_apml_tsi_register_descriptions(soc_num);
	if (ret)
		printf("Failed: For TSI Err[%d]: %s\n", ret,
		       esmi_get_err_msg(ret));
}

static void show_smi_message(void)
{
	printf("\n================================= APML System Management "
	       "Interface ====================================\n\n");
}

static void show_smi_end_message(void)
{
	printf("\n========================================== End of APML SMI "
	       "============================================\n");
}

static void print_apml_usage(char *arg)
{
	printf("Usage: %s <soc_num>\n"
		"Where:  soc_num : socket number 0 or 1\n", arg);
}

/*
 * returns 0 if the given string is a number for the given base, else 1.
 * Base will be 16 for hexdecimal value and 10 for decimal value.
 */
static oob_status_t validate_number(char *str, uint8_t base)
{
	uint64_t buffer_number = 0;
	char *endptr;

	buffer_number = strtol(str, &endptr, base);
	if (*endptr != '\0')
		return OOB_INVALID_INPUT;

	return OOB_SUCCESS;
}

/*
 * Parse command line parameters and set data for program.
 * @param argc number of command line parameters
 * @param argv list of command line parameters
 */
static oob_status_t parseesb_args(int argc, char **argv)
{
	struct nbio_err_log nbio;
	struct lclk_dpm_level_range lclk;
	struct pci_address pci_addr;
	struct dimm_power dp_soc_num;
	struct dimm_thermal dt_soc_num;
	uint8_t soc_num;
	uint8_t dimm_addr;
	struct mca_bank mca_dump;
	struct link_id_bw_type link;
	float uprate;
	float temp;
	int value;
	int power = 0;
	int opt = 0; /* option character */
	int i;
	uint32_t val1;
	uint32_t val2;
	uint32_t val3;
	uint32_t dram_thr;
	uint32_t boostlimit = 0, thread_ind = 0;
	char *val;
	char *end;
	char *link_name;
	char *bw_type;
	oob_status_t ret;

	//Specifying the expected options
	static struct option long_options[] = {
		{"help",		no_argument,	0,	'h'},
		{"version",		no_argument,	0,	'v'},
		{"showmailboxsummary",  no_argument,    0,      'Y'},
		{"showpower",		no_argument,	0,	'p'},
		{"showtdp",		no_argument,	0,	't'},
		{"setpowerlimit",	required_argument,	0,	's'},
		{"showddrbandwidth",	no_argument,	&flag,	 3 },
		{"showboostlimit",	required_argument,	0,	'b'},
		{"setapmlboostlimit",	required_argument,	0,	'd'},
		{"setapmlsocketboostlimit", required_argument,	0,	'a'},
		{"set_and_verify_dramthrottle", required_argument, 0,   'l'},
		{"showrmiregisters", no_argument,	&flag,   1 },
		{"showtsiregisters", no_argument,	&flag,   2 },
		{"set_verify_updaterate",   required_argument,	0,	'u'},
		{"sethightempthreshold", required_argument,	0,	'X'},
		{"setlowtempthreshold",	required_argument,	0,	'w'},
		{"settempoffset",	required_argument,	0,	'x'},
		{"settimeoutconfig",	required_argument,	0,	'y'},
		{"setalertthreshold",	required_argument,	0,	'g'},
		{"setalertconfig",	required_argument,	0,	'j'},
		{"setalertmask",	required_argument,	0,	'k'},
		{"setrunstop",		required_argument,	0,	'm'},
		{"setreadorder",	required_argument,	0,	'n'},
		{"setara",		required_argument,	0,	'o'},
		{"setdimmpower",			required_argument,	0,	'P'},
		{"setdimmthermalsensor",		required_argument,	0,	'T'},
		{"showdimmpower",			required_argument,	0,	'O'},
		{"showdimmthermalsensor",		required_argument,	0,	'E'},
		{"showdimmtemprangeandrefreshrate",	required_argument,	0,	'S'},
		{"showPCIeconfigspacedata",		required_argument,	0,	'R'},
		{"showvalidmcabanks",			no_argument,	&flag,	 5 },
		{"showrasmcamsr",			required_argument,	0,	'D'},
		{"showfchresetreason",			required_argument,	0,	'F'},
		{"showsktfreqlimit",			no_argument,	&flag,	 6 },
		{"showcclklimit",			required_argument,	0,	'C'},
		{"showsvitelemetryallrails",		no_argument,	&flag,	 7 },
		{"showsktfreqrange",			no_argument,	&flag,	 8 },
		{"showiobandwidth",			required_argument,	0,	'B'},
		{"showxGMIbandwidth",			required_argument,	0,	'G'},
		{"setGMI3linkwidthrange",		required_argument,	0,	'H'},
		{"setxGMIlinkwidthrange",		required_argument,	0,	'L'},
		{"APBDisable",				required_argument,	0,	'M'},
		{"enabledfpstatedynamic",		no_argument,	&flag,	 9 },
		{"showfclkmclkuclk",			no_argument,	&flag,	 10},
		{"setlclkdpmlevel",			required_argument,	0,	'N'},
		{"showcpubasefreq",			no_argument,	&flag,	 11},
		{"showraplcore",			required_argument,	0,	'J'},
		{"showraplpkg",				no_argument,	&flag,	 12},
		{"setPCIegenratectrl",			required_argument,	0,	'Z'},
		{"setpwrefficiencymode",		required_argument,	0,	'U'},
		{"setdfpstaterange",			required_argument,	0,	'V'},
		{"readregister",			required_argument,	0,	 'e'},
		{"writeregister",			required_argument,	&flag,	 14},
		{"readmsrregister",			required_argument,	&flag,	 15},
		{"readcpuidregister",			required_argument,	&flag,	 16},
		{"showiodbist",				no_argument,	&flag,	 17},
		{"showccdbist",				required_argument,	&flag,	 18},
		{"showccxbist",                         required_argument,      &flag,   19},
		{"shownbioerrorloggingregister",	required_argument,      &flag,   20},
		{"showdramthrottle",			no_argument,	&flag,	21},
		{"showprochotstatus",			no_argument,	&flag,	22},
		{"showprochotresidency",		no_argument,	&flag,	23},
		{"showlclkdpmlevelrange",		required_argument,	&flag,	25},
		{"showraslasttransactaddr",	no_argument,		&flag,  29},
		{"showpowerconsumed",		no_argument,		&flag,	30},
		{"showSMTstatus",		no_argument,		&flag,	31},
		{"showthreadspercoreandsocket",	no_argument,		&flag,	32},
		{"showccxinfo",			no_argument,		&flag,	33},
		{0,			0,			0,	0},
	};


	int long_index = 0;
	char *helperstring = "+:vhfYpts:b:d:a:u:X:w:x:y:g:j:k:m:n:o:";

	if (argc <= 1) {
		print_apml_usage(argv[0]);
		show_usage(argv[0]);
		return 0;
	}

	while ((opt = getopt_long(argc, argv,  helperstring, long_options, &long_index)) != EOF) {
		switch (opt) {
		case 'h':
			show_usage(argv[0]);
			if (argc == 2)
				return OOB_SUCCESS;
			continue;
		case 'v':
			printf("APML lib version : %d.%d.%d\n",
				apml64_VERSION_MAJOR, apml64_VERSION_MINOR,
				apml64_VERSION_PATCH);
			return OOB_SUCCESS;
		default:
			continue;
		}
	}

	if (argc > 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		show_module_commands(argv[0], argv[2]);
		return OOB_SUCCESS;
	}

	if (validate_number(argv[1], 10)) {
		print_apml_usage(argv[0]);
		return OOB_INVALID_INPUT;
	}

	soc_num = atoi(argv[1]);
	if (argc == 2) {
		show_smi_parameters(soc_num);
		printf(RED "Try `%s --help' for more information." RESET"\n",
		       argv[0]);
		return 0;
	}

	optind = 2;

	while ((opt = getopt_long(argc, argv, helperstring,
				  long_options, &long_index)) != -1) {
	if (opt == 's' ||
	    opt == 'b' ||
	    opt == 'a' ||
	    opt == 'l' ||
	    opt == 'd' ||
	    opt == 'y' ||
	    opt == 'g' ||
	    opt == 'k' ||
	    opt == 'm' ||
	    opt == 'n' ||
	    opt == 'o' ||
	    opt == 'j' ||
	    opt == 'u' ||
	    opt == 'X' ||
	    opt == 'x' ||
	    opt == 'R' ||
	    opt == 'D' ||
	    opt == 'F' ||
	    opt == 'S' ||
	    opt == 'E' ||
	    opt == 'O' ||
	    opt == 'C' ||
	    opt == 'H' ||
	    opt == 'L' ||
	    opt == 'M' ||
	    opt == 'Z' ||
	    opt == 'U' ||
	    opt == 'J' ||
	    opt == 'W' ||
	    opt == 'w' ||
	    opt == 0 && ((*long_options[long_index].flag) == 18 ||
	    *(long_options[long_index].flag) == 19)) {
		// make sure optind is valid  ... or another option
		if ((optind - 1) >= argc) {
			printf("\nOption '-%c' require an argument"
				"\n\n", optopt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}

		if ((opt == 'u' || opt == 'X' || opt == 'w' ||
		    opt == 'x')) {
			temp = strtof(argv[optind - 1], &end);
			if (*end != '\0') {
				printf("\nOption '-%c' require argument as valid"
				       " decimal value\n\n", opt);
				show_usage(argv[0]);
				return OOB_SUCCESS;
			}
		}
		else {
			if (opt != 'O' && opt != 'E' && opt != 'S'
			    && opt != 'T' && opt !='P'
			    && validate_number(argv[optind - 1], 10)) {
				printf("\nOption '-%c' require argument as valid"
				       " numeric value\n\n", opt);
				show_usage(argv[0]);
				return OOB_SUCCESS;
			}
		}
	}

	if (opt == 'd' ||
	    opt == 'D' ||
	    opt == 'B' ||
	    opt == 'H' ||
	    opt == 'G' ||
	    opt == 'L' ||
	    opt == 'V' ||
	    opt == 'e' ||
	    (opt == 0 && *(long_options[long_index].flag) == 15)) {
		if (optind >= argc || *argv[optind] == '-') {
			printf("\nOption '-%c' require TWO arguments\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}

		if (opt == 'V' && validate_number(argv[optind - 1], 10)) {
			printf("Option '-%c' require 1st argument as valid"
				" numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}

		if ((validate_number(argv[optind], 10) && opt != 'B'
		     && opt != 'e' && opt != 'G')) {
			printf("Option '-%c' require 2nd argument as valid"
			       " numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
	}

	if ((opt == 0 && *(long_options[long_index].flag) == 20)) {
		if (optind >= argc || *argv[optind] == '-') {
			printf("\nOption '-%c' require TWO arguments\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
	}

	if (opt == 'N' || (opt == 0 && *(long_options[long_index].flag) == 14)) {
		if ((optind + 1) >= argc || *argv[optind] == '-'
		     || *argv[optind + 1] == '-') {
			printf("\nOption '-%c' requires 3 arguments\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
		if (opt == 'N' && validate_number(argv[optind], 10)) {
			printf("Option '-%c' requires 2nd argument as valid"
			       " numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
		else {
			if (validate_number(argv[optind], 16)) {
				printf("Option  '-%c' requires 2nd argument as valid"
				       " hex value\n\n", opt);
				show_usage(argv[0]);
				return OOB_SUCCESS;
			}
		}

		if (validate_number(argv[optind + 1], 10)) {
			printf("Option '-%c' requires 3rd argument as valid"
			       " numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
	}

	if (opt == 0 && *(long_options[long_index].flag) == 16) {
		if ((optind + 1) >= argc || *argv[optind] == '-'
		     || *argv[optind + 1] == '-') {
			printf("\nOption '-%c' requires 3 arguments\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
		if (validate_number(argv[optind + 1], 10)) {
			printf("Option '-%c' requires 3rd argument as valid"
				" numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
	}

	if (opt == 'P') {
		if ((optind + 1) >= argc || *argv[optind] == '-'
		     || *argv[optind + 1] == '-') {
			printf("\nOption '-%c' requires 3 arguments\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
		if (validate_number(argv[optind], 10)
		    || validate_number(argv[optind + 1], 10)) {
			printf("Option '-%c' requires 2nd & 3rd argument"
			       " as valid numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
	}

	if (opt == 'T') {
		if ((optind + 1) >= argc
		     || *argv[optind + 1] == '-') {
			printf("\nOption '-%c' requires 3 arguments\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
		if (validate_number(argv[optind + 1], 10)) {
			printf("Option '-%c' requires 2nd & 3rd argument"
			       " as valid numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
	}

	if (opt == 'R') {
		if ((optind + 3) >= argc || *argv[optind] == '-'
		     || *argv[optind + 1] == '-' || *argv[optind + 2] == '-'
		     || *argv[optind] == '-' || *argv[optind + 3] == '-') {
			printf("\nOption '-%c' requires 5 arguments\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
		if (validate_number(argv[optind], 10)
		    || validate_number(argv[optind + 3], 10)) {
			printf("Option '-%c' requires 2nd 5th argument"
				" as valid numeric value\n\n", opt);
			show_usage(argv[0]);
			return OOB_SUCCESS;
		}
	}

	switch (opt) {
	case 0:
		if (*(long_options[long_index].flag) == 1)
			get_apml_rmi_access(soc_num);
		else if (*(long_options[long_index].flag) == 2) {
			ret = get_apml_tsi_access(soc_num);
			if (ret)
				return ret;
		} else if (*(long_options[long_index].flag) == 3)
			apml_get_ddr_bandwidth(soc_num);
		else if (*(long_options[long_index].flag) == 5)
			/* get number of mca banks with valid */
			/* status after a fatal error */
			apml_get_ras_valid_mca_banks(soc_num);
		else if (*(long_options[long_index].flag) == 6)
			/* Get current active freq limit per socket */
			apml_get_freq_limit(soc_num);
		else if (*(long_options[long_index].flag) == 7)
			/* get svi based power telemetry for all rails */
			apml_get_pwr_telemetry(soc_num);
		else if (*(long_options[long_index].flag) == 8)
			/* Get Fmax and Fmin for socket */
			apml_get_sock_freq_range(soc_num);
		else if (*(long_options[long_index].flag) == 9)
			/* enable df pstate dynamic */
			apml_apb_enable(soc_num);
		else if (*(long_options[long_index].flag) == 10)
			/* ger current df pstate frequency */
			apml_get_fclkmclkuclk(soc_num);
		else if (*(long_options[long_index].flag) == 11)
			/* get base freq of socket */
			apml_get_cpu_base_freq(soc_num);
		else if (*(long_options[long_index].flag) == 12)
			/* get package energy */
			apml_get_pkg_energy(soc_num);
		else if (*(long_options[long_index].flag) == 14) {
			/* Write to sbrmi/sbtsi register */
			val = argv[optind - 1];
			val1 = strtoul(argv[optind++], &end, 16);
			val2 = atoi(argv[optind++]);
			write_register(soc_num, val1, val, val2);
		} else if (*(long_options[long_index].flag) == 15) {
			/* Read MSR register */
			val1 = strtoul(argv[optind - 1], &end, 16);
			val2 = atoi(argv[optind++]);
			read_msr_register(soc_num, val1, val2);
		} else if (*(long_options[long_index].flag) == 16) {
			/* Read CPUID register */
			/* CPUID Function register */
			val1 = strtoul(argv[optind - 1], &end, 16);
			/* CPUID Extended Function register */
			val2 = strtoul(argv[optind++], &end, 16);
			/* Thread Id */
			val3 = atoi(argv[optind++]);
			read_cpuid_register(soc_num, val1, val2,
					    val3);
		} else if (*(long_options[long_index].flag) == 17) {
			/* Read IOD Bist result */
			apml_get_iod_bist_status(soc_num);
		} else if (*(long_options[long_index].flag) == 18) {
			/* Read CCD Bist result */
			val1 = atoi(argv[optind - 1]);
			apml_get_ccd_bist_status(soc_num, val1);
		} else if (*(long_options[long_index].flag) == 19) {
			/* Read CCX Bist result */
			val1 = atoi(argv[optind - 1]);
			apml_get_ccx_bist_status(soc_num,
						 val1);
		} else if (*(long_options[long_index].flag) == 20) {
			/* Read NBIO error logging register */
			/* nbio quadrant */
			nbio.quadrant = strtoul(argv[optind - 1], &end, 16);
			/* register offset */
			nbio.offset = strtoul(argv[optind++], &end, 16);
			apml_get_nbio_error_log_reg(soc_num, nbio);
		} else if (*(long_options[long_index].flag) == 21) {
			/* Read DRAM Throttle */
			apml_get_dram_throttle(soc_num);
		} else if (*(long_options[long_index].flag) == 22) {
			/* Read Prochot status */
			apml_get_prochot_status(soc_num);
		} else if (*(long_options[long_index].flag) == 23) {
			/* Read Prochot Residency */
			apml_get_prochot_residency(soc_num);
		} else if (*(long_options[long_index].flag) == 25) {
			/* Read LCLK DPM Level range */
			val1 = atoi(argv[optind - 1]);
			apml_get_lclk_dpm_level_range(soc_num, val1);
		} else if (*(long_options[long_index].flag) == 29) {
			/* Read RAS last transaction address */
			apml_get_ras_last_transact_addr(soc_num);
		} else if (*(long_options[long_index].flag) == 30) {
			/* Read power consumed for a socket */
			apml_get_power_consumed(soc_num);
		} else if (*(long_options[long_index].flag) == 31) {
			/* Read SMT enabled status */
			apml_get_smt_status(soc_num);
		} else if (*(long_options[long_index].flag) == 32) {
			/* Show threads per core and threads per socket */
			apml_get_threads_per_core_and_soc(soc_num);
		} else if (*(long_options[long_index].flag) == 33) {
			/* Show maximum number of cores per ccx
			 * and logical ccx instance numbers
			 */
			apml_get_ccx_info(soc_num);
		} else {
			printf(RED "Try `%s --help' for more "
			       "information."RESET "\n\n", argv[0]);
			return OOB_SUCCESS;
		}
		break;
	case 'Y':
		/* Get the summary of mailbox commands for a given bus_num, */
		/* addr */
		show_apml_mailbox_cmds(soc_num);
		break;
	case 'p':
		/* Get the power metrics for a given bus_num, addr */
		apml_get_sockpower(soc_num);
		break;
	case 't':
		/* Get tdp value for a given soc_num */
		apml_get_socktdp(soc_num);
		break;
	case 's':
		power = atoi(argv[optind - 1]);
		apml_setpower_limit(soc_num, power);
		break;
	case 'b':
		/* Get apml boostlimit for a given soc_num */
		/* and thread index */
		thread_ind = atoi(argv[optind - 1]);
		get_boostlimit(soc_num, thread_ind);
		break;
	case 'd':
		thread_ind = atoi(argv[optind - 1]);
		boostlimit = atoi(argv[optind++]);
		set_apml_boostlimit(soc_num, thread_ind, boostlimit);
		break;
	case 'a':
		boostlimit = atoi(argv[optind - 1]);
		set_apml_socket_boostlimit(soc_num, boostlimit);
		break;
	case 'l':
		dram_thr = atoi(argv[optind - 1]);
		set_and_verify_dram_throttle(soc_num, dram_thr);
		break;
	case 'u':
		uprate = atof(argv[optind - 1]);
		set_and_verify_apml_socket_uprate(soc_num, uprate);
		break;
	case 'X':
		temp = atof(argv[optind - 1]);
		set_high_temp_threshold(soc_num, temp);
		break;
	case 'w':
		temp = atof(argv[optind - 1]);
		set_low_temp_threshold(soc_num, temp);
		break;
	case 'x':
		temp = atof(argv[optind - 1]);
		set_temp_offset(soc_num, temp);
		break;
	case 'y':
		value = atoi(argv[optind - 1]);
		set_timeout_config(soc_num, value);
		break;
	case 'g':
		value = atoi(argv[optind - 1]);
		set_alert_threshold(soc_num, value);
		break;
	case 'j':
		value = atoi(argv[optind - 1]);
		set_alert_config(soc_num, value);
		break;
	case 'k':
		value = atoi(argv[optind - 1]);
		set_tsi_config(soc_num, value, 'k');
		break;
	case 'm':
		value = atoi(argv[optind - 1]);
		set_tsi_config(soc_num, value, 'm');
		break;
	case 'n':
		value = atoi(argv[optind - 1]);
		set_tsi_config(soc_num, value, 'n');
		break;
	case 'o':
		value = atoi(argv[optind - 1]);
		set_tsi_config(soc_num, value, 'o');
		break;
	case 'P':
		/* Write BMC reported dim power and update rate value to */
		/* given socket index and dimm id */
		dp_soc_num.dimm_addr = strtoul(argv[optind - 1], &end, 16);
		dp_soc_num.power = atoi(argv[optind++]);
		dp_soc_num.update_rate = atoi(argv[optind++]);
		apml_set_dimm_power(soc_num, dp_soc_num);
		break;
	case 'T':
		/* Write BMC reported dimm temperature and update rate value */
		/* to given socket index and dimm id */
		dt_soc_num.dimm_addr = strtoul(argv[optind - 1], &end, 16);
		temp = atof(argv[optind++]);
		//dt_soc_num.sensor = strtoul(argv[optind++], &end, 16);
		dt_soc_num.update_rate = atoi(argv[optind++]);
		apml_set_thermal_sensor(soc_num, dt_soc_num, temp);
		break;
	case 'R':
		/* Read 32 bit data from extended pci config space */
		pci_addr.segment = atoi(argv[optind - 1]);
		pci_addr.offset = atoi(argv[optind++]);
		pci_addr.bus = strtoul(argv[optind++], &end, 16);
		pci_addr.device = strtoul(argv[optind++], &end, 16);
		pci_addr.func = atoi(argv[optind++]);
		apml_get_ras_pcie_config_data(soc_num, pci_addr);
		break;
	case 'D':
		/* read 32 bit data from MCA bank reported by */
		/* validity check mesg */
		mca_dump.index = atoi(argv[optind - 1]);
		mca_dump.offset = atoi(argv[optind++]);
		apml_get_ras_mca_msr(soc_num, mca_dump);
		break;
	case 'F':
		/* Get FCH reset reson code */
		val1 = atoi(argv[optind - 1]);
		apml_get_fch_reset_reason(soc_num, val1);
		break;
	case 'S':
		/* Get per dimm temperature range and refresh rate */
		/* from MR4 register */
		dimm_addr = strtoul(argv[optind - 1], &end, 16);
		apml_get_temp_range_and_refresh_rate(soc_num, dimm_addr);
		break;
	case 'O':
		/* Get dimm power when BMC doesn't own SPD side band bus */
		dimm_addr = strtoul(argv[optind - 1], &end, 16);
		apml_get_dimm_power(soc_num, dimm_addr);
		break;
	case 'E':
		/* Get dimm temperature when BMC doesn't own SPD side */
		/* band bus */
		dimm_addr = strtoul(argv[optind - 1], &end, 16);
		apml_get_dimm_temp(soc_num, dimm_addr);
		break;
	case 'C':
		/* Get current active cclk limit */
		val1 = atoi(argv[optind - 1]);
		apml_get_cclklimit(soc_num, val1);
		break;
	case 'B':
		/* get current bandwidth on io link */
		link_name = argv[optind - 1];
		bw_type = argv[optind++];
		apml_get_iobandwidth(soc_num, link_name, bw_type);
		break;
	case 'G':
		/* get current bandwidth on xgmi link */
		link_name = argv[optind - 1];
		bw_type = argv[optind++];
		apml_get_xgmibandwidth(soc_num, link_name, bw_type);
		break;
	case 'H':
		/* set gmi3 link width */
		val1 = atoi(argv[optind - 1]);
		val2 = atoi(argv[optind++]);
		apml_set_gmi3link_width(soc_num, val1, val2);
		break;
	case 'L':
		/* set xgmi link width */
		val1 = atoi(argv[optind - 1]);
		val2 = atoi(argv[optind++]);
		apml_set_xgmilink_width(soc_num, val1,  val2);
		break;
	case 'M':
		/* disable dynamic pstate of df and set the user */
		/* specified pstate */
		val1 = atoi(argv[optind - 1]);
		apml_set_dfpstate(soc_num, val1);
		break;
	case 'N':
		/* set the max and min lclk dpm level on given nbio */
		lclk.nbio_id = atoi(argv[optind - 1]);
		lclk.dpm.max_dpm_level = atoi(argv[optind++]);
		lclk.dpm.min_dpm_level = atoi(argv[optind++]);
		apml_set_lclk_dpm_level(soc_num, lclk);
		break;
	case 'Z':
		/* Control pcie rate on gen5 capable devices */
		val1 = atoi(argv[optind - 1]);
		apml_set_pciegen5_control(soc_num, val1);
		break;
	case 'U':
		/* select power efficiency profile policy */
		val1 = atoi(argv[optind - 1]);
		apml_set_pwr_efficiency_mode(soc_num, val1);
		break;
	case 'J':
		/* get core energy */
		val1 = atoi(argv[optind - 1]);
		apml_get_core_energy(soc_num, val1);
		break;
	case 'V':
		/* get data fabric pstate value */
		val1 = atoi(argv[optind - 1]);
		val2 = atoi(argv[optind++]);
		apml_set_df_pstate_range(soc_num, val1, val2);
		break;
	case 'e':
		/* read register */
		val = argv[optind - 1];
		val1 = strtoul(argv[optind++], &end, 16);
		read_register(soc_num, val1, val);
		break;
	case 'h':
		if (argc > 3 && (validate_number(argv[3], 10))) {
			show_module_commands(argv[0], argv[3]);
		} else
			show_usage(argv[0]);
		return OOB_SUCCESS;
	case ':':
		/* missing option argument */
		printf(RED "%s: option '%s' requires an argument."
			RESET"\n\n", argv[0], argv[optind - 1]);
		break;
	case '?':
		printf("Unrecognized option %s\n", argv[2]);
		printf(RED "Try `%s --help' for more"
		       " information." RESET "\n", argv[0]);
		return OOB_SUCCESS;
	default:
		printf(RED "Try `%s --help' for more information."
			RESET "\n\n", argv[0]);
		return OOB_SUCCESS;
	} // end of Switch
	}

	if (optind < argc) {
		printf(RED "\nExtra Non-option argument<s> passed : %s"
				RESET"\n", argv[optind]);
		printf(RED "Try `%s --help' for more information."
				RESET"\n", argv[0]);
	}

	return OOB_SUCCESS;
}


static void rerun_sudo(int argc, char **argv)
{
	static char *args[ARGS_MAX];
	char sudostr[] = "sudo";
	int i;

	args[0] = sudostr;
	for (i = 0; i < argc; i++)
		args[i + 1] = argv[i];
	args[i + 1] = NULL;
	execvp("sudo", args);
}

/*
 * Main program.
 * @param argc number of command line parameters
 * @param argv list of command line parameters
 */
int main(int argc, char **argv)
{
	uint32_t soc_num;
	oob_status_t ret;

	if (getuid() != 0)
		rerun_sudo(argc, argv);

	show_smi_message();

	/* Parse command arguments */
	ret = parseesb_args(argc, argv);
	if (ret)
		return ret;

	show_smi_end_message();

	return ret;
}
