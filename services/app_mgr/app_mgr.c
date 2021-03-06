/*
 * Copyright (C) 2015-2019 Alibaba Group Holding Limited
 */
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include "amp_system.h"
#include "amp_fs.h"
#include "amp_task.h"
#include "amp_errno.h"
#include "amp_defines.h"
#include "app_mgr.h"
#include "ota_socket.h"
//#include "ota_service.h"
#include "ota_agent.h"
#include "mbedtls/md5.h"

#ifdef LINUXOSX
#define OTA_BUFFER_MAX_SIZE 8192
#else
#define OTA_BUFFER_MAX_SIZE 1536
#endif
#define HTTP_HEADER                      \
    "GET /%s HTTP/1.1\r\nAccept:*/*\r\n" \
    "User-Agent: Mozilla/5.0\r\n"        \
    "Cache-Control: no-cache\r\n"        \
    "Connection: close\r\n"              \
    "Host:%s:%d\r\n\r\n"

typedef struct {
    uint16_t file_count;
    uint16_t pack_version;
    uint32_t pack_size;
} JSEPACK_HEADER;

typedef struct {
    uint32_t header_size;
    uint32_t file_size;
    uint8_t md5[16];
    /* uint8_t  name[...];
       uint8 data[]; */
} JSEPACK_FILE;

static write_js_cb_t jspackcb = NULL;
static JSEPACK_HEADER header;
static JSEPACK_FILE fileheader;
static int32_t jspacksize = 0;

static int32_t jspackfile_offset;
static int32_t jspackfile_header_offset;
static int32_t jspackfile_count;
static int32_t jspack_done        = 0;
static int32_t jspack_found_error = 0;

#define MOD_STR "APP_MGR"
#define JSEPACK_BLOCK_SIZE 2 * 1024

static uint8_t *jspackdst_buf = NULL;

extern int amp_get_user_dir(char *dir);
extern void jsengine_exit();

void apppack_init(write_js_cb_t cb)
{
    jspackcb                 = cb;
    jspacksize               = 0;
    jspackfile_offset        = 0;
    jspackfile_header_offset = 0;
    jspack_done              = 0;
    jspackfile_count         = 0;
    jspack_found_error       = 0;

    jspackdst_buf = amp_malloc(JSEPACK_BLOCK_SIZE);

    char root_dir[128] = {0};
    amp_get_user_dir(root_dir);

    amp_debug(MOD_STR, "sizeof(fileheader) = %d", sizeof(fileheader));

    /* remove all js file */
    amp_rmdir(root_dir);
}

void apppack_final()
{
    jspackcb = NULL;
    amp_free(jspackdst_buf);
    jspackdst_buf = NULL;
    jspack_done   = 1;
}

static amp_md5_context g_ctx;
static uint8_t digest[16]      = {0};
static int32_t app_file_offset = 0;

static void jspackoutput(const char *filename, const uint8_t *md5,
                         int32_t file_size, int32_t type, int32_t offset,
                         uint8_t *buf, int32_t buf_len)
{
    int i;
    int outsize;

    if (offset == 0) {
        amp_md5_init(&g_ctx);
        amp_md5_starts(&g_ctx);
        app_file_offset = 0;
    }

    amp_md5_update(&g_ctx, buf, buf_len);

    if (buf_len > 0) {
        outsize = buf_len;
        if (jspackcb) {
            jspackcb(filename, file_size, type, app_file_offset, buf, outsize,
                     0);
        }

        app_file_offset += outsize;
        buf_len -= outsize;
        buf += outsize;
    }

    int32_t complete = 0;

    /* end of file, check MD5 */
    if (file_size == app_file_offset) {
        amp_md5_finish(&g_ctx, digest);
        amp_md5_free(&g_ctx);

        amp_warn(MOD_STR, "0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x "
                 "0x%x 0x%x 0x%x 0x%x",
                 digest[0], digest[1], digest[2], digest[3], digest[4],
                 digest[5], digest[6], digest[7], digest[8], digest[9],
                 digest[10], digest[11], digest[12], digest[13], digest[14],
                 digest[15]);

        amp_warn(MOD_STR, "0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x "
                 "0x%x 0x%x 0x%x 0x%x",
                 md5[0], md5[1], md5[2], md5[3], md5[4], md5[5], md5[6], md5[7],
                 md5[8], md5[9], md5[10], md5[11], md5[12], md5[13], md5[14],
                 md5[15]);

        jspackfile_count--;
        if (jspackfile_count == 0) {
            jspack_done = 1;
        }

        if (memcmp(digest, md5, 16) == 0) {
            complete = 1; /* check success */
        } else {
            complete           = -1;
            jspack_found_error = 1;
            jspack_done        = 1;
        }

        if (jspackcb) {
            jspackcb(filename, app_file_offset, type, app_file_offset, NULL, 0,
                     complete);
        }
    }
}

