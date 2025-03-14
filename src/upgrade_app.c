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
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(tcp_sample,CONFIG_TCP_LOG_LEVEL);

#include "upgrade_app.h"
#include "dfu_lib/nrf_dfu_flash.h"


struct ftp_server_t server;

static struct k_work fota_work;

static enum fota_state state = IDLE;

bool download_finished = false;

static uint32_t file_length = 0;	//fetch length of file
static uint32_t total_length = 0;	//received total length of file


static int fetch_file_length(const uint8_t *msg)
{
    int bytes;
    
    if (sscanf(msg, "%*[^()](%d bytes)", &bytes) == 1) {
        LOG_INF("Fetch file length: %d\n", bytes);
    } else {
        LOG_WRN("Can not fetch file length\n");
    }
	return bytes;
}

static void ftp_ctrl_callback(const uint8_t *msg, uint16_t len)
{
	char code_str[4];  /* Proprietary code 900 ~ 999 */
	int code;

	strncpy(code_str, msg, sizeof(code_str) - 1);
	code_str[sizeof(code_str) - 1] = '\0';
	code = atoi(code_str);
	if (FTP_PROPRIETARY(code)) 
	{
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
	
		return;
	}
	else if(FTP_PRELIMINARY_POS(code))
	{
		if(code == FTP_CODE_150)
		{
			file_length = fetch_file_length(msg);
			// LOG_INF("File status okay; about to open data connection\r\n");
		}	
	}
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


static int do_ftp_status(void)
{
	int ret = ftp_status();

	return (ret == FTP_CODE_211) ? 0 : -1;
}

#if 0
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


static int do_ftp_cd(char *dirpath)
{
	int ret;

	ret = ftp_cwd(dirpath);
	return (ret == FTP_CODE_250) ? 0 : -1;
}
#endif


static int do_ftp_close(void)
{
	int ret = ftp_close();

	return (ret == FTP_CODE_221) ? 0 : -1;
}


static int download_image_file(struct ftp_server_t *server)
{
    int ret;
	char *file_name = "zephyr.signed.bin";
	// char *file_path = "home";

    // char *file_name = strrchr(server->hostname, '/');
    // if (file_name == NULL) 
    // {
    //     LOG_ERR("Invalid file path\n");
    //     return -ENOENT;
    // }
    // file_name++;

	ret = do_ftp_open(server);
	if (ret) 
	{
		LOG_ERR("Failed to open FTP connection\n");		
		return ret;
	}

	ret = do_ftp_status();
	if (ret)		
	{
		LOG_ERR("Failed to get FTP status\n");		
		return ret;
	}

	// ret = do_ftp_ls(file_name);
	// if (ret)		
	// {
	// 	LOG_ERR("Failed to get FTP status\n");		
	// 	return ret;
	// }

	// ret = do_ftp_cd(file_path);
	// if (ret)		
	// {
	// 	LOG_ERR("Failed to change directory path!\n");		
	// 	return ret;
	// }

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
	// __ASSERT(state != new_state, "State already set: %d", state);		
	state = new_state;

	switch (new_state) {
	case IDLE:
		break;
	case CONNECTED:
		do_ftp_close();
        state = UPDATE_DOWNLOAD;
		LOG_INF("Now enter 'download' to download new application firmware\n");
		break;
	case UPDATE_DOWNLOAD:
		k_work_submit(&fota_work);
		LOG_INF("Now start to download new application firmware\n");
		break;
	case UPDATE_PENDING:
		if(total_length >= 0)
		{
			 // state = UPDATE_APPLY;
			LOG_INF("file download succsee, now enter mcuboot!\n");
		}
		else
		{
			state = UPDATE_DOWNLOAD;		//download fiale, reconnect and download again
			LOG_INF("Now enter 'download' to download new application firmware\n");	
		}
		k_work_submit(&fota_work);
		break;
	case UPDATE_APPLY:
		k_work_submit(&fota_work);
		break;
	}
}


static int start_dfu_process(void)
{
	int err = 0;

	file_length = 0;
	total_length = 0;
	download_finished = false;
	memset(&server, 0, sizeof(server));
	memcpy(server.hostname, CONFIG_FTP_DOWNLOAD_HOST, strlen(CONFIG_FTP_DOWNLOAD_HOST));
	server.port = CONFIG_FTP_DOWNLOAD_PORT;
	memcpy(server.username, CONFIG_FTP_DOWNLOAD_USER, strlen(CONFIG_FTP_DOWNLOAD_USER));
	memcpy(server.password, CONFIG_FTP_DOWNLOAD_PASSWORD, strlen(CONFIG_FTP_DOWNLOAD_PASSWORD));

	err = dfu_flash_start(0);
	if(err)
	{
		LOG_ERR("Failed to init flash: %d", err);
		return err;
	}

	err = download_image_file(&server);
	if (err != FTP_CODE_226) {
		LOG_INF("Download failed, err %d\n", err);
	}
	else
	{
		LOG_INF("FTP received_length=%d	file_length=%d\n", total_length, file_length);
		if(file_length == total_length)
		{
			// apply_state(UPDATE_PENDING);
			download_finished = true;
			LOG_INF("Download completed!\n");
		}
		else
		{
			// apply_state(UPDATE_DOWNLOAD);
			LOG_INF("File is corrupted, drop it!\n");
		}
	}
	
	err = do_ftp_close();

	return err;
}

// static void start_download_image(void)
// {
// 	total_length = 0;
// 	download_finished = false;
// 	start_dfu_process();		//test by Noy
	
// 	memset(&server, 0, sizeof(server));
// 	memcpy(server.hostname, CONFIG_FTP_DOWNLOAD_HOST, strlen(CONFIG_FTP_DOWNLOAD_HOST));
// 	server.port = CONFIG_FTP_DOWNLOAD_PORT;
// 	memcpy(server.username, CONFIG_FTP_DOWNLOAD_USER, strlen(CONFIG_FTP_DOWNLOAD_USER));
// 	memcpy(server.password, CONFIG_FTP_DOWNLOAD_PASSWORD, strlen(CONFIG_FTP_DOWNLOAD_PASSWORD));

// 	err = download_image_file(&server);
// 	if (err != FTP_CODE_226) {
// 		LOG_INF("Download failed, err %d\n", err);
// 		// apply_state(CONNECTED);
// 		apply_state(IDLE);
// 	}
// 	else
// 	{
// 		if(file_length == total_length)
// 		{
// 			// apply_state(UPDATE_PENDING);
// 			download_finished = true;
// 			LOG_INF("Download completed!\n");
// 		}
// 		else
// 		{
// 			// apply_state(UPDATE_DOWNLOAD);
// 			LOG_INF("File is corrupted, drop it!\n");
// 		}
// 	}
	
// 	ftp_uninit();
// }


static void fota_work_cb(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	switch (state) {
	case UPDATE_DOWNLOAD:
		err = start_dfu_process();
		break;
	case UPDATE_APPLY:
        // err = fota_image_apply();
		sys_reboot(SYS_REBOOT_WARM);
		break;
	default:
		break;
	}
}


static void ftp_data_save(uint8_t *data, uint16_t length)
{
	// LOG_HEXDUMP_INF(data, length, "FTP received:");

	total_length += length;
	if(length < 708)
	{
		LOG_INF("FTP received_data=%d	total_length=%d\n", length, total_length);
	}
	// LOG_INF("FTP total_length = %d\n", total_length);
	
	// int rc = 0;
	// if(!download_finished)
	// {
	// 	rc = dfu_data_store(data, length, false);
	// 	if(rc != 0)
	// 	{
	// 		LOG_ERR("flash img write fail");
	// 	}
	// }
	// else
	// {
	// 	rc = dfu_data_store(data, length, true);
	// 	if(rc != 0)
	// 	{
	// 		LOG_INF("flash img write fail");
	// 	}

	// 	dfu_flash_finish();
	// }
}

static void ftp_data_callback(const uint8_t *msg, uint16_t len)
{
	ftp_data_save((uint8_t *)msg, len);
}


void ftp_client_init(void)
{
	ftp_init(ftp_ctrl_callback, ftp_data_callback);

    k_work_init(&fota_work, fota_work_cb);
}