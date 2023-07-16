/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 * Modified by: Anjelica Benitez (2023)
 */

#include <string.h>
#include "tts_api.h"
#include <sys/param.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"
#include "app_audio.h"
#include "app_ui_ctrl.h"
#include "audio_player.h"
#include "settings.h"
#include "parser.h"
#include "esp_crt_bundle.h"
#include "inttypes.h"
#include "cJSON.h"

#define VOICE_ID CONFIG_VOICE_ID
#define VOLUME CONFIG_VOLUME_LEVEL
#define AUDIO_CONTENT_KEY "audioContent"

static const char *TAG = "tts_api";



/* Define a function to handle HTTP events during an HTTP request */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static char * output_buffer;
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            file_total_len = 0;
            int copy_len = 0;
            output_buffer = (char *) malloc(MAX_FILE_SIZE);
            if (output_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                return ESP_FAIL;
            }
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=(%"PRIu32" + %d) [%d]", file_total_len, evt->data_len, MAX_FILE_SIZE);
              // Buffer to store response of http request from event handler
            static int output_len;
            copy_len = 0;   // Stores len of current chunk
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {

                // Get len of response, so we know what size to assign to output buffer
                const int buffer_len = esp_http_client_get_content_length(evt->client);
                if (output_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                    return ESP_FAIL;
                }
                copy_len = MIN(evt->data_len, (buffer_len - file_total_len));
                if (copy_len) {
                    memcpy(output_buffer + file_total_len, evt->data, copy_len);
                }
                file_total_len += copy_len;
            }
            else {

                // Get length of incoming chunk data and copy into buffer until the whole response has been stored
                copy_len = MIN(evt->data_len, (MAX_FILE_SIZE - file_total_len));
                if (copy_len) {
                    // Append data from current chunk to output buffer
                    memcpy(output_buffer + file_total_len, evt->data, copy_len);
                }
                // Update current len of buffer
                file_total_len += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH:%"PRIu32", %"PRIu32" K", file_total_len, file_total_len / 1024);
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
//            ESP_LOG_BUFFER_HEX(TAG, output_buffer, file_total_len);
            if (!output_buffer) {
                ESP_LOGE(TAG, "Nothing in response buffer");
                return ESP_FAIL;
            }

            cJSON *json_response = cJSON_Parse(output_buffer);
            if (json_response == NULL) {
                ESP_LOGE(TAG, "Error parsing JSON");
                return ESP_FAIL;
            }

            cJSON *audio_content = cJSON_GetObjectItem(json_response, AUDIO_CONTENT_KEY);
            if (audio_content == NULL) ESP_LOGE(TAG, "'%s' key not found", AUDIO_CONTENT_KEY);
            else {
                file_total_len = strlen(audio_content->valuestring);

                size_t output_length;
                mbedtls_base64_decode(audio_rx_buffer, MAX_FILE_SIZE, &output_length,
                                      (unsigned char *) audio_content->valuestring, file_total_len);
                audio_player_play(audio_rx_buffer, output_length);
            }
            cJSON_Delete(json_response);

            free(output_buffer);
            output_buffer = NULL;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

/* Decode 2 Hex */

char dec2hex(short int c)
{
    if (0 <= c && c <= 9) {
        return c + '0';
    } else if (10 <= c && c <= 15) {
        return c + 'A' - 10;
    } else {
        return -1;
    }
}

/* Encode URL for playing sound */

void url_encode(const char *url, char *encode_out)
{
    int i = 0;
    int len = strlen(url);
    int res_len = 0;

    assert(encode_out);

    for (i = 0; i < len; ++i) {
        char c = url[i];
        char n = url[i + 1];
        if (c == '\\' && n == 'n') {
            i += 1;
            continue;
        } else if (('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '/' || c == '.') {
            encode_out[res_len++] = c;
        } else {
            int j = (short int)c;
            if (j < 0) {
                j += 256;
            }
            int i1, i0;
            i1 = j / 16;
            i0 = j - i1 * 16;
            encode_out[res_len++] = '%';
            encode_out[res_len++] = dec2hex(i1);
            encode_out[res_len++] = dec2hex(i0);
        }
    }
    encode_out[res_len] = '\0';
}

/* Create Text to Speech request */
static cJSON * create_google_tts_payload(char * chatgpt_resp, char * encoding) {

    const char *language_code = "en-US";

    const char *name = "en-US-Wavenet-J";
    const char *ssml_gender = "MALE";

    const char *audio_encoding = encoding;

    const float speaking_rate = 0.87;
    const int pitch = -10;

    cJSON *payload = cJSON_CreateObject();

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "text", chatgpt_resp);
    cJSON_AddItemToObject(payload, "input", input);

    cJSON *voice = cJSON_CreateObject();
    cJSON_AddStringToObject(voice, "languageCode", language_code);
    cJSON_AddStringToObject(voice, "name", name);
    cJSON_AddStringToObject(voice, "ssmlGender", ssml_gender);
    cJSON_AddItemToObject(payload, "voice", voice);

    cJSON *audio_config = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_config, "audioEncoding", audio_encoding);
    cJSON_AddNumberToObject(audio_config, "speakingRate", speaking_rate);
    cJSON_AddNumberToObject(audio_config, "pitch", pitch);
    cJSON_AddItemToObject(payload, "audioConfig", audio_config);

    return payload;
}

esp_err_t text_to_speech_request(const char * message, AUDIO_CODECS_FORMAT code_format)
{
    sys_param_t * sys_param = settings_get_parameter();

    char * audio_encoding;
    char * formatted_message = groom_chatgpt_response(message);

    if (AUDIO_CODECS_MP3 == code_format) {
        audio_encoding = "MP3";
    } else {
        audio_encoding = "WAV";
    }

    cJSON * payload_object = create_google_tts_payload(formatted_message, audio_encoding);
    char * payload = cJSON_PrintUnformatted(payload_object);

    int url_size = snprintf(NULL, 0, "https://texttospeech.googleapis.com/v1/text:synthesize?key=%s", sys_param->google_tts_key);
    // Allocate memory for the URL buffer
    char * url = heap_caps_malloc((url_size + 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (url == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for URL");
        return ESP_ERR_NO_MEM;
    }
    snprintf(url, url_size + 1, "https://texttospeech.googleapis.com/v1/text:synthesize?key=%s", sys_param->google_tts_key);

    esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .event_handler = http_event_handler,
            .buffer_size = 128000,
            .buffer_size_tx = 4000,
            .timeout_ms = 40000,
            .crt_bundle_attach = esp_crt_bundle_attach,
    };

    uint32_t starttime = esp_log_timestamp();
    ESP_LOGE(TAG, "[Start] create_TTS_request, timestamp:%"PRIu32, starttime);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    ESP_LOGE(TAG, "[End] create_TTS_request, + offset:%"PRIu32, esp_log_timestamp() - starttime);

    heap_caps_free(url);
    heap_caps_free(formatted_message);
    cJSON_Delete(payload_object);
    esp_http_client_cleanup(client);
    return err;
}