#define JSEPACK_HEADER_SIZE 8  /* sizeof(JSEPACK_HEADER) */
#define JSEPACK_FILE_HEADER 24 /* sizeof(JSEPACK_FILE)  exclude filename */
static uint32_t g_file_header_size = 24 + 1;
static uint8_t *g_file_name        = NULL;

/* report process state */
void apppack_post_process_state()
{
    char msg[128];
    if (jspacksize >= JSEPACK_HEADER_SIZE) {
        sprintf(msg, "%d/%d", jspacksize, header.pack_size);
    }
}

/* analysis recursive */
int apppack_update(uint8_t *ptr, int size)
{
    int len = 0;
    uint8_t *pdst;

    if (jspack_found_error) return -1;

    if ((size < 1) || (jspack_done)) {
        amp_debug(MOD_STR, "size: %d, jspack_done: %d", size, jspack_done);
        return 0;
    }

    amp_debug(MOD_STR, "jspacksize = %d size=%d ", jspacksize, size);
    amp_debug(MOD_STR, "jspackfile_header_offset = %d ", jspackfile_header_offset);

    amp_debug(MOD_STR, "g_file_header_size = %d ", g_file_header_size);

    /* parse file header */
    if (jspacksize == 0) {
        if (size > JSEPACK_HEADER_SIZE) {
            jspacksize = JSEPACK_HEADER_SIZE;
        } else {
            jspacksize = size;
        }

        memcpy(&header, ptr, jspacksize);
        if (jspacksize < JSEPACK_HEADER_SIZE) {
            return 0;
        }

        return apppack_update(ptr + jspacksize, size - jspacksize);
    } else if (jspacksize < JSEPACK_HEADER_SIZE) {
        len = JSEPACK_HEADER_SIZE - jspacksize;
        if (len > size) {
            len = size;
        }

        pdst = (uint8_t *)&header;
        memcpy(pdst + jspacksize, ptr, len);
        jspacksize += len;

        return apppack_update(ptr + len, size - len);
    } else if (jspacksize == JSEPACK_HEADER_SIZE) {
        /* start parse file header */

        jspackfile_count = header.file_count;
        amp_warn(MOD_STR, "file_count = %d ", header.file_count);
        amp_warn(MOD_STR, "pack_version = %d ", header.pack_version);
        amp_warn(MOD_STR, "pack_size = %d ", header.pack_size);

        /* reset jspackfile_header_offset */
        len = JSEPACK_FILE_HEADER; /* sizeof(JSEPACK_FILE) */
        if (len > size) {
            len = size;
        }

        memcpy(&fileheader, ptr, len);
        jspackfile_header_offset = len;
        jspacksize += len;
        jspackfile_offset = 0;

        return apppack_update(ptr + len, size - len);
    } else if (jspackfile_header_offset < JSEPACK_FILE_HEADER) {
        amp_warn(MOD_STR, "jspackfile_header_offset = %d size=%d ",
                 jspackfile_header_offset, size);

        len = JSEPACK_FILE_HEADER -
              jspackfile_header_offset; /* sizeof(JSEPACK_FILE) */
        if (len > size) {
            len = size;
        }

        pdst = (uint8_t *)&fileheader;
        memcpy(pdst + jspackfile_header_offset, ptr, len);

        jspackfile_header_offset += len;
        jspacksize += len;
        jspackfile_offset = 0;

        return apppack_update(ptr + len, size - len);

    } else if (jspackfile_header_offset == JSEPACK_FILE_HEADER) {
        /* read g_file_header_size */

        amp_warn(MOD_STR, "file_size = %d", fileheader.file_size);
        amp_warn(MOD_STR, "header_size = %d", fileheader.header_size);

        g_file_header_size = fileheader.header_size;

        amp_warn(MOD_STR, "update g_file_header_size = %d", g_file_header_size);

        amp_warn(MOD_STR, "filename len = %d", g_file_header_size - JSEPACK_FILE_HEADER);

        if (g_file_name) amp_free(g_file_name);
        g_file_name =
            amp_calloc(1, g_file_header_size - JSEPACK_FILE_HEADER + 1);

        /* get file name */
        len = g_file_header_size - JSEPACK_FILE_HEADER;
        if (len > size) {
            /* part of file name */
            len = size;
        }

        memcpy(g_file_name, ptr, len);

        jspackfile_header_offset += len;
        jspacksize += len;

        return apppack_update(ptr + len, size - len);

    } else if (jspackfile_header_offset < g_file_header_size) {
        /* read rest length of file name */
        len = g_file_header_size - jspackfile_header_offset;
        if (len > size) {
            /* part of file name */
            len = size;
        }

        int offset = jspackfile_header_offset - JSEPACK_FILE_HEADER;

        amp_warn(MOD_STR, "get filename, offset = %d  len = %d", offset, len);

        memcpy(g_file_name + offset, ptr, len);

        jspackfile_header_offset += len;
        jspacksize += len;

        return apppack_update(ptr + len, size - len);

    } else if (jspackfile_header_offset == g_file_header_size) {
        if (jspackfile_offset == 0) {
            amp_warn(MOD_STR, "name = %s", g_file_name);
            amp_warn(MOD_STR, "file_size = %d", fileheader.file_size);

            len = fileheader.file_size;
        } else {
            len = fileheader.file_size - jspackfile_offset;
        }

        if (len > size) {
            len = size;
        }

        /* max length */
        if (len > JSEPACK_BLOCK_SIZE) {
            len = JSEPACK_BLOCK_SIZE;
        }

        /* parse file */
        jspackoutput(g_file_name, fileheader.md5, fileheader.file_size, 3,
                     jspackfile_offset, ptr, len);

        jspacksize += len;
        jspackfile_offset += len;

        amp_warn(MOD_STR, "jspackfile_offset = %d file_size = %d", jspackfile_offset,
                 fileheader.file_size);

        if (jspackfile_offset == fileheader.file_size) {
            /* next file */
            jspackfile_header_offset = 0;
            jspackfile_offset        = 0;
            /* reset g_file_header_size to default */
            g_file_header_size = 24 + 1;
            if (jspackfile_count > 0)
                amp_warn(MOD_STR, "parse next file");
            else
                amp_warn(MOD_STR, "app pack parse complete");
        }

        amp_warn(MOD_STR, "jspacksize = %d len = %d", jspacksize, len);

        return apppack_update(ptr + len, size - len);
    }

    amp_warn(MOD_STR, "jspackfile_header_offset = %d", jspackfile_header_offset);
    amp_warn(MOD_STR, "g_file_header_size = %d", g_file_header_size);
    amp_warn(MOD_STR, "apppack check file content fail");

    return -1;
}


