//
// Created by abenitez on 7/15/23.
//
#include <string.h>
#include "parser.h"
#include "esp_http_client.h"


char * groom_chatgpt_response(const char * text) {
    if (NULL == text) {
        return "";
    }

    char * groomed_text = heap_caps_malloc((strlen(text) + 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(groomed_text);

    int j = 0;
    for (int i = 0; i < strlen(text);) {
        if ((*(text + i) == '\\') && ((i + 1) < strlen(text)) && (*(text + i + 1) == 'n')) {
            *(groomed_text + j++) = '\n';
            i += 2;
        } else {
            *(groomed_text + j++) = *(text + i);
            i += 1;
        }
    }
    *(groomed_text + j) = '\0';

    return groomed_text;
}