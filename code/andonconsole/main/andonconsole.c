/* MIT License

Project: Wireless Reconfigurable Andon System with Maintenance Prediction
  * For the module EN2160: Electronic Design Realisation at University of 
    Moratuwa, Sri Lanka


Parts of the code taken from following repositories:

- Boris Lovosevic: TFT Display Code
  Copyright (c) 2017 Boris Lovosevic

- Jeremy Huffman: TFT Display Code - CMake suppport
  Copyright (c) 2021 Jeremy Huffman

- Luis D. Gomez: TFT Controller Intergration
  Copyright (c) 2020 Luis Diego Gomez

- Espressif Systems (Shanghai): WPS connection, Websockets, HTTP GET
  Copyright (c) 2021 Espressif Systems (Shanghai)

- Fateh Ali: WiFi Event Handler
  Copyright (c) 2023 Fateh Ali Sharukh Khan

- Josh Marangoni: Baremetal ESP32 GPIO
  Copyright (c) 2023 Josh Marangoni

Main code and modifications by Isuranga Senavirathne and Yasith Silva
Copyright (c) 2024 Isuranga Senavirathne, Yasith Silva


Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to 
deal in the Software without restriction, including without limitation the 
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or 
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
DEALINGS IN THE SOFTWARE. */


#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_event.h"
#include "nvs_flash.h"
// #include "esp_http_client.h"
//#include "esp_netif.h"
#include "esp_websocket_client.h"
#include "tft.h"
#include "tft_controller.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "mdns.h" 
#include "soc/gpio_struct.h"



// -----------------------------------------------------------
//    Configuring the registers
// ----------------------------------------------------------- 

// Define pins
#define CS    5         // Display SPI
#define DC    17
#define RST   22
#define MOSI  23
#define SCLK  18
#define BKLT  19

#define UP    21        // Navigation
#define DOWN  13
#define SELOK 14

#define CALL1 33        // Calls/Indicators
#define CALL2 34
#define CALL3 35
#define IND1  25
#define IND2  26
#define IND3  27

#define DEBUG 2        // Lights up to ESP_LOGs

// Define registers
#define GPIO_OUT_W1TS_REG 0x3FF44008
#define GPIO_OUT_W1TC_REG 0x3FF4400C
#define GPIO_ENABLE_REG   0x3FF44020
#define GPIO_IN_REG       0x3FF4403C
#define GPIO_IN1_REG      0x3FF44040

// Enable registers
volatile uint32_t* gpio_enable_reg   = (volatile uint32_t*) GPIO_ENABLE_REG;
// Output register
volatile uint32_t* gpio_out_w1ts_reg = (volatile uint32_t*) GPIO_OUT_W1TS_REG;
volatile uint32_t* gpio_out_w1tc_reg = (volatile uint32_t*) GPIO_OUT_W1TC_REG;
// Input register
volatile uint32_t* gpio_in_reg       = (volatile uint32_t*) GPIO_IN_REG;
volatile uint32_t* gpio_in1_reg      = (volatile uint32_t*) GPIO_IN1_REG;

void gpio_setup() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE; // Disable interrupt
    io_conf.mode = GPIO_MODE_INPUT;        // Set as input mode
    io_conf.pin_bit_mask = (1ULL << UP) | (1ULL << DOWN) | (1ULL << SELOK) | (1ULL << CALL1) | (1ULL << CALL2) | (1ULL << CALL3);    
    io_conf.pull_down_en = 0;              // Disable pull-down mode
    io_conf.pull_up_en = 0;                // Disable pull-up mode
    gpio_config(&io_conf);

    // Inputs left 0, outputs set to 1
    *gpio_enable_reg   |= (1<<IND1) | (1<<IND2) | (1<<IND3) | (1<<BKLT) | (1<<DEBUG);
    *gpio_out_w1tc_reg |= (1 << IND1) | (1 << IND2) | (1 << IND3) | (1<<DEBUG); // Initialises to 0
}