/**
 * @brief http_gethost_info
 *
 * @Param: src  url
 * @Param: web  WEB
 * @Param: file  download filename
 * @Param: port  default 80
 */
static void http_gethost_info(char *src, char **web, char **file, int *port)
{
    char *pa;
    char *pb;
    int isHttps = 0;

    if (!src || strlen(src) == 0) {
        amp_warn(MOD_STR, "http_gethost_info parms error!");
        return;
    }

    amp_warn(MOD_STR, "src = %s %d", src, strlen(src));

    *port = 0;
    if (!(*src)) {
        return;
    }

    pa = src;
    if (!strncmp(pa, "https://", strlen("https://"))) {
        pa      = src + strlen("https://");
        isHttps = 1;
    }

    if (!isHttps) {
        if (!strncmp(pa, "http://", strlen("http://"))) {
            pa = src + strlen("http://");
        }
    }

    *web = pa;
    pb   = strchr(pa, '/');
    if (pb) {
        *pb = 0;
        pb += 1;
        if (*pb) {
            *file                   = pb;
            *((*file) + strlen(pb)) = 0;
        }
    } else {
        (*web)[strlen(pa)] = 0;
    }

    pa = strchr(*web, ':');
    if (pa) {
        *pa   = 0;
        *port = atoi(pa + 1);
    } else {
        /* TODO: support https:443
        if (isHttps) {
            *port = 80;
        } else {
            *port = 80;
        } */
        *port = 80;
    }
}

