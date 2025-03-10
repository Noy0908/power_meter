#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <zephyr/posix/fcntl.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/tls_credentials.h>
#include <net/ftp_client.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
// #include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(tcp_sample);

#include "upgrade_app.h"


// RING_BUF_DECLARE(ftp_data_buf, SLM_MAX_MESSAGE_SIZE);

struct ftp_server_t server;


static struct k_work fota_work;

static enum fota_state state = IDLE;


static void ftp_ctrl_callback(const uint8_t *msg, uint16_t len)
{
	char code_str[4];  /* Proprietary code 900 ~ 999 */
	int code;

	strncpy(code_str, msg, sizeof(code_str) - 1);
	code_str[sizeof(code_str) - 1] = '\0';
	code = atoi(code_str);
	if (FTP_PROPRIETARY(code)) {
		switch (code) {
		case FTP_CODE_901:
			LOG_WRN("Disconnected by remote server!\r\n");
			break;
		case FTP_CODE_902:
            LOG_WRN("Connection aborted!\r\n");
			break;
		case FTP_CODE_903:
            LOG_WRN("Socket poll error!\r\n");
			break;
		case FTP_CODE_904:
            LOG_WRN("Unexpected poll event!\"\r\n");
			break;
		case FTP_CODE_905:
			LOG_WRN("Network down!\r\n");
			break;
		default:
			LOG_WRN("Unexpected error!\r\n");
			break;
		}
		// if (ftp_data_mode_handler && exit_datamode_handler(-EAGAIN)) {
		// 	ftp_data_mode_handler = NULL;
		// }
		return;
	}

	// if (ftp_verbose_on) {
	// 	data_send((uint8_t *)msg, len);
	// }
}


static int do_ftp_open(struct ftp_server_t *server)
{
    int ret = 0;
    sec_tag_t sec_tag = INVALID_SEC_TAG;

    if (strlen(server->username) == 0) 
    {
        strcpy(server->username, FTP_USER_ANONYMOUS);
        strcpy(server->password, FTP_PASSWORD_ANONYMOUS);
    } 
    else 
    {
         /* FTP open */
        ret = ftp_open(server->hostname, server->port, sec_tag);
        if (ret != FTP_CODE_200) 
        {
            return -ENETUNREACH;
        }

        /* FTP login */
        ret = ftp_login(server->username, server->password);
        if (ret != FTP_CODE_230) 
        {
            return -EACCES;
        }
    }
    
    return 0;
}


static int do_ftp_close(void)
{
	int ret = ftp_close();

	return (ret == FTP_CODE_221) ? 0 : -1;
}


static int do_ftp_ls(char *filepath)
{
    int ret;

    ret = ftp_list("-l", filepath);
    if (ret == FTP_CODE_226) 
    {
        return 0;
    } 
    else 
    {
        return -1;
    }
}



static int download_image_file(struct ftp_server_t *server)
{
    int ret;

    char *file_name = strrchr(server->hostname, '/');
    if (file_name == NULL) 
    {
        LOG_ERR("Invalid file path\n");
        return -ENOENT;
    }
    file_name++;

	ret = do_ftp_open(server);
	if (ret) 
	{
		LOG_ERR("Failed to open FTP connection\n");		
		return ret;
	}


    ret = ftp_type(FTP_TYPE_BINARY);
    if (ret != FTP_CODE_200) 
    {
        LOG_ERR("Failed to set binary mode\n");
        return ret;
    }

    ret = ftp_get(file_name);
    if (ret != FTP_CODE_226) 
    {
        LOG_ERR("Failed to download file\n");
        return ret;
    }
    LOG_INF("File downloaded successfully\n");

	return ret;
}


void apply_state(enum fota_state new_state)
{
	__ASSERT(state != new_state, "State already set: %d", state);

	state = new_state;

	switch (new_state) {
	case IDLE:
		break;
	case CONNECTED:
        state = UPDATE_DOWNLOAD;
		LOG_INF("Now enter 'download' to download new application firmware\n");
		break;
	case UPDATE_DOWNLOAD:
		k_work_submit(&fota_work);
		break;
	case UPDATE_PENDING:
        state = UPDATE_APPLY;
		LOG_INF("Now enter 'apply' to apply new application firmware\n");
		break;
	case UPDATE_APPLY:
		k_work_submit(&fota_work);
		break;
	}
}


static void fota_work_cb(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);
	
	memset(&server, 0, sizeof(server));
	memcpy(server.hostname, CONFIG_FTP_DOWNLOAD_HOST, strlen(CONFIG_FTP_DOWNLOAD_HOST));
	server.port = CONFIG_FTP_DOWNLOAD_PORT;
	memcpy(server.username, CONFIG_FTP_DOWNLOAD_USER, strlen(CONFIG_FTP_DOWNLOAD_USER));
	memcpy(server.password, CONFIG_FTP_DOWNLOAD_PASSWORD, strlen(CONFIG_FTP_DOWNLOAD_PASSWORD));

	switch (state) {
	case UPDATE_DOWNLOAD:
		err = download_image_file(&server);
		if (err) {
			printk("Download failed, err %d\n", err);
			apply_state(CONNECTED);
		}
		break;
	case UPDATE_APPLY:
        // err = fota_image_apply();
		sys_reboot(SYS_REBOOT_WARM);
		break;
	default:
		break;
	}
}


void ftp_client_init(void)
{
    // ftp_verbose_on = true;
	// ftp_data_mode_handler = NULL;

	ftp_init(ftp_ctrl_callback, NULL);

    k_work_init(&fota_work, fota_work_cb);
}