/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __UPGRADE_APP_H_
#define __UPGRADE_APP_H_

#include "dfu_lib/nrf_dfu_flash.h"

#define INVALID_SOCKET       -1
#define INVALID_SEC_TAG      -1

#define FTP_USER_ANONYMOUS      "anonymous"
#define FTP_PASSWORD_ANONYMOUS  "anonymous@example.com"
#define FTP_DEFAULT_PORT        21

#define FTP_MAX_URL          128  /** max size of URL string */
#define FTP_MAX_FILEPATH     128  /** max size of filepath string */
#define FTP_MAX_USERNAME     32   /** max size of username in login */
#define FTP_MAX_PASSWORD     32   /** max size of password in login */


enum fota_state { 
    IDLE, 
    UPDATE_SERIAL,  //serial port update
    UPDATE_DOWNLOAD,    //ftp download update
    UPDATE_PENDING, 
    UPDATE_APPLY 
};


struct ftp_server_t {
    char hostname[FTP_MAX_URL];
    uint16_t port;
	char username[FTP_MAX_USERNAME];
	char password[FTP_MAX_PASSWORD];
};


struct image_t {
    uint32_t file_length;	//fetch length of file
    uint32_t total_length;	//received total length of file
};


extern struct image_t upgrade;	

extern void apply_state(enum fota_state new_state);

extern void image_data_save(uint8_t *data, uint16_t length);

extern int fetch_file_length(const uint8_t *msg);

extern void ftp_client_init(void);

#endif