int apppack_download_test2(void *params)
{
    int sockfd = 0;
    sockfd = ota_socket_connect(80, "appengine.oss-cn-hangzhou.aliyuncs.com");
    if (sockfd < 0) {
        amp_warn(MOD_STR, "ota_socket_connect error");
        return -1;
    }
    ota_socket_close(sockfd);
    return 0;
}

int apppack_download_test(void *params)
{
    int ret             = 0;
    int sockfd          = 0;
    int port            = 0;
    int nbytes          = 0;
    int send            = 0;
    int totalsend       = 0;
    uint32_t breakpoint = 0;
    int size            = 0;
    int header_found    = 0;
    char *pos           = 0;
    int file_size       = 0;
    char *host_file     = NULL;
    char *host_addr     = NULL;
    char *url = amp_malloc(256);
    strcpy(url, "http://appengine.oss-cn-hangzhou.aliyuncs.com/a1WGzkI1GJR.engine_001/1582118016285.bin");
   
    amp_warn(MOD_STR, "url = %s", url);

    if (!url || strlen(url) == 0) {
        amp_warn(MOD_STR, "ota_download parms error!");
        bone_websocket_send_frame("/device/updateapp_reply", 201,
                                  "Bad Request");
        return OTA_DOWNLOAD_INIT_FAIL;
    }

    char *http_buffer = amp_malloc(OTA_BUFFER_MAX_SIZE);

    amp_warn(MOD_STR, "http_buffer = %p", http_buffer);
    http_gethost_info(url, &host_addr, &host_file, &port);

    if (host_file == NULL || host_addr == NULL) {
        ret = OTA_DOWNLOAD_INIT_FAIL;
        amp_free(http_buffer);
        bone_websocket_send_frame("/device/updateapp_reply", 201,
                                  "Bad Request");
        return ret;
    }

    amp_warn(MOD_STR, "ota_socket_connect  %d %s", port, host_addr);

    sockfd = ota_socket_connect(port, host_addr);
    if (sockfd < 0) {
        amp_warn(MOD_STR, "ota_socket_connect error");
        ret = OTA_DOWNLOAD_CON_FAIL;
        amp_free(http_buffer);
        bone_websocket_send_frame("/device/updateapp_reply", 205,
                                  "connect failed");
        return ret;
    }

    breakpoint = 0;
    sprintf(http_buffer, HTTP_HEADER, host_file, host_addr, port);

    send      = 0;
    totalsend = 0;
    nbytes    = strlen(http_buffer);

    amp_warn(MOD_STR, "send %s", http_buffer);

    while (totalsend < nbytes) {
        send = ota_socket_send(sockfd, http_buffer + totalsend,
                               nbytes - totalsend);
        if (send == -1) {
            amp_warn(MOD_STR, "send error!%s", strerror(errno));
            ret = OTA_DOWNLOAD_REQ_FAIL;
            goto DOWNLOAD_END;
        }
        totalsend += send;
        amp_warn(MOD_STR, "%d bytes send OK!", totalsend);
    }

    memset(http_buffer, 0, OTA_BUFFER_MAX_SIZE);
    while ((nbytes = ota_socket_recv(sockfd, http_buffer,
                                     OTA_BUFFER_MAX_SIZE - 1)) != 0) {
        if (nbytes < 0) {
            amp_warn(MOD_STR, "ota_socket_recv nbytes < 0");
            if (errno != EINTR) {
                break;
            }
            if (ota_socket_check_conn(sockfd) < 0) {
                amp_warn(MOD_STR, "download system error %s", strerror(errno));
                break;
            } else {
                continue;
            }
        }

        if (!header_found) {
            if (!file_size) {
                char *ptr = strstr(http_buffer, "Content-Length:");
                if (ptr) {
                    sscanf(ptr, "%*[^ ]%d", &file_size);
                }
            }

            pos = strstr(http_buffer, "\r\n\r\n");
            if (!pos) {
                /* header pos */
                /* memcpy(headbuf, http_buffer, OTA_BUFFER_MAX_SIZE); */
            } else {
                pos += 4;
                int len      = pos - http_buffer;
                header_found = 1;
                size         = nbytes - len;
                // func((uint8_t *)pos, size);

                if (size == file_size) {
                    nbytes = 0;
                    break;
                }
                memset(http_buffer, 0, OTA_BUFFER_MAX_SIZE);
            }

            continue;
        }

        size += nbytes;
        amp_debug(MOD_STR, "receive data len: %d, total recvSize: %d, get fileSize: %d", nbytes, size, file_size);
        // func((uint8_t *)http_buffer, nbytes);

        if (size == file_size) {
            nbytes = 0;
            break;
        }

        memset(http_buffer, 0, OTA_BUFFER_MAX_SIZE);
    }

    amp_debug(MOD_STR, "file recv done: %d", nbytes);
    if (nbytes < 0) {
        amp_warn(MOD_STR, "download read error %s", strerror(errno));
        ret = OTA_DOWNLOAD_RECV_FAIL;
    } else if (nbytes == 0) {
        ret = OTA_SUCCESS;
    } else {
        /* can never reach here */
        /* ret = OTA_INIT_FAIL; */
    }

DOWNLOAD_END:
    amp_debug(MOD_STR, "DOWNLOAD_END, sockfd: %d ", sockfd);
    ota_socket_close(sockfd);
    amp_debug(MOD_STR, "ota_socket_close, sockfd: %d ", sockfd);
    amp_debug(MOD_STR, "http_buffer free :%p", http_buffer);
    amp_free(http_buffer);
    amp_free(url);
    amp_debug(MOD_STR, "http_buffer free done");
    return ret;
}


