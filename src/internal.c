/**
 * \file internal.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of internal libnetconf's functions.
 *
 * Copyright (C) 2012 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#define _BSD_SOURCE
#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "netconf_internal.h"

static const char rcsid[] __attribute__((used)) ="$Id: "__FILE__": "RCSID" $";

/* defined in datastore.c */
int ncds_sysinit(void);

int verbose_level = 0;

void nc_verbosity(NC_VERB_LEVEL level)
{
	verbose_level = level;
}

void prv_print(NC_VERB_LEVEL level, const char* msg)
{
	if (callbacks.print != NULL) {
		callbacks.print(level, msg);
	}
}

struct nc_shared_info *nc_info = NULL;
static int shmid = -1;

#define NC_INIT_DONE  0x00000001
static int init_flags = 0;

int nc_init(int flags)
{
	int retval = 0, r;
	key_t key = -2;
	int first = 1;
	char* t;
	pthread_rwlockattr_t rwlockattr;

	if (init_flags & NC_INIT_DONE) {
		ERROR("libnetconf already initiated!");
		return (-1);
	}

	DBG("Shared memory key: %d", key);
	shmid = shmget(key, sizeof(struct nc_shared_info), IPC_CREAT | IPC_EXCL | 0777 );
	if (shmid == -1 && errno == EEXIST) {
		shmid = shmget(key, sizeof(struct nc_shared_info), 0777);
		retval = 1;
		first = 0;
	}
	if (shmid == -1) {
		ERROR("Accessing shared memory failed (%s).", strerror(errno));
		return (-1);
	}
	DBG("Shared memory ID: %d", shmid);

	/* attach memory */
	nc_info = shmat(shmid, NULL, 0);
	if (nc_info == (void*) -1) {
		ERROR("Attaching shared memory failed (%s).", strerror(errno));
		nc_info = NULL;
		return (-1);
	}

	/* todo use locks */
	if (first) {
		/* lock */
		pthread_rwlockattr_init(&rwlockattr);
		pthread_rwlockattr_setpshared(&rwlockattr, PTHREAD_PROCESS_SHARED);
		if ((r = pthread_rwlock_init(&(nc_info->lock), &rwlockattr)) != 0) {
			ERROR("Shared information lock initialization failed (%s)", strerror(r));
			shmdt(nc_info);
			return (-1);
		}
		pthread_rwlockattr_destroy(&rwlockattr);
		memset(nc_info, 0, sizeof(struct nc_shared_info));

		/* init the information structure */
		pthread_rwlock_wrlock(&(nc_info->lock));
		strncpy(nc_info->stats.start_time, t = nc_time2datetime(time(NULL)), TIME_LENGTH);
		free(t);
	} else {
		pthread_rwlock_wrlock(&(nc_info->lock));
	}
	nc_info->stats.participants++;
	pthread_rwlock_unlock(&(nc_info->lock));

	/* init internal datastores */
	if (ncds_sysinit() != EXIT_SUCCESS) {
		shmdt(nc_info);
		return (-1);
	}

	/* init NETCONF sessions statistics */
	nc_session_monitoring_init();

	/* init Notification subsystem */
	if (flags & NC_INIT_NOTIF) {
		if (ncntf_init() != EXIT_SUCCESS) {
			shmdt(nc_info);
			return (-1);
		}
		init_flags |= NC_INIT_NOTIF;
	}

	init_flags |= NC_INIT_DONE;
	return (retval);
}

int nc_close(int system)
{
	struct shmid_ds ds;
	int retval = 0;

	if (shmid == -1 || nc_info == NULL) {
		/* we've not been initiated */
		return (-1);
	}

	if (system) {
		if (shmctl(shmid, IPC_STAT, &ds) == -1) {
			ERROR("Unable to get status of shared memory (%s).", strerror(errno));
			return (-1);
		}
		if (ds.shm_nattch == 1) {
			shmctl(shmid, IPC_RMID, NULL);
		} else {
			retval = 1;
		}
	}

	pthread_rwlock_wrlock(&(nc_info->lock));
	nc_info->stats.participants--;
	pthread_rwlock_unlock(&(nc_info->lock));
	shmdt(nc_info);
	nc_info = NULL;

	/* close NETCONF session statistics */
	nc_session_monitoring_close();

	/* close Notification subsystem */
	if (init_flags & NC_INIT_NOTIF) {
		ncntf_close();
	}

	init_flags = 0;
	return (retval);
}

