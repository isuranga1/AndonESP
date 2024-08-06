/* MIT License

Project: Wireless Reconfigurable Andon System with Maintenance Prediction
  * For the module EN2160: Electronic Design Realisation at University of 
    Moratuwa, Sri Lanka

Parts of the code taken from following repositories:
- Boris Lovosevic: TFT Display Code
  Copyright (c) 2017 Boris Lovosevic
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

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_wps.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_websocket_client.h"
#include "tft.h"
#include "tftspi.h"
#include "cJSON.h"

// Define registers
#define GPIO_OUT_W1TS_REG 0x3FF44008
#define GPIO_OUT_W1TC_REG 0x3FF4400C
#define GPIO_ENABLE_REG   0x3FF44020
#define GPIO_IN_REG       0x3FF4403C

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

void gpioSetup() {
    // -- -- Defining GPIO pins -- --
    // Enable registers
    volatile uint32_t* gpio_enable_reg = (volatile uint32_t*)
                                           GPIO_ENABLE_REG;

    // Inputs left 0, outputs set to 1
    *gpio_enable_reg |= (1<<IND1) | (1<<IND2) | (1<<IND3) | (1<<BKLT);

    // Output register
    volatile uint32_t* gpio_out_w1ts_reg = (volatile uint32_t*) 
                                             GPIO_OUT_W1TS_REG;

    volatile uint32_t* gpio_out_w1tc_reg = (volatile uint32_t*) 
                                             GPIO_OUT_W1TC_REG;

    // Input register
    volatile uint32_t* gpio_in_reg = (volatile uint32_t*) GPIO_IN_REG;
}

// console ID to be defined in building
static const char *TAG_CODE = "code";
string websocket_server_uri = "192.168.40.183"

int spacing         = 3;  // Linespacing for the display
int default_spacing = 3;
int refresh_rate    = 500;

const int MAX_CALL_RECORDS = 50; // Adjust as needed
const int MAX_DEPT_RECORDS = 50; // Adjust as needed

struct Callrecord {
    string status;
    string mancalldesc;
    string mancallto;
};

struct Deptrecord {
    string deptname;
    int deptid;
};

Callrecord callRecords[MAX_CALL_RECORDS];
int callRecordCount = 0;

Deptrecord deptRecords[MAX_DEPT_RECORDS];
int deptRecordCount = 0;

string department_id = "";
Deptrecord department;
Callrecord calls[3] = {
    {"", "", ""},
    {"", "", ""},
    {"", "", ""}
};





// --------------------------------------------------------
// -------------- Functions for WiFi ----------------------
// -------------------------------------------------------- 

// Set WPS mode via project configuration
#define WPS_MODE WPS_TYPE_PBC
#define MAX_RETRY_ATTEMPTS 2
#define MAX_WPS_AP_CRED    5

static const char *TAG_WIFI = "wifi";
static esp_wps_config_t config = WPS_CONFIG_INIT_DEFAULT(WPS_MODE);
static wifi_config_t wps_ap_creds[MAX_WPS_AP_CRED];
static int s_ap_creds_num = 0;
static int s_retry_num = 0;

static esp_websocket_client_handle_t client;

// Event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    static int ap_idx = 1;

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG_WIFI, "WIFI_EVENT_STA_START");
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG_WIFI, "WIFI_EVENT_STA_DISCONNECTED");
            if (s_retry_num < MAX_RETRY_ATTEMPTS) {
                esp_wifi_connect();
                s_retry_num++;
            
            } else if (ap_idx < s_ap_creds_num) {
                // Try the next AP credential if first one fails

                if (ap_idx < s_ap_creds_num) {
                    ESP_LOGI(TAG_WIFI, 
                             "Connecting to SSID: %s, Passphrase: %s",
                             wps_ap_creds[ap_idx].sta.ssid, 
                             wps_ap_creds[ap_idx].sta.password);
            
                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, 
                                    &wps_ap_creds[ap_idx++]) );
                                    esp_wifi_connect();
                }
                s_retry_num = 0;
            
            } else {
                ESP_LOGI(TAG_WIFI, "Failed to connect!");
            }

            break;
        
        case WIFI_EVENT_STA_WPS_ER_SUCCESS:
            ESP_LOGI(TAG_WIFI, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
            {
                wifi_event_sta_wps_er_success_t *evt =
                    (wifi_event_sta_wps_er_success_t *)event_data;
                int i;

                if (evt) {
                    s_ap_creds_num = evt->ap_cred_cnt;
                    for (i = 0; i < s_ap_creds_num; i++) {
                        memcpy(wps_ap_creds[i].sta.ssid, 
                               evt->ap_cred[i].ssid,
                               sizeof(evt->ap_cred[i].ssid));
        
                        memcpy(wps_ap_creds[i].sta.password, 
                               evt-> ap_cred[i].passphrase,
                               sizeof(evt->ap_cred[i].passphrase));
                    }
                    // If multiple AP credentials are received from WPS, 
                    // connect with first one
                    ESP_LOGI(TAG_WIFI, 
                             "Connecting to SSID: %s, Passphrase: %s",
                             wps_ap_creds[0].sta.ssid, 
                             wps_ap_creds[0].sta.password);
        
                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, 
                                    &wps_ap_creds[0]) );
                }
                /*
                   If only one AP credential is received from WPS, there 
                   will be no event data and
                   esp_wifi_set_config() is already called by WPS modules 
                   for backward compatibility
                   with legacy apps. So directly attempt connection here.
                 */
                ESP_ERROR_CHECK(esp_wifi_wps_disable());
                esp_wifi_connect();
            }
            break;
        
        case WIFI_EVENT_STA_WPS_ER_FAILED:
            ESP_LOGI(TAG_WIFI, "WIFI_EVENT_STA_WPS_ER_FAILED");
            ESP_ERROR_CHECK(esp_wifi_wps_disable());
            ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
            ESP_ERROR_CHECK(esp_wifi_wps_start(0));
            break;
        
        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
            ESP_LOGI(TAG_WIFI, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
            ESP_ERROR_CHECK(esp_wifi_wps_disable());
            ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
            ESP_ERROR_CHECK(esp_wifi_wps_start(0));
            break;
        
        default:
            break;
    }
}