int apppack_download(char *url, download_js_cb_t func)
{
    int ret             = 0;
    int sockfd          = 0;
    int port            = 0;
    int nbytes          = 0;
    int send            = 0;
    int totalsend       = 0;
    uint32_t breakpoint = 0;
    int size            = 0;
    int header_found    = 0;
    char *pos           = 0;
    int file_size       = 0;
    char *host_file     = NULL;
    char *host_addr     = NULL;

    amp_warn(MOD_STR, "url = %s", url);

    if (!url || strlen(url) == 0 || func == NULL) {
        amp_warn(MOD_STR, "ota_download parms error!");
        bone_websocket_send_frame("/device/updateapp_reply", 201,
                                  "Bad Request");
        return OTA_DOWNLOAD_INIT_FAIL;
    }

    char *http_buffer = amp_malloc(OTA_BUFFER_MAX_SIZE);

    amp_warn(MOD_STR, "http_buffer = %p", http_buffer);
    http_gethost_info(url, &host_addr, &host_file, &port);

    if (host_file == NULL || host_addr == NULL) {
        ret = OTA_DOWNLOAD_INIT_FAIL;
        amp_free(http_buffer);
        bone_websocket_send_frame("/device/updateapp_reply", 201,
                                  "Bad Request");
        return ret;
    }

    amp_warn(MOD_STR, "ota_socket_connect  %d %s", port, host_addr);

    sockfd = ota_socket_connect(port, host_addr);
    if (sockfd < 0) {
        amp_warn(MOD_STR, "ota_socket_connect error");
        ret = OTA_DOWNLOAD_CON_FAIL;
        amp_free(http_buffer);
        bone_websocket_send_frame("/device/updateapp_reply", 205,
                                  "connect failed");
        return ret;
    }

    breakpoint = 0;
    sprintf(http_buffer, HTTP_HEADER, host_file, host_addr, port);

    send      = 0;
    totalsend = 0;
    nbytes    = strlen(http_buffer);

    amp_warn(MOD_STR, "send %s", http_buffer);

    while (totalsend < nbytes) {
        send = ota_socket_send(sockfd, http_buffer + totalsend,
                               nbytes - totalsend);
        if (send == -1) {
            amp_warn(MOD_STR, "send error!%s", strerror(errno));
            ret = OTA_DOWNLOAD_REQ_FAIL;
            bone_websocket_send_frame("/device/updateapp_reply", 205,
                                      "download failed");
            goto DOWNLOAD_END;
        }
        totalsend += send;
        amp_warn(MOD_STR, "%d bytes send OK!", totalsend);
    }

#ifdef LINUXOSX /* clean the root direcoty when user update app */
    be_osal_rmdir(JSE_FS_ROOT_DIR);
#else
    char root_dir[128] = {0};
    char script_dir[128] = {0};

    amp_get_user_dir(root_dir);
    snprintf(script_dir, 128, "%s/index.js", root_dir);
    amp_remove(script_dir);
#endif

    memset(http_buffer, 0, OTA_BUFFER_MAX_SIZE);
    while ((nbytes = ota_socket_recv(sockfd, http_buffer,
                                     OTA_BUFFER_MAX_SIZE - 1)) != 0) {
        if (nbytes < 0) {
            amp_warn(MOD_STR, "ota_socket_recv nbytes < 0");
            if (errno != EINTR) {
                break;
            }
            if (ota_socket_check_conn(sockfd) < 0) {
                amp_warn(MOD_STR, "download system error %s", strerror(errno));
                break;
            } else {
                continue;
            }
        }

        if (!header_found) {
            if (!file_size) {
                char *ptr = strstr(http_buffer, "Content-Length:");
                if (ptr) {
                    sscanf(ptr, "%*[^ ]%d", &file_size);
                }
            }

            pos = strstr(http_buffer, "\r\n\r\n");
            if (!pos) {
                /* header pos */
                /* memcpy(headbuf, http_buffer, OTA_BUFFER_MAX_SIZE); */
            } else {
                pos += 4;
                int len      = pos - http_buffer;
                header_found = 1;
                size         = nbytes - len;
                func((uint8_t *)pos, size);

                if (size == file_size) {
                    nbytes = 0;
                    break;
                }
                memset(http_buffer, 0, OTA_BUFFER_MAX_SIZE);
            }
            continue;
        }

        size += nbytes;
        amp_debug(MOD_STR, "receive data len: %d, total recvSize: %d, get fileSize: %d", nbytes, size, file_size);
        func((uint8_t *)http_buffer, nbytes);

        if (size == file_size) {
            nbytes = 0;
            break;
        }

        memset(http_buffer, 0, OTA_BUFFER_MAX_SIZE);
    }

    amp_debug(MOD_STR, "file recv done: %d", nbytes);
    if (nbytes < 0) {
        amp_warn(MOD_STR, "download read error %s", strerror(errno));
        ret = OTA_DOWNLOAD_RECV_FAIL;
    } else if (nbytes == 0) {
        ret = OTA_SUCCESS;
    } else {
        /* can never reach here */
        /* ret = OTA_INIT_FAIL; */
    }

DOWNLOAD_END:
    amp_debug(MOD_STR, "DOWNLOAD_END, sockfd: %d ", sockfd);
    ota_socket_close(sockfd);
    amp_debug(MOD_STR, "ota_socket_close, sockfd: %d ", sockfd);
    amp_debug(MOD_STR, "http_buffer free :%p", http_buffer);
    amp_free(http_buffer);
    amp_debug(MOD_STR, "http_buffer free done");
    return ret;
}