char* nc_clrwspace (const char* in)
{
	int i, j = 0, len = strlen(in);
	char* retval = strdup(in);
	if (retval == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	/* remove leading whitespace characters */
	for (i = 0, j = 0; i < len ; i++, j++) {
		while (retval[i] != '\0' && isspace(retval[i])) {
			i++;
		}
		retval[j] = retval[i];
	}

	/* remove trailing whitespace characters */
	while (j >= 0 && isspace(retval[j])) {
		retval[j] = '\0';
		j--;
	}

	return (retval);
}

time_t nc_datetime2time(const char* datetime)
{
	struct tm time;
	char* dt;
	int i;
	long int shift, shift_m;
	time_t retval;

	if (datetime == NULL) {
		return (-1);
	} else {
		dt = strdup(datetime);
	}

	if (strlen(dt) < 20 || dt[4] != '-' || dt[7] != '-' || dt[13] != ':' || dt[16] != ':') {
		ERROR("Wrong date time format not compliant to RFC 3339.");
		free(dt);
		return (-1);
	}

	memset(&time, 0, sizeof(struct tm));
	time.tm_year = atoi(&dt[0]) - 1900;
	time.tm_mon = atoi(&dt[5]) - 1;
	time.tm_mday = atoi(&dt[8]);
	time.tm_hour = atoi(&dt[11]);
	time.tm_min = atoi(&dt[14]);
	time.tm_sec = atoi(&dt[17]);

	retval = timegm(&time);

	/* apply offset */
	i = 19;
	if (dt[i] == '.') { /* we have fractions to skip */
		for (i++; isdigit(dt[i]); i++);
	}
	if (dt[i] == 'Z' || dt[i] == 'z') {
		/* zero shift */
		shift = 0;
	} else if (dt[i+3] != ':') {
		/* wrong format */
		ERROR("Wrong date time shift format not compliant to RFC 3339.");
		free(dt);
		return (-1);
	} else {
		shift = strtol(&dt[i], NULL, 10);
		shift = shift * 60 * 60; /* convert from hours to seconds */
		shift_m = strtol(&dt[i+4], NULL, 10) * 60; /* includes conversion from minutes to seconds */
		/* correct sign */
		if (shift < 0) {
			shift_m *= -1;
		}
		/* connect hours and minutes of the shift */
		shift = shift + shift_m;
	}
	/* we have to shift to the opposite way to correct the time */
	retval -= shift;

	free(dt);
	return (retval);
}

char* nc_time2datetime(time_t time)
{
	char* date = NULL;
	char* zoneshift = NULL;
        int zonediff, zonediff_h, zonediff_m;
        struct tm tm;

	if (gmtime_r(&time, &tm) == NULL) {
		return (NULL);
	}

	if (tm.tm_isdst < 0) {
		zoneshift = NULL;
	} else {
		if (tm.tm_gmtoff == 0) {
			/* time is Zulu (UTC) */
			if (asprintf(&zoneshift, "Z") == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				return (NULL);
			}
		} else {
			zonediff = tm.tm_gmtoff;
			zonediff_h = zonediff / 60 / 60;
			zonediff_m = zonediff / 60 % 60;
			if (asprintf(&zoneshift, "%s%02d:%02d",
			                (zonediff < 0) ? "-" : "+",
			                zonediff_h,
			                zonediff_m) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				return (NULL);
			}
		}
	}
	if (asprintf(&date, "%04d-%02d-%02dT%02d:%02d:%02d%s",
			tm.tm_year + 1900,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
	                (zoneshift == NULL) ? "" : zoneshift) == -1) {
		free(zoneshift);
		ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	free (zoneshift);

	return (date);
}