// Event handler for when credentials received
static void got_ip_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG_WIFI, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
}

// Initialise WiFi as STA and start WPS
static void start_wps(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID, 
                                               &wifi_event_handler, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, 
                                               IP_EVENT_STA_GOT_IP, 
                                               &got_ip_event_handler, 
                                               NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "start wps...");

    ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
    ESP_ERROR_CHECK(esp_wifi_wps_start(0));
}


// ---------- Establishing WebSocket Connection ----------
// Event Handler
static void websocket_event_handler(void *handler_args, esp_event_base_t    
                                    base, int32_t event_id, 
                                    void *event_data) {
    
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)
                                        event_data;
    
    esp_websocket_client_handle_t client = (esp_websocket_client_handle_t) 
                                            handler_args;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG_WIFI, "WebSocket connected");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_WIFI, "WebSocket disconnected, code: %d", 
            data->status_code);
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(TAG_WIFI, "WebSocket received data: %.*s", 
                     data->data_len, (char *)data->data_ptr);
            break;
        default:
            break;
    }
}

// Sending socket
void send_websocket_message(esp_websocket_client_handle_t client, 
                            const char *message) {
    if (esp_websocket_client_is_connected(client)) {
        esp_websocket_client_send_text(client, message, strlen(message), 
                                       portMAX_DELAY);
    } else {
        ESP_LOGE(TAG_WIFI, "WebSocket client is not connected.");
    }
}

esp_websocket_client_handle_t websocket_client_init(void) {
    esp_websocket_client_config_t ws_cfg = {
        .uri = websocket_server_uri, // your WebSocket server URL
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(
                                                    &ws_cfg);
    
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, 
                                  websocket_event_handler, client);
    
    esp_websocket_client_start(client);

    return client;
}