static int32_t update_done = 1;
static void *app_fp          = NULL;

int write_app_pack(const char *filename, int32_t file_size, int32_t type,
                   int32_t offset, uint8_t *buf, int32_t buf_len,
                   int32_t complete)
{
    /* char path[64]; */
    int ret;

    amp_debug(MOD_STR, "file_size=%d, offset = %d buf_len = %d complete = %d",
              file_size, offset, buf_len, complete);

    if (offset == 0) {
        app_fp = app_mgr_open_file(filename);
        amp_warn(MOD_STR, "app_mgr_open_file %s return  = %d", filename, app_fp);
    }

    if (app_fp != NULL) {
        if (buf_len > 0) {
            ret = amp_fwrite(buf, 1, buf_len, app_fp);
        }

        if ((offset + buf_len) == file_size) {
            ret    = amp_fsync(app_fp);
            ret    = amp_fclose(app_fp);
            app_fp = NULL;
            amp_warn(MOD_STR, "jse_close return %d", ret);
        }
    }

    if (complete != 0) {
        /* check failed */
        if (app_fp != NULL) {
            ret    = amp_fsync(app_fp);
            ret    = amp_fclose(app_fp);
            app_fp = NULL;
        }
        amp_warn(MOD_STR, "file verify %s", (complete == 1 ? "success" : "failed"));
        return -1;
    }

    return 0;
}

