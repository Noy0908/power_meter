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
// #include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(tcp_sample,CONFIG_TCP_LOG_LEVEL);

#include "upgrade_app.h"
#include "dfu_lib/nrf_dfu_flash.h"


// RING_BUF_DECLARE(ftp_data_buf, SLM_MAX_MESSAGE_SIZE);

struct ftp_server_t server;


static struct k_work fota_work;

static enum fota_state state = IDLE;

bool download_finished = false;

static uint32_t total_length = 0;


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


static int do_ftp_status(void)
{
	int ret = ftp_status();

	return (ret == FTP_CODE_211) ? 0 : -1;
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
	char *file_name = "zephyr.signed.bin";
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

	// ret = do_ftp_ls(NULL);
	// if (ret)		
	// {
	// 	LOG_ERR("Failed to get FTP status\n");		
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
	// err = boot_erase_img_bank(IMAGE1_ID);
	// if(err)
	// {
	// 	LOG_ERR("erase Secondary slot failed!");
	// 	return err;
	// }

	err = dfu_flash_start(0);
	if(err)
	{
		LOG_ERR("Failed to dfu_start, errno %d", err);
	}

	return err;
}


static void fota_work_cb(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	switch (state) {
	case UPDATE_DOWNLOAD:
		total_length = 0;
		// start_dfu_process();		//test by Noy
		ftp_uninit();

		memset(&server, 0, sizeof(server));
		memcpy(server.hostname, CONFIG_FTP_DOWNLOAD_HOST, strlen(CONFIG_FTP_DOWNLOAD_HOST));
		server.port = CONFIG_FTP_DOWNLOAD_PORT;
		memcpy(server.username, CONFIG_FTP_DOWNLOAD_USER, strlen(CONFIG_FTP_DOWNLOAD_USER));
		memcpy(server.password, CONFIG_FTP_DOWNLOAD_PASSWORD, strlen(CONFIG_FTP_DOWNLOAD_PASSWORD));

		err = download_image_file(&server);
		if (err) {
			printk("Download failed, err %d\n", err);
			// apply_state(CONNECTED);
			// do_ftp_close();
			apply_state(IDLE);
		}
		else
		{
			printk("Downloadcompleted!\n");
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




static void ftp_data_save(uint8_t *data, uint16_t length)
{
	// LOG_HEXDUMP_INF(data, length, "FTP received:");
	

	total_length += length;
	LOG_INF("FTP total_length = %d\n", total_length);

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

//    const struct download_client_evt evt = {
// 	   .id = DOWNLOAD_CLIENT_EVT_FRAGMENT,
// 	   .fragment = {
// 		   .buf = msg,
// 		   .len = len,
// 	   }
//    };

//    fota_download_external_evt_handler(&evt);
}


void ftp_client_init(void)
{
    // ftp_verbose_on = true;
	// ftp_data_mode_handler = NULL;

	ftp_init(ftp_ctrl_callback, ftp_data_callback);

    k_work_init(&fota_work, fota_work_cb);
}