// ----------- Getting calls and departments via http ----------
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





// --------------------------------------------------------
// -------------- Functions for Calling -------------------
// -------------------------------------------------------- 

void callPress(){
    // -- -- Sending data -- --

    if (esp_websocket_client_is_connected(client)) {
        char jsonString[200];
        const char* call1 = "";
        const char* call2 = "";
        const char* call3 = "";

        if (*gpio_in_reg & (1 << CALL1)) {
            call1 = calls[0].status;
            *gpio_out_w1ts_reg = (1 << IND1);

        } else if (*gpio_in_reg & (1 << CALL2)) {
            call2 = calls[1].status;
            *gpio_out_w1ts_reg = (1 << IND2);

        } else if (*gpio_in_reg & (1 << CALL3)) {
            call3 = calls[2].status;
            *gpio_out_w1ts_reg = (1 << IND3);
        
        } else {
            *gpio_out_w1tc_reg |= (1 << IND1) | (1 << IND2) | (1 << IND3);
        }

        snprintf(jsonString, sizeof(jsonString),
                "{\"consoleid\":%d,\"department\":%d,\"call1\":\"%s\",
                \"call2\":\"%s\",\"call3\":\"%s\",\"oldcall\":\"%s\"}",
                console_id, department_id, call1, call2, call3, "");

        send_websocket_message(client, jasonString);
        ESP_LOGI(TAG_WIFI, "Frame sent");
            
    } else {
        ESP_LOGI(TAG_WIFI, "Client disconnected.");
        while (true) {
            // Hang on disconnect.
        }
    }
    
    // wait to fully let the client disconnect
    vTaskDelay(pdMS_TO_TICKS(3000));
}





// --------------------------------------------------------
// -------------- Functions for displaying ----------------
// -------------------------------------------------------- 
static const char *TAG_DISP = "display";
#define SPI_BUS TFT_HSPI_HOST

void disp_write(string distring, int x, int line) {
    // -- -- Prints onto display -- --

    int y = size*10*(line-1)+spacing;
    TFT_setFont(DEFAULT_FONT, NULL);
    TFT_print(distring, x, y);
}

void disp_start() {
    // -- -- Opening Animation -- -- 

    int fadetime = 260;
    for (uint8_t col=0; col<=255; col+=1) {
        _fg = (color_t){col, col, col};
        disp_write("Andon", 5, 2);
        vTaskDelay(pdMS_TO_TICKS(fadetime/5));
    }
    vTaskDelay(pdMS_TO_TICKS(1000-fadetime));
    disp_cls();
}

void disp_cls() {
    // -- -- Clears Display -- -- 
    TFT_fillScreen(TFT_BLACK);
    TFT_resetclipwin();
}





// --------------------------------------------------------
// -------------- Functions for Menu ----------------------
// -------------------------------------------------------- 

int checkButtonPress() {
    // -- -- Checks for the buttons pressed -- -- 

    if (*gpio_in_reg & (1 << UP)) {
        return 1;
    } else if (*gpio_in_reg & (1 << SELOK)) {
        return 2;
    } else if (*gpio_in_reg & (1 << DOWN)) {
        return 3;
    } else {
        return 0;
    }
}

void showMainMenu(){      // Main Menu
    spacing = default_spacing;

    bool statset = (!calls[0].isEmpty() || !calls[1].isEmpty() 
                    || !calls[2].isEmpty()); 

    if (!statset) {         // If no statuses set 
        disp_write("  Do the initial Setup..", 5, 1);
        disp_write("> Settings (Press OK)", 5, 2);

    } else {                // If statuses set
        disp_write(calls[0].status, 10, 1);
        disp_write(calls[1].status, 10, 2);
        disp_write(calls[2].status, 10, 3);
        disp_write(">Settings", 10, 4);
    }

    if (checkButtonPress()==2) {
        disp_cls();
        settings();           // Goto the settings menu
    }
}

void showSettings(int menu_item) {
    disp_write(" Connect to WiFi", 5, 1);
    disp_write(" Choose Calls", 5, 2);
    disp_write(" Set Department", 5, 3);
    disp_write(" Exit", 5, 4);

    // Cursor
    switch (menu_item) {
        case 1:
            disp_write(">",5,1);
        case 2:
            disp_write(">",5,2);
        case 3:
            disp_write(">",5,3);
        case 4:
            disp_write(">",5,4);
    }
}

void settings() {
    int menu_item = 1;
    int button = 0;

    while (true) {
        showSettings(menu_item);           // Increment
        vTaskDelay(pdMS_TO_TICKS(refresh_rate));
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item==0) ? 4 : menu_item-1;
            disp_cls();

        } else if (button==3) {           // Decrement
            menu_item = (menu_item==5) ? 1 : menu_item+1;
            disp_cls();

        } else if (button==2) {           // Select
        if (menu_item==1) {
            disp_cls();
            connectWiFi();
        } else if (menu_item==2) {
            disp_cls();
            setCalls();
        } else if (menu_item==3) {
            disp_cls();
            setDepartment();
        } else {
            break;
        }
        }
    }
}

void connectWiFi() {
    // Do nothing
    int menu_item = 1;
    int button = 0;
    int c = 0;
    string ccc = "000";
    int d = 0;
    string ddd = "000";

    while (true) {
        showConnectWiFi(menu_item, ccc, ddd);
        vTaskDelay(pdMS_TO_TICKS(refresh_rate));
        button = checkButtonPress();

        if (button == 2) {
            if (menu_item == 1) {
                menu_item = (menu_item > 2) ? 1 : menu_item+1;
            } else if (menu_item == 2) {
                break;
            }
        } else if (button == 1) {
            if (menu_item == 1) {
                c = (c < 0) ? 255 : c-1;
            } else if (menu_item == 2) {
                d = (d < 0) ? 255 : d-1;
            }
        } else if (button == 3) {
            if (menu_item == 1) {
                c = (c > 255) ? 0 : c+1;
            } else if (menu_item == 2) {
                d = (d > 255) ? 0 : d+1;
            }
        }
    }

    if (c <= 9) {
        ccc = "00"+to_string(c);
    } else if (c <= 99) {
        ccc = "0"+to_string(c);
    } else {
        ccc = to_string(c);
    }
    
    if (d <= 9) {
        ddd = "00"+to_string(d);
    } else if (d <= 99) {
        ddd = "0"+to_string(d);
    } else {
        ddd = to_string(d);
    }

    websocket_server_uri = "192:168:"+ccc+":"+ddd;
}

void showConnectWiFi(int menu_item, string ccc, string ccc) {
    disp_write("Set the IP of Back-end", 5, 2);
    if (menu_item == 1) {
        disp_write("192:168:>" + ccc + ":" + ddd, 5, 2);
    } else if (menu_item == 2) {
        disp_write("192:168:" + ccc + ":>" + ddd, 5, 2);
    } 
    
}

void showSetCalls(int menu_item) {
    int button = 0;
    if (calls[0].isEmpty()) {
        disp_write(" Choose Call 1", 5, 1);
    } else {
        disp_write(" " + calls[0].status, 5, 1);
    }
    if (calls[1].isEmpty()) {
        disp_write(" Choose Call 2", 5, 2);
    } else {
        disp_write(" " + calls[1].status, 5, 2);
    }
    if (calls[2].isEmpty()) {
        disp_write(" Choose Call 3", 5, 3);
    } else {
        disp_write(" " + calls[2].status, 5, 3);
    }
    disp_write(" Back", 5, 4);

    switch (menu_item % 4) {
        case 1:
            disp_write(">",5,1);
        case 2:
            disp_write(">",5,2);
        case 3:
            disp_write(">",5,3);
        case 0:
            disp_write(">",5,4);
    }
}