// -----------------------------------------------------------
//    Setting up calls and departments
// ----------------------------------------------------------- 
#define MACHINE_ID 500

// console ID to be defined in building
static const char *TAG_CODE = "Code";

#define MAX_CALL_RECORDS 50 // Adjust as needed
#define MAX_DEPT_RECORDS 50 // Adjust as needed

struct Callrecord {
    char *status;
    char *mancalldesc;
    char *mancallto;
};

struct Deptrecord {
    char *deptname;
    int deptid;
};

struct Deptrecord department = {NULL, NULL};
struct Callrecord calls[3] = {
    {NULL,NULL,NULL},
    {NULL,NULL,NULL},
    {NULL,NULL,NULL}
};

// Set Callrecord
void setCallrecord(struct Callrecord *record, const char *status, const char *desc, const char *to) {
    if (record->status != NULL) free(record->status);
    if (record->mancalldesc != NULL) free(record->mancalldesc);
    if (record->mancallto != NULL) free(record->mancallto);

    // Allocate new memory and copy values
    record->status = malloc(strlen(status) + 1);
    record->mancalldesc = malloc(strlen(desc) + 1);
    record->mancallto = malloc(strlen(to) + 1);

    if (record->status != NULL) strcpy(record->status, status);
    if (record->mancalldesc != NULL) strcpy(record->mancalldesc, desc);
    if (record->mancallto != NULL) strcpy(record->mancallto, to);
}

// Set Deptrecord
void setDeptrecord(struct Deptrecord *record, const char *dept, int id) {
    if (record->deptname != NULL) free (record->deptname);

    record->deptname = malloc(strlen(dept) + 1);
    if (record->deptname != NULL) {
        strcpy(record->deptname, dept);
    }
    record->deptid = id;
}

struct Callrecord callRecords[MAX_CALL_RECORDS];
int callRecordCount = 0;

struct Deptrecord deptRecords[MAX_DEPT_RECORDS];
int deptRecordCount = 0;

// Initialising callRecords to be NULL
void initialiseCallRecord() {
    for (int i = 0; i < MAX_CALL_RECORDS; i++) {
        callRecords[i].status = NULL;
        callRecords[i].mancalldesc = NULL;
        callRecords[i].mancallto = NULL;
    }
    callRecordCount = 0;  // Initialize count
}

// For testing purposes, comment when in operation
void testCallRecords() {
    // Test call 1
    callRecords[0].status = "Red";
    callRecords[0].mancalldesc = "Test Problem1";
    callRecords[0].mancallto = "Test person1";

    // Test call 2
    callRecords[1].status = "Yellow";
    callRecords[1].mancalldesc = "Test Problem2";
    callRecords[1].mancallto = "Test person2";

    // Test call 3
    callRecords[2].status = "Green";
    callRecords[2].mancalldesc = "Test Problem3";
    callRecords[2].mancallto = "Test person3";

    callRecordCount = 3;
}



// --------------------------------------------------------
//    Functions for WiFi 
// -------------------------------------------------------- 

static const char *TAG_WIFI = "WiFi";
static const char *TAG_SOCK = "WebSocket";

// Set either credentials if you're demonstrating with a mobile hotspot,
// or comment all that and uncomment the functions where it's set in WPS

#define WIFI_SSID       "Isuranga's Galaxy A72"
#define WIFI_PASS       "xbai9431"
#define websocket_uri   "ws://192.168.79.53:8080"

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// Event handler for Wifi events
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG_WIFI, "retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_WIFI, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize Wi-Fi
void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG_WIFI, "wifi_init_sta finished.");
}

