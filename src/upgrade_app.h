/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __UPGRADE_APP_H_
#define __UPGRADE_APP_H_


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
    CONNECTED, 
    UPDATE_DOWNLOAD, 
    UPDATE_PENDING, 
    UPDATE_APPLY 
};


struct ftp_server_t {
    char hostname[FTP_MAX_URL];
    uint16_t port;
	char username[FTP_MAX_USERNAME];
	char password[FTP_MAX_PASSWORD];
};


extern void apply_state(enum fota_state new_state);


extern void ftp_client_init(void);

#endif