void setCalls() {
    int menu_item = 1;
    int button = 0;

    while (true) {
        showSetCalls(menu_item);
        vTaskDelay(pdMS_TO_TICKS(refresh_rate));
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item <= 0) ? 4 : menu_item-1;
            disp_cls();

        } else if (button==3) {           // Decrement
            menu_item = (menu_item >= 3) ? 0 : menu_item+1;
            disp_cls();

        } else if (button==2) {
            if (menu_item==1) {
                disp_cls();
                chooseCalls(0);
            } else if (menu_item==2) {
                disp_cls();
                chooseCalls(1);
            } else if (menu_item==3) {
                disp_cls();
                chooseCalls(2);
            } else {
                break;
            }
        }
    }
}

void showChooseCalls(int menu_item) {
    int button = 0;
    if (int callRecordCount == 0) {
        disp_write(" No calls set", 5, 1);
        disp_write(" Please set on mgmt console", 5, 2);
        disp_write(">Go back", 5, 3);

    } else {
        if (menu_item+3 <= callRecordCount-1) {
            disp_write(" " + callRecords[menu_item].status, 5, 1);
            disp_write(" " + callRecords[menu_item+1].status, 5, 2);
            disp_write(" " + callRecords[menu_item+2].status, 5, 3);
            disp_write(" " + callRecords[menu_item+3].status, 5, 4);
    
        } else if (menu_item+2 <= callRecordCount-1) {
            disp_write(" " + callRecords[menu_item].status, 5, 1);
            disp_write(" " + callRecords[menu_item+1].status, 5, 2);
            disp_write(" " + callRecords[menu_item+2].status, 5, 3);
    
        } else if (menu_item+1 <= callRecordCount-1) {
            disp_write(" " + callRecords[menu_item].status, 5, 1);
            disp_write(" " + callRecords[menu_item+1].status, 5, 2);
    
        } else {
            disp_write(" " + callRecords[menu_item].status, 5, 1);
        }

        switch (menu_item % 4) {
            case 1:
                disp_write(">",5,1);
            case 2:
                disp_write(">",5,2);
            case 3:
                disp_write(">",5,3);
            case 0:
                disp_write(">",5,4);
        }
    }
}

void chooseCalls(int callIndex) {
    fetchCallRecords();

    int menu_item = 1;
    int button = 0;

    while (true) {
        showChooseCalls(menu_item);
        vTaskDelay(pdMS_TO_TICKS(refresh_rate));
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item <= 0) ? 1 : menu_item-1;
            disp_cls();

        } else if (button==3) {           // Decrement
            menu_item = (menu_item >= callRecordCount) ? callRecordCount : 
            menu_item+1;
            disp_cls();

        } else if (button==2) {
            calls[callIndex] = callRecords[menu_item-1];
            break();
        }
    }
}

void showSetDepartment(int menu_item) {
    int button = 0;
    if (int deptRecordCount == 0) {
        disp_write(" No departments set", 5, 1);
        disp_write(" Please set on mgmt console", 5, 2);
        disp_write(">Go back", 5, 3);

    } else {
        if (menu_item+3 <= deptRecordCount-1) {
            disp_write(" " + deptRecords[menu_item].deptname, 5, 1);
            disp_write(" " + deptRecords[menu_item+1].deptname, 5, 2);
            disp_write(" " + deptRecords[menu_item+2].deptname, 5, 3);
            disp_write(" " + deptRecords[menu_item+3].deptname, 5, 4);
    
        } else if (menu_item+2 <= deptRecordCount-1) {
            disp_write(" " + deptRecords[menu_item].deptname, 5, 1);
            disp_write(" " + deptRecords[menu_item+1].deptname, 5, 2);
            disp_write(" " + deptRecords[menu_item+2].deptname, 5, 3);
    
        } else if (menu_item+1 <= deptRecordCount-1) {
            disp_write(" " + deptRecords[menu_item].deptname, 5, 1);
            disp_write(" " + deptRecords[menu_item+1].deptname, 5, 2);
    
        } else {
            disp_write(" " + deptRecords[menu_item].deptname, 5, 1);
        }

        switch (menu_item % 4) {
            case 1:
                disp_write(">",5,1);
            case 2:
                disp_write(">",5,2);
            case 3:
                disp_write(">",5,3);
            case 0:
                disp_write(">",5,4);
        }
    }
}