// Connect Websocket server
/*
static char* resolve_mdns_host() {
    ESP_LOGI(TAG_SOCK, "Query websocket server");

    struct ip4_addr addr;
    addr.addr=0;

    esp_err_t err = mdns_query_a("AndonESP-Backend", 2000, &addr);
    if (err) {
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG_SOCK, "Server not found");
        } else {
            ESP_LOGW(TAG_SOCK, "mDNS Query failed");
        }
        return NULL;
    }
    ESP_LOGI(TAG_SOCK, "Query A: %s.local resolved to: " IPSTR, "AndonESP-Backend", IP2STR(&addr));
    
    char *websocket_uri = malloc(50);
    if (websocket_uri == NULL) {
        ESP_LOGE(TAG_CODE, "Failed to allocate memory to IP");
        return NULL;
    }

    snprintf(websocket_uri, 50, "wss://" IPSTR ":443/", IP2STR(&addr));
    return websocket_uri;
}*/

// WebSocket event handler
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG_SOCK, "WebSocket Connected");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_SOCK, "WebSocket Disconnected");
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(TAG_SOCK, "Received data length: %d", data->data_len);
            ESP_LOGI(TAG_SOCK, "Received data: %.*s", data->data_len, (char*)data->data_ptr);
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG_SOCK, "WebSocket Error");
            break;
    }
}

// Task to send data periodically
void send_data_task(esp_websocket_client_handle_t client)
{    
    if (esp_websocket_client_is_connected(client)) {
        // Check inputs
        bool call1 = REG_READ(gpio_in1_reg)&(1 << (CALL1-32));
        bool call2 = REG_READ(gpio_in1_reg)&(1 << (CALL2-32));
        bool call3 = REG_READ(gpio_in1_reg)&(1 << (CALL3-32));

        // Create a JSON object
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "consoleid", MACHINE_ID); // Define ConsoleID
        cJSON_AddNumberToObject(json, "department", department.deptid ? department.deptid : -1);
        cJSON_AddStringToObject(json, "call1", call1 ? (calls[0].status ? calls[0].status : "Undefined") : "");
        cJSON_AddStringToObject(json, "call2", call2 ? (calls[1].status ? calls[1].status : "Undefined") : "");
        cJSON_AddStringToObject(json, "call3", call3 ? (calls[2].status ? calls[2].status : "Undefined") : "");
        cJSON_AddStringToObject(json, "oldcall", "");

        // Convert JSON object to string
        char *json_string = cJSON_PrintUnformatted(json);

        // Send JSON string
        esp_websocket_client_send_text(client, json_string, strlen(json_string), portMAX_DELAY);
        ESP_LOGI(TAG_SOCK, "Sent data: %s", json_string);

        // Free JSON string and object
        free(json_string);
        cJSON_Delete(json);

        if (call1) {
            *gpio_out_w1ts_reg |= (1<<IND1);
            ESP_LOGI(TAG_CODE, "Call1");
        } 

        if (call2) {
            *gpio_out_w1ts_reg |= (1<<IND2);
            ESP_LOGI(TAG_CODE, "Call2");
        }

        if (call3) {
            *gpio_out_w1ts_reg |= (1<<IND3);
            ESP_LOGI(TAG_CODE, "Call3");
        }

        if (!call1 & !call2 & !call3) {
            *gpio_out_w1tc_reg |= (1<<IND1) | (1<<IND2) | (1<<IND3);
        }
    } else {
        ESP_LOGW(TAG_SOCK, "WebSocket client is not connected");
    }
}

// Function to initialize and start WebSocket client
void websocket_app_start(void)
{
    esp_websocket_client_config_t websocket_cfg = {
        .uri = websocket_uri,  // Replace with your WebSocket server address
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);

    // Register the WebSocket event handler
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
}



// -----------------------------------------------------------
//    Functions for HTTP download 
// ----------------------------------------------------------- 

/*
// Getting calls and departments via http
// Event handler
static const char *TAG_HTTP = "http";

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG_HTTP, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", 
                   evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                strncat(evt->user_data, (char*)evt->data, evt->data_len);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

// Get the calls
void fetchCallRecords() {
    if (esp_wifi_get_state() == WIFI_STATE_CONNECTED) {
        char buffer[2048] = {0};
        
        esp_http_client_config_t config = {
            .url = "http://localhost:3002/getCalls",
            .event_handler = _http_event_handler,
            .user_data = buffer,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            int httpResponseCode = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG_HTTP,"HTTP Response code: %d", httpResponseCode);
            ESP_LOGI(TAG_HTTP,"HTTP Response payload: %s", buffer);

            cJSON *json = cJSON_Parse(buffer);
            if (json == NULL) {
                ESP_LOGE(TAG_HTTP, "Error parsing JSON");
                esp_http_client_cleanup(client);
                return;
            }

            callRecordCount = 0;
            cJSON *record;
            cJSON_ArrayForEach(record, json) {
                if (callRecordCount < MAX_CALL_RECORDS) {
                    cJSON *status = cJSON_GetObjectItem(record, "status");
                    cJSON *mancalldesc = cJSON_GetObjectItem(record, 
                                                            "mancalldesc");
                    cJSON *mancallto = cJSON_GetObjectItem(record, 
                                                            "mancallto");
                    if (cJSON_IsString(status) && cJSON_IsString
                    (mancalldesc) && cJSON_IsString(mancallto)) {
                        strncpy(callRecords[callRecordCount].status, 
                        status->valuestring, 
                        sizeof(callRecords[callRecordCount].status) - 1);

                        strncpy(callRecords[callRecordCount].mancalldesc, 
                        mancalldesc->valuestring, 
                        sizeof(
                            callRecords[callRecordCount].mancalldesc) - 1);
                        
                        strncpy(callRecords[callRecordCount].mancallto, 
                        mancallto->valuestring, 
                        sizeof(callRecords[callRecordCount].mancallto) - 1);
                        callRecordCount++;
                    }
                }
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG_HTTP,"HTTP GET request failed: %s", 
                     esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }
}

// Get the departments
void fetchDeptRecords() {
    if (esp_wifi_get_state() == WIFI_STATE_CONNECTED) {
        char buffer[2048] = {0};
        
        esp_http_client_config_t config = {
            .url = "http://localhost:3002/getUsers",
            .event_handler = _http_event_handler,
            .user_data = buffer,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            int httpResponseCode = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG_HTTP,"HTTP Response code: %d", httpResponseCode);
            ESP_LOGI(TAG_HTTP,"HTTP Response payload: %s", buffer);

            cJSON *json = cJSON_Parse(buffer);
            if (json == NULL) {
                ESP_LOGE(TAG_HTTP,"Error parsing JSON");
                esp_http_client_cleanup(client);
                return;
            }

            deptRecordCount = 0;
            cJSON *record;
            cJSON_ArrayForEach(record, json) {
                if (deptRecordCount < MAX_DEPT_RECORDS) {
                    cJSON *deptname = cJSON_GetObjectItem(record, 
                                                          "deptname");
                    cJSON *deptid = cJSON_GetObjectItem(record, "deptid");
                    if (cJSON_IsString(deptname) && cJSON_IsNumber(deptid)) 
                    {
                        strncpy(deptRecords[deptRecordCount].deptname, 
                                deptname->valuestring, 
                                sizeof(
                                deptRecords[deptRecordCount].deptname) - 1);
                        
                        deptRecords[deptRecordCount].deptid = 
                            deptid->valueint;
                        deptRecordCount++;
                    }
                }
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG_HTTP, "HTTP GET request failed: %s\n", 
                     esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }
}
*/



// --------------------------------------------------------
//    Functions for displaying
// -------------------------------------------------------- 
static const char *TAG_DISP = "Display";

int spacing         = 2;  // Linespacing for the display
int default_spacing = 2;
int refresh_rate    = 500;
int highlight_padding = 3;

// Function to display
void disp_write(const char *distring, int x, int line, bool highlight) {
    int y = 10*(line-1)*spacing+25;
    TFT_setFont(DEFAULT_FONT, NULL);

    if (!highlight) {
        // -- -- Prints onto display -- --
        TFT_fillRect(x-highlight_padding, y-highlight_padding, tft_width-2*(x-highlight_padding), TFT_getfontheight()+2*highlight_padding, TFT_BLACK);
        tft_fg = TFT_WHITE;
        tft_bg = TFT_BLACK;
        TFT_print(distring,x,y);
    } else {
        // If back has to be highlighted
        TFT_fillRect(x-highlight_padding, y-highlight_padding, tft_width-2*(x-highlight_padding), TFT_getfontheight()+2*highlight_padding, TFT_WHITE);
        tft_fg = TFT_BLACK;
        tft_bg = TFT_WHITE;
        TFT_print(distring,x,y);
        tft_fg = TFT_WHITE;
        tft_bg = TFT_BLACK;
    }    
}

// Function to clear screen
void disp_cls() {
    TFT_fillScreen(TFT_BLACK);
}

// Open animation
void disp_start() {
    disp_write("Andon", 5, 2, false);
    vTaskDelay(pdMS_TO_TICKS(5000));
    disp_cls();
    ESP_LOGI(TAG_DISP,"Start - Display Started");
}


// --------------------------------------------------------
//    Functions for Menu 
// --------------------------------------------------------
// 
// The main menu is structured as below:
// 
// Main Menu
//   |___ Settings
//          |____ Choose Calls
//          |       |___ Choose Call 1
//          |       |___ Choose Call 2
//          |       |___ Choose Call 3
//          |____ Set Department
// 
// -------------------------------------------------------- 

// Checks for the buttons pressed
int checkButtonPress() {
    if (REG_READ(gpio_in_reg) & (1 << UP)) {
        return 1;
    } else if (REG_READ(gpio_in_reg) & (1 << SELOK)) {
        return 2;
    } else if (REG_READ(gpio_in_reg) & (1 << DOWN)) {
        return 3;
    } else {
        return 0;
    }
}

// Displaying Menu for choosing calls
void showChooseCalls(int menu_item) {
    int button = 0;
    if (callRecordCount == 0) {
        disp_write("No calls set", 5, 1, false);
        disp_write("Please set on mgmt console", 5, 2, false);
        disp_write("Go back", 5, 3, true);

    } else {
        if ((callRecordCount%4 == 3)&&(menu_item>callRecordCount-3)) {
            if ((menu_item-(callRecordCount-3))%3 == 1) {
                disp_write(callRecords[menu_item].mancalldesc, 5, 2, false);
                disp_write(callRecords[menu_item+1].mancalldesc, 5, 3, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 1, true);
            } else if ((menu_item-(callRecordCount-3))%3 == 2) {
                disp_write(callRecords[menu_item-2].mancalldesc, 5, 1, false);
                disp_write(callRecords[menu_item].mancalldesc, 5, 3, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 2, true);
            } else {
                disp_write(callRecords[menu_item-3].mancalldesc, 5, 1, false);
                disp_write(callRecords[menu_item-2].mancalldesc, 5, 2, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 3, true);
            }
        } else if ((callRecordCount%4 == 2)&&(menu_item>callRecordCount-2)) {
            if ((menu_item - (callRecordCount-2))%2 == 1) {
                disp_write(callRecords[menu_item].mancalldesc, 5, 2, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 1, true);
            } else {
                disp_write(callRecords[menu_item-2].mancalldesc, 5, 1, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 2, true);
            }
        } else if ((callRecordCount%4 == 1)&&(menu_item>callRecordCount-1)) {
            disp_write(callRecords[menu_item-1].mancalldesc, 5, 1, true);
        } else {
            if (menu_item%4 == 1) {
                disp_write(callRecords[menu_item].mancalldesc, 5, 2, false);
                disp_write(callRecords[menu_item+1].mancalldesc, 5, 3, false);
                disp_write(callRecords[menu_item+2].mancalldesc, 5, 4, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 1, true);
            } else if (menu_item%4 == 2) {
                disp_write(callRecords[menu_item-2].mancalldesc, 5, 1, false);
                disp_write(callRecords[menu_item].mancalldesc, 5, 3, false);
                disp_write(callRecords[menu_item+1].mancalldesc, 5, 4, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 2, true);
            } else if (menu_item%4 == 3) {
                disp_write(callRecords[menu_item-3].mancalldesc, 5, 1, false);
                disp_write(callRecords[menu_item-2].mancalldesc, 5, 2, false);
                disp_write(callRecords[menu_item].mancalldesc, 5, 4, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 3, true);
            } else {
                disp_write(callRecords[menu_item-4].mancalldesc, 5, 1, false);
                disp_write(callRecords[menu_item-3].mancalldesc, 5, 2, false);
                disp_write(callRecords[menu_item-2].mancalldesc, 5, 3, false);
                disp_write(callRecords[menu_item-1].mancalldesc, 5, 4, true);
            }
        }
    }
}

// Menu for choosing calls
void chooseCalls(int callIndex) {
    //fetchCallRecords();
     
    int menu_item = 1;
    int button = 0;
    showChooseCalls(menu_item);

    while (true) {
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item == 1) ? callRecordCount : menu_item-1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showChooseCalls(menu_item);

        } else if (button==3) {           // Decrement
            menu_item = (menu_item == callRecordCount) ? 1 : menu_item+1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showChooseCalls(menu_item);

        } else if (button==2) {
            setCallrecord(&calls[callIndex], callRecords[menu_item-1].status, callRecords[menu_item-1].mancalldesc, callRecords[menu_item-1].mancallto);
            disp_cls();
            break;
        }
    }
}

// Displaying menu for choosing the specific call
void showSetCalls(int menu_item) {
    char *call1MenuText = (calls[0].status) ? calls[0].mancalldesc : "Choose Call 1";
    char *call2MenuText = (calls[1].status) ? calls[1].mancalldesc : "Choose Call 2";
    char *call3MenuText = (calls[2].status) ? calls[2].mancalldesc : "Choose Call 3";

    if (menu_item==1) {
        disp_write(call2MenuText, 5, 2, false);
        disp_write(call3MenuText, 5, 3, false);
        disp_write("Back", 5, 4, false);

        disp_write(call1MenuText, 5, 1, true);
    
    } else if (menu_item==2) {
        disp_write(call1MenuText, 5, 1, false);
        disp_write(call3MenuText, 5, 3, false);
        disp_write("Back", 5, 4, false);
        
        disp_write(call2MenuText, 5, 2, true);

    } else if (menu_item==3) {
        disp_write(call1MenuText, 5, 1, false);
        disp_write(call2MenuText, 5, 2, false);
        disp_write("Back", 5, 4, false);
        
        disp_write(call3MenuText, 5, 3, true);

    } else {
        disp_write(call1MenuText, 5, 1, false);
        disp_write(call2MenuText, 5, 2, false);
        disp_write(call3MenuText, 5, 3, false);

        disp_write("Back", 5, 4, true);
    }    
}

// Menu for setting calls
void setCalls() {
    int menu_item = 1;
    int button = 0;
    showSetCalls(menu_item);

    while (true) {
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item==1) ? 4 : menu_item-1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showSetCalls(menu_item);

        } else if (button==3) {           // Decrement
            menu_item = (menu_item==4) ? 1 : menu_item+1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showSetCalls(menu_item);

        } else if (button==2) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (menu_item==1) {
                disp_cls();
                chooseCalls(0);
                showSetCalls(menu_item);
                vTaskDelay(pdMS_TO_TICKS(500));

            } else if (menu_item==2) {
                disp_cls();
                chooseCalls(1);
                showSetCalls(menu_item);
                vTaskDelay(pdMS_TO_TICKS(500));

            } else if (menu_item==3) {
                disp_cls();
                chooseCalls(2);
                showSetCalls(menu_item);
                vTaskDelay(pdMS_TO_TICKS(500));

            } else {
                disp_cls();
                break;
            }
        }
    }
    ESP_LOGI(TAG_DISP, "Exiting SetCalls()");
}

// Displaying menu choosing the department
void showSetDepartment(int menu_item) {
    int button = 0;
    if (deptRecordCount == 0) {
        disp_write("No departments set", 5, 1, false);
        disp_write("Please set on mgmt console", 5, 2, false);
        disp_write("Go back", 5, 3, true);

    } else {
        if (menu_item+3 <= deptRecordCount-1) {
            if (menu_item%4 == 1) {
                disp_write(deptRecords[menu_item+1].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+2].deptname, 5, 3, false);
                disp_write(deptRecords[menu_item+3].deptname, 5, 4, false);
                disp_write(deptRecords[menu_item].deptname, 5, 1, true);
            } else if (menu_item%4 == 2) {
                disp_write(deptRecords[menu_item].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+2].deptname, 5, 3, false);
                disp_write(deptRecords[menu_item+3].deptname, 5, 4, false);
                disp_write(deptRecords[menu_item+1].deptname, 5, 1, true);
            } else if (menu_item%4 == 3) {
                disp_write(deptRecords[menu_item].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+1].deptname, 5, 3, false);
                disp_write(deptRecords[menu_item+3].deptname, 5, 4, false);
                disp_write(deptRecords[menu_item+2].deptname, 5, 1, true);
            } else {
                disp_write(deptRecords[menu_item].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+1].deptname, 5, 3, false);
                disp_write(deptRecords[menu_item+2].deptname, 5, 4, false);
                disp_write(deptRecords[menu_item+3].deptname, 5, 1, true);
            }
            
        } else if (menu_item+2 <= deptRecordCount-1) {
            if (menu_item%4 == 1) {
                disp_write(deptRecords[menu_item+1].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+2].deptname, 5, 3, false);
                disp_write(deptRecords[menu_item].deptname, 5, 1, true);
            } else if (menu_item%4 == 2) {
                disp_write(deptRecords[menu_item].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+2].deptname, 5, 3, false);
                disp_write(deptRecords[menu_item+1].deptname, 5, 1, true);
            } else {
                disp_write(deptRecords[menu_item].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+1].deptname, 5, 3, false);
                disp_write(deptRecords[menu_item+2].deptname, 5, 1, true);
            }
    
        } else if (menu_item+1 <= deptRecordCount-1) {
            if (menu_item%4 == 1) {
                disp_write(deptRecords[menu_item+1].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item].deptname, 5, 1, true);
            } else if (menu_item%4 == 2) {
                disp_write(deptRecords[menu_item].deptname, 5, 2, false);
                disp_write(deptRecords[menu_item+1].deptname, 5, 1, true);
            }
            
        } else {
            disp_write(deptRecords[menu_item].deptname, 5, 1, true);
        }
    }
}

// Menu choosing the department
void setDepartment() {
    //fetchDeptRecords();

    int menu_item = 1;
    int button = 0;
    showSetDepartment(menu_item);

    while (true) {
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item <= 0) ? 1 : menu_item-1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showSetDepartment(menu_item);

        } else if (button==3) {           // Decrement
            menu_item = (menu_item >= deptRecordCount) ? deptRecordCount : menu_item+1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showSetDepartment(menu_item);

        } else if (button==2) {
            setDeptrecord(&department, deptRecords[menu_item-1].deptname, deptRecords[menu_item-1].deptid);
            break;
        }
    }
}

// Display settings
void showSettings(int menu_item) {
    if (menu_item==1) {
        disp_write("Choose Calls", 5, 2, false);
        disp_write("Set Department", 5, 3, false);
        disp_write("Exit", 5, 4, false);

        disp_write("Connect to WiFi", 5, 1, true);

    } else if (menu_item==2) {
        disp_write("Connect to WiFi", 5, 1, false);
        disp_write("Set Department", 5, 3, false);
        disp_write("Exit", 5, 4, false);

        disp_write("Choose Calls", 5, 2, true);

    } else if (menu_item==3) {
        disp_write("Connect to WiFi", 5, 1, false);
        disp_write("Choose Calls", 5, 2, false);
        disp_write("Exit", 5, 4, false);

        disp_write("Set Department", 5, 3, true);

    } else {
        disp_write("Connect to WiFi", 5, 1, false);
        disp_write("Choose Calls", 5, 2, false);
        disp_write("Set Department", 5, 3, false);

        disp_write("Exit", 5, 4, true);

    }            
}

// Settings menu
void settings() {
    int menu_item = 1;
    int button = 0;
    showSettings(menu_item);           // Increment

    while (true) {        
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item==1) ? 4 : menu_item-1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showSettings(menu_item);           // Increment

        } else if (button==3) {           // Decrement
            menu_item = (menu_item==4) ? 1 : menu_item+1;
            vTaskDelay(pdMS_TO_TICKS(500));
            disp_cls();
            showSettings(menu_item);           // Increment

        } else if (button==2) {           // Select
            vTaskDelay(pdMS_TO_TICKS(500));
            if (menu_item==1) {
                disp_cls();
                setCalls();
                showSettings(menu_item);
            } else if (menu_item==2) {
                disp_cls();
                setDepartment();
                showSettings(menu_item);
            } else {
                disp_cls();
                break;
            }
        }
    }

    ESP_LOGI(TAG_DISP, "Exiting settings()");
}

// Main menu
void showMainMenu(){      // Main Menu
    spacing = default_spacing; 
    char display_text[50];

    if (calls[0].status==NULL && calls[1].status==NULL && calls[2].status==NULL) {         // If no statuses set 
        disp_write("Do the initial Setup..", 5, 1,false);
        disp_write("Settings (Press OK)", 5, 2, true);

    } else {                // If statuses set
        sprintf(display_text, sizeof(display_text), "Call 1: %s", calls[0].mancalldesc);
        disp_write(calls[0].status, 5, 1, false);

        sprintf(display_text, sizeof(display_text), "Call 2: %s", calls[1].mancalldesc);
        disp_write(calls[1].status, 5, 2, false);

        sprintf(display_text, sizeof(display_text), "Call 3: %s", calls[2].mancalldesc);
        disp_write(calls[2].status, 5, 3, false);

        disp_write("Settings", 5, 4,true);
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    if (checkButtonPress()==2) {
        disp_cls();
        settings();           // Goto the settings menu
    }
}


// --------------------------------------------------------
//    Main App 
// --------------------------------------------------------

void app_main(void) {  
    gpio_setup();
    ESP_LOGI(TAG_CODE, "GPIO configured");

    // Display setup
    *gpio_out_w1ts_reg |= (1 << BKLT);   // Switch on backlight
    _init_TFT();
    //disp_start();
    ESP_LOGI(TAG_DISP, "Display Initiated");
    vTaskDelay(pdMS_TO_TICKS(500));
    *gpio_out_w1tc_reg |= (1 << BKLT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize NVS (Non-Volatile Storage) to store Wi-Fi credentials
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_CODE, "Non volatile memory initialized");
    
    // Wifi initialization
    wifi_init_sta();
    // Wait for Wi-Fi connection
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG_WIFI, "Wifi initialized");

    // Initializing Callrecords
    initialiseCallRecord();
    testCallRecords();

    // Initializing Websockets
    // char* WEBSOCKET_URI = resolve_mdns_host();
    websocket_app_start();
    esp_websocket_client_config_t websocket_cfg = {
        .uri = websocket_uri,  // Replace with your WebSocket server address
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);

    // Register the WebSocket event handler
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
    ESP_LOGI(TAG_SOCK, "Socket connection initialised");

    while(true) {
        // Send data
        send_data_task(client);

        int button = checkButtonPress();
        int displayontime = 0;

        if (button == 2) {
            *gpio_out_w1ts_reg |= (1 << BKLT);
            disp_cls();
            while(displayontime < 15) {
                button = 0;
                button = checkButtonPress();

                showMainMenu();
                vTaskDelay(pdMS_TO_TICKS(10));
                displayontime++;
            }
            
        }

        *gpio_out_w1tc_reg |= (1 << BKLT);
        
        vTaskDelay(pdMS_TO_TICKS(1000));       
    }
}