int download_apppack(uint8_t *buf, int32_t buf_len)
{
    amp_warn(MOD_STR, "download buf len = %d", buf_len);
    apppack_update(buf, buf_len);
    // amp_warn(MOD_STR, "apppack_post_process_state");
    // apppack_post_process_state();
    return 0;
}

void *be_jse_task_restart_entrance(void *data)
{
    amp_debug(MOD_STR, "[APPENGINE.D] appengine will restart ...");

    amp_task_main();

    amp_thread_delete(NULL);
    return NULL;    
}

extern void *task_mutex;
extern void *jse_task_exit_sem;
int upgrading;
static void *download_work(void *arg)
{
    int ret;
    void *restart_task;
    int restart_task_stack_used;
    amp_os_thread_param_t task_params = {0};

    // amp_warn(MOD_STR, "download_work task name=%s", jse_osal_get_taskname());
    amp_warn(MOD_STR, "url=%s", (char *)arg);

    ret = apppack_download((char *)arg, download_apppack);
    amp_debug(MOD_STR, "apppack_download done:%d", ret);
    apppack_final();
    update_done = 1;

    // amp_free(arg);

    if (ret == OTA_SUCCESS) {
        app_mgr_set_boneflag(1);
        amp_warn(MOD_STR, "Upgrade app success");
        bone_websocket_send_frame("/device/updateapp_reply", 200, "success");
        amp_msleep(200);
    }

    amp_warn(MOD_STR, "reboot ...");
    upgrading = 0;
    amp_task_exit_call(NULL, NULL);
    // jse_system_reboot();

    amp_warn(MOD_STR, "waiting jse taks exit completely\n");
    amp_semaphore_wait(jse_task_exit_sem, PLATFORM_WAIT_INFINITE);
    amp_msleep(200);
    amp_debug(MOD_STR, "amp restart task will create");
    task_params.priority = amp_get_default_task_priority();
    task_params.stack_size = JSENGINE_TASK_STACK_SIZE;
    task_params.name = "amp_restart";
    if(amp_thread_create(&restart_task, be_jse_task_restart_entrance, NULL, &task_params, &restart_task_stack_used) != 0)
    {
        amp_warn(MOD_STR, "jse osal task failed!");
        return NULL;
    }
    
    amp_thread_delete(NULL);

    return NULL;
}

void app_js_restart()
{
    void *restart_task;
    int restart_task_stack_used;
    amp_os_thread_param_t task_params = {0};
    amp_debug(MOD_STR, "amp restart task will create");
    task_params.priority = amp_get_default_task_priority();
    task_params.stack_size = JSENGINE_TASK_STACK_SIZE;
    task_params.name = "amp_restart";
    if(amp_thread_create(&restart_task, be_jse_task_restart_entrance, NULL, &task_params, &restart_task_stack_used) != 0)
    {
        amp_warn(MOD_STR, "jse osal task failed!");
    }
}

void app_js_stop()
{
    amp_task_exit_call(NULL, NULL);
    amp_warn(MOD_STR, "waiting jse taks exit completely\n");
    amp_semaphore_wait(jse_task_exit_sem, PLATFORM_WAIT_INFINITE);
    amp_msleep(200);
    amp_debug(MOD_STR, "amp task exit completely\n");
}

int apppack_upgrade(char *url)
{
    upgrading = 1;
    void *upgrade_task;
    int upgrade_stack_used;
    amp_os_thread_param_t task_params = {0};

    amp_warn(MOD_STR, "apppack_upgrade url=%s ", (char *)url);
    /* clear jsengine timer */
    timer_list_clear();
    amp_msleep(100);

    if (update_done) {
        update_done = 0;
        /* app_mgr_set_boneflag(0); */
        apppack_init(write_app_pack);

        amp_warn(MOD_STR, "create upgrade task ...");

        task_params.priority = amp_get_default_task_priority();
        task_params.stack_size = 1024 * 6;
        task_params.name = "amp_upgrade";
        if (amp_thread_create(&upgrade_task, download_work, (void *)url, &task_params, &upgrade_stack_used) != 0) {
            update_done = 1;
            apppack_final();
            amp_warn(MOD_STR, "jse_osal_task_new fail");
            bone_websocket_send_frame("/device/updateapp_reply", 203,
                                      "out of memory");
            return -1;
        }

    } else {
        amp_free(url);
        amp_warn(MOD_STR, "apppack upgrading...");
        bone_websocket_send_frame("/device/updateapp_reply", 204,
                                  "Busy,please try again later");
    }

    return 0;
}

int bone_websocket_send_frame(char* topic, int level, char* msg)
{
    return 0;
}

#ifdef LINUXOSX
static int upgrading_mutex   = 0;
static int upgrade_file_size = 0;

int upgrade_simulator_reply(uint8_t *buf, int32_t buf_len)
{
    char msg[64]            = {0};
    static int last_buf_len = 0;
    static int total_recv   = 0;

    total_recv += buf_len;
    sprintf(msg, "%d/%d", total_recv, upgrade_file_size);

    if (((total_recv - last_buf_len) > OTA_BUFFER_MAX_SIZE * 2) ||
        total_recv >= upgrade_file_size) {
        amp_debug(MOD_STR, "upgrade_simulator_reply lastbuf=%d %s", last_buf_len,
                  msg);
        if (total_recv == upgrade_file_size) {
            last_buf_len = 0;
            total_recv   = 0;
        } else {
            last_buf_len = total_recv;
        }
    }
}
static void upgrade_simulator_work(upgrade_image_param_t *arg)
{
    int ret;

    amp_warn(MOD_STR, "url=%s ,size=%d", (char *)arg->url, arg->file_size);
    ret = apppack_download((char *)arg->url, upgrade_simulator_reply);
    upgrading_mutex = 0;
    amp_free(arg);
    amp_free(arg->url);
    if (ret == OTA_DOWNLOAD_FINISH) {
        amp_warn(MOD_STR, "Upgrade app success");
        jse_osal_delay(200);
    }

    amp_warn(MOD_STR, "reboot ...");

    jse_system_reboot();
    jse_osal_exit_task(0);
}

int simulator_upgrade(upgrade_image_param_t *p_info)
{
    amp_warn(MOD_STR, "simulator_upgrade url=%s %d", p_info->url, p_info->file_size);
    if (upgrading_mutex == 0) {
        upgrading_mutex   = 1;
        upgrade_file_size = p_info->file_size;
        amp_warn(MOD_STR, "simulator_upgrade ...");

        if (jse_osal_new_task("simulator_upgrade", upgrade_simulator_work,
                             p_info, 1024 * 4, NULL) != 0) {
            amp_warn(MOD_STR, "jse_osal_task_new fail");
            upgrading_mutex = 0;
            return -1;
        }

    } else {
        amp_free(p_info);
        amp_warn(MOD_STR, "simulator_upgrading...");
    }

    return 0;
}
#endif

void app_mgr_set_boneflag(int enable)
{
    // jse_system_kv_set(BoneFlag, &enable, 4, 1);
}

int app_mgr_get_boneflag()
{
    int flag = 0;
    int len  = 4;
    // jse_system_kv_get(BoneFlag, &flag, &len);
    return flag;
}

/*
max length 192
*/

void app_mgr_set_devicespec(char *jsonstr)
{
    // jse_system_kv_set(DeviceSpec, jsonstr, strlen(jsonstr), 1);
}

int app_mgr_get_devicespec(char *jsonstr, int jsonstrlen)
{
    // jse_system_kv_get(DeviceSpec, jsonstr, &jsonstrlen);
    return jsonstrlen;
}

void * app_mgr_open_file(const char *targetname)
{
    void * fp;
    char path[256] = {0};
    char root_dir[128] = {0};

    amp_get_user_dir(root_dir);
    if (targetname == NULL) {
        return NULL;
    }
    snprintf(path, sizeof(path), "%s/", root_dir);
    if (targetname[0] == '.') {
        if (targetname[1] == '/') {
            strncat(path, targetname + 2, sizeof(path) - 1);
        } else {
            /* .aaa  not support hide file */
            return NULL;
        }
    } else {
        strncat(path, targetname, sizeof(path) - 1);
    }

    int i   = strlen(root_dir); /* 8 */
    int len = strlen(path);
    for (; i < len; i++) {
        if (path[i] == '/') {
            path[i] = 0;
            amp_mkdir(path);
            path[i] = '/';
        }
    }

    fp = amp_fopen(path, "w+");

    return fp;
}