// Check numbering again
void setDepartment() {
    fetchDeptRecords();

    int menu_item = 1;
    int button = 0;

    while (true) {
        showSetDepartment(menu_item);
        vTaskDelay(pdMS_TO_TICKS(refresh_rate));
        button = checkButtonPress();

        if (button==1) {
            menu_item = (menu_item <= 0) ? 1 : menu_item-1;
            disp_cls();

        } else if (button==3) {           // Decrement
            menu_item = (menu_item >= deptRecordCount) ? deptRecordCount : 
            menu_item+1;
            disp_cls();

        } else if (button==2) {
            department = deptRecords[menu_item-1];
            break();
        }
    }
}





// --------------------------------------------------------
// -------------- Functions for main ----------------------
// -------------------------------------------------------- 

void app_main() {
    // Get console ID
    const char *console_id = getenv("CONSOLE_ID");
    if (console_id == NULL) {
        console_id = "0102401";  // Default console ID
        ESP_LOGW(TAG_CODE, 
            "CONSOLE_ID environment variable not set. Using default: %s", 
            console_id);
    }

    // Display setup
    *gpio_out_w1ts_reg = (1 << BKLT);   // Switch on backlight
    esp_err_t ret1;

    tft_disp_type = DEFAULT_DISP_TYPE;
    _width  = DEFAULT_TFT_DISPLAY_WIDTH;
    _height = DEFAULT_TFT_DISPLAY_HEIGHT;
    max_rdclock = 8000000;

    TFT_PinsInit();

    // Configuring SPI
    spi_lobo_device_handle_t spi;

    spi_lobo_bus_config_t buscfg = {
        .miso_io_num = -1,  // No MISO line
        .mosi_io_num = MOSI,
        .sclk_io_num = SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        .max_transfer_sz = 6*1024,
    };

    spi_lobo_device_interface_config_t devcfg = {
        .clock_speed_hz = 8000000,
        .mode = 0,
        .spics_io_num = -1,
        .spics_ext_io_num = CS,
        .flags = LB_SPI_DEVICE_HALFDUPLEX,
    };

    ret1 = spi_lobo_bus_add_device(SPI_BUS, &buscfg, &devcfg, &spi);
    assert(ret1 == ESP_OK);
    disp_spi = spi;

    ESP_LOGI(TAG_DISP, "SPI: display init...");
    TFT_display_init();
    ESP_LOGI(TAG_DISP, "OK");
    
    spi_lobo_set_speed(spi, DEFAULT_SPI_CLK);
    ESP_LOGI(TAG_DISP,"SPI: Changed speed to %u", spi_lobo_get_speed(spi));

    disp_start();

    //Initialise NVS
    esp_err_t ret2 = nvs_flash_init();
    if (ret2 == ESP_ERR_NVS_NO_FREE_PAGES || ret2 == 
        ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret2 = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret2);
    start_wps();

    client = websocket_client_init();

    // GPIO setup
    gpioSetup();

    // Fetch Department and Call info
    fetchDeptRecords();
    fetchCallRecords();

    int displaytime;

    while (true) {
        displaytime = 0;
        int button = 0;
        button = checkButtonPress();
        
        if (button == 2) {
            button = 0;
            button = checkButtonPress();

            while (displaytime < 10) {
                // Displays the main menu
                showMainMenu();
                vTaskDelay(pdMS_TO_TICKS(500));
                displaytime++;

                if (button == 2) {
                    // Check for navigation
                    settings();
                }
            }
        } 

        // Checks for calls
        callPress();
    }
}