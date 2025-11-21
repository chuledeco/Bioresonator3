#include <stdio.h>
#include <esp_log.h>
#include <esp_err.h>
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include "webServer.h"
#include "main.h"
#include "i2s_es8311.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "webServer";

static bool uri_casei(const char *uri, const char *match_uri, size_t match_upto) {
    // compara sin mayúsc/minúsc — soporta match_upto (prefijos)
    size_t n = match_upto ? match_upto : strlen(match_uri);
    if (strncasecmp(uri, match_uri, n) != 0) return false;
    return match_upto ? true : (uri[n] == '\0');  // igualdad completa si no hay prefijo
}

esp_err_t record_uri_handler(httpd_req_t *req) {

    char query[64] = {0};
    int ret = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No valid querry provided : %d", ret);
        httpd_resp_send(req, "No valid length", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    query[63] = '\0';
    // ESP_LOGI(TAG, "Received query: %s", query);

    char length_str[5] = {0};
    if (httpd_query_key_value(query, "length", length_str, sizeof(length_str)) != ESP_OK) {
        ESP_LOGW(TAG, "Length not found in query %d", ret);
        httpd_resp_send(req, "Length not found", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    length_str[4] = '\0';
    // ESP_LOGI(TAG, "Requested length: %s", length_str);

    char *endptr;
    errno = 0;

    long val = strtol(length_str, &endptr, 10);

    if (errno != 0 || *endptr != '\0' || val <= 0 || val > 10) {
        ESP_LOGW(TAG, "Invalid length value: %s", length_str);
        httpd_resp_send(req, "Invalid length value", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    uint16_t length = (uint16_t)val;
    aquisition_length_seconds = length;

    char ans[32];
    snprintf(ans, sizeof(ans), "Recording %d seconds", length);
    httpd_resp_send(req, ans, HTTPD_RESP_USE_STRLEN);
    // ESP_LOGI(TAG, "Respuesta enviada");

    ESP_LOGI(TAG, "Trigger record via HTTP. Length: %d seconds", length);
    record = true;
    return ESP_OK;
}

esp_err_t getFile(httpd_req_t *req) {
    sending = true;
    char query[64] = {0};
    int ret = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No valid filename provided");
        httpd_resp_send(req, "No valid filename", HTTPD_RESP_USE_STRLEN);
        sending = false;
        return ESP_OK;
    }

    char filename[33] = {0};
    if (httpd_query_key_value(query, "filename", filename, sizeof(filename)) != ESP_OK) {
        ESP_LOGI(TAG, "Filename not found in query");
        httpd_resp_send(req, "Filename not found", HTTPD_RESP_USE_STRLEN);
        sending = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Requested filename: %s", filename);

    // Verifica extensión .bsc
    const char *ext = ".bsc";
    size_t len = strlen(filename);
    size_t ext_len = strlen(ext);
    if (len <= ext_len || strcmp(filename + len - ext_len, ext) != 0) {
        ESP_LOGI(TAG, "Invalid file extension");
        httpd_resp_send(req, "Invalid file extension", HTTPD_RESP_USE_STRLEN);
        sending = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "file extension OK");

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename);

    ESP_LOGI(TAG, "filepath: %s", filepath);
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        httpd_resp_send(req, "File does not exist", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "File doesn't exist");
        file = fopen(filepath, "wb");
        if (file) {
            const char *test_content = "Text file created for testing.\n";
            fwrite(test_content, 1, strlen(test_content), file);
            fclose(file);
            ESP_LOGI(TAG, "File created for testing");
        } else {
            ESP_LOGE(TAG, "Failed to create file");
        }
        sending = false;
        return ESP_OK;
    }

    // File exist, prepare answer
    ESP_LOGI(TAG, "File exist");
    char disp[96];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "close");

    ESP_LOGI(TAG, "Send chuncks");
    // Chunked answer (HTTP/1.1 chunked). We don't set Content-Length.
    uint8_t *buf = (uint8_t *)malloc(MAX_CHUNK_SIZE);
    if (!buf) {
        fclose(file);
        ESP_LOGE(TAG, "No memory for chunk buffer");
        httpd_resp_send_500(req);
        sending = false;
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    size_t nread;
    while ((nread = fread(buf, 1, MAX_CHUNK_SIZE, file)) > 0) {
        ESP_LOGI(TAG, "%d readed", nread);
        err = httpd_resp_send_chunk(req, (const char *)buf, nread);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "httpd_resp_send_chunk failed: %d", err);
            break;
        }
    }

    fclose(file);
    free(buf);

    ESP_LOGI(TAG, "Falta solo el de longitud 0");
    // Señalar fin de la respuesta chunked
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Envio chunck longitud 0");
        err = httpd_resp_send_chunk(req, NULL, 0);
    }

    sending = false;
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t get_samples(httpd_req_t *req) {
    sending = true;

    // Verifica que file_from_mic* y readed_samples sean válidos
    if (!file_from_mic || readed_samples <= 0 ||
        readed_samples > (I2S_SAMPLE_RATE * MAX_LENGTH_SECONDS)) {
        ESP_LOGE(TAG, "Invalid sample count or buffer");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid sample count");
        sending = false;
        return ESP_OK;
    }

    size_t total_bytes = readed_samples * sizeof(uint16_t);

    #ifdef TEST_WITH_TONE

    // Genera un tono de 1kHz en file_from_mic (uint16_t), sampleado a 44100Hz
    double freq = 1000.0;
    double sample_rate = 44100.0;
    double amplitude = 32767.0; // rango máximo para uint16_t
    for (int i = 0; i < readed_samples; ++i) {
        double t = (double)i / sample_rate;

        double val = amplitude * sin(2.0 * M_PI * freq * t);
        ((uint16_t *)file_from_mic)[i] = (uint16_t)(val + amplitude); // offset para evitar negativos
    }

    #endif

    ESP_LOGI(TAG, "Sending %d samples (%d bytes)", (int)readed_samples, (int)total_bytes);

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"samples.bsc\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "close");

    // Chunked transfer
    size_t sent = 0;
    esp_err_t err = ESP_OK;
    while (sent < total_bytes) {
        size_t chunk = (total_bytes - sent) > MAX_CHUNK_SIZE ? MAX_CHUNK_SIZE : (total_bytes - sent);
        err = httpd_resp_send_chunk(req, (const char *)((uint8_t *)file_from_mic + sent), chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "httpd_resp_send_chunk failed: %d", err);
            break;
        }
        sent += chunk;
    }

    // Señalar fin de la respuesta chunked
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }

    sending = false;
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

#define MAX_FILENAME_LEN   32
#define MAX_PATH_LEN       64
#define POST_BUF_SIZE      4096   // 1 KB por chunk

static esp_err_t postFile(httpd_req_t *req)
{
    sending = true;
    size_t total=0, used=0;
    esp_err_t info = esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS total=%u used=%u free=%u need=%u",
            (unsigned)total, (unsigned)used,
            (unsigned)(total - used),
            (unsigned)req->content_len);

    if (info != ESP_OK || (total - used) < req->content_len + 4096) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Not enough space");
        // opcional: sugerir borrar algo o usar otro filename
        sending = false;
        return ESP_OK;
    }

    // Get gilename from query
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
        sending = false;
        return ESP_OK;
    }

    char filename[MAX_FILENAME_LEN + 1] = {0};
    if (httpd_query_key_value(query, "filename", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'filename'");
        sending = false;
        return ESP_OK;
    }

    // Verify absence of /\ in filename
    for (char *p = filename; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
            sending = false;
            return ESP_OK;
        }
    }

    // Verify extension .bsc
    const char *ext = ".bsc";
    size_t len = strlen(filename), ext_len = strlen(ext);
    if (len <= ext_len || strcmp(filename + len - ext_len, ext) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid extension");
        sending = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Posting file: %s", filename);
    
    // Open the file and override if exists
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "/spiffs/%s", filename);

    unlink(path);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Open failed");
        sending = false;
        return ESP_OK;
    }
    ESP_LOGI(TAG, "File opened");

    // Read in blocks and save
    uint8_t *buf = (uint8_t *)malloc(POST_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "No enough memory for buffer");
        fclose(fp);
        httpd_resp_send_500(req);
        sending = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reserved memory for buffer");
    int accumulated = 0;
    size_t remaining = req->content_len;
    while (remaining > 0) {
        size_t to_read = remaining > POST_BUF_SIZE ? POST_BUF_SIZE : remaining;
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                // Retry on timeout
                continue;
            }
            // Error en recv
            free(buf);
            fclose(fp);
            ESP_LOGE(TAG, "Error receiving data");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv error");
            sending = false;
            return ESP_OK;
        }
        // Write to file
        size_t w = fwrite(buf, 1, r, fp);
        if (w != (size_t)r) {
            free(buf);
            fclose(fp);
            ESP_LOGE(TAG, "Write error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write error");
            sending = false;
            return ESP_OK;
        }
        accumulated += r;
        ESP_LOGI(TAG, "Chunk saved size: %d total siz: %d", r, accumulated);
        remaining -= r;
    }

    free(buf);
    fclose(fp);

    // OK response
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "OK");

    sending = false;
    return ESP_OK;
}

void ws_init() {
    
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.uri_match_fn = uri_casei;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t co_uri = {
            .uri = "/get_samples",
            .method = HTTP_GET,
            .handler = get_samples,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &co_uri);


        httpd_uri_t post_uri = {
            .uri      = "/postFile",
            .method   = HTTP_POST,
            .handler  = postFile,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &post_uri);

        httpd_uri_t record_uri = {
            .uri      = "/record",
            .method   = HTTP_GET,
            .handler  = record_uri_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &record_uri);

        ESP_LOGI(TAG, "HTTP server started");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}
