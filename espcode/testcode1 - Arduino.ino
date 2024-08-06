#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <WebSocketClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <time.h>
#include <WiFi.h>

#define cs    5
#define dc    17
#define rst   22
#define UP    21
#define DOWN  13
#define SELOK 14
#define CALL1 33
#define CALL2 34
#define CALL3 35
#define IND1  25
#define IND2  26
#define IND3  27

#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET_DST 0
int UTC_OFFSET = 0;

int curr_hour = 0;
int curr_mins = 0;

const String console_id = "0102401";

int spacing         = 3;  // Linespacing for the display
int default_spacing = 3;
int refresh_rate    = 500;

char path[] = "/";
char host[] = "192.168.40.183"; // change ip to the server ////

String calls[] = {"000","001","101"}; 

WebSocketClient webSocketClient;
WiFiClient client;

Adafruit_ST7735 disp = Adafruit_ST7735(cs, dc, rst);



// ------------------------------ Functions for displaying ------------------------------ 

void disp_write(String distring, int size, int x, int line, int color = 0xFFFF) {
  // ------------------------------ 
  // Prints onto display
  // ------------------------------ 

  int y = size*10*(line-1)+spacing;
  //static_assert(y <= 128, "Display overflow");

  disp.setTextColor(color);
  disp.setTextSize(size);
  disp.setCursor(x,y);
  disp.print(distring);
}

void disp_start() {
  // ------------------------------ 
  // Opening Animation
  // ------------------------------ 

  int fadetime = 260;
  for (uint16_t color=0; color<=0xFFFF; color+=0x210) {
    disp_write("Andon", 5, 5, 2, color);
    delay(fadetime/5);
  }
  delay(1000-fadetime);
  disp_cls();
}



void disp_cls() {
  // ------------------------------
  // Clears Display
  // ------------------------------ 

  disp.fillScreen(ST7735_BLACK);
}



void wifiSetup(){ //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;
  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library
  //wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  if(!res) {
    Serial.println("Failed to connect");
    // ESP.restart();
  } else {
    //if you get here you have connected to the WiFi    
    Serial.println("connected...yeey :)"); // display the ssid and connected status
  }



  // Connect to the websocket server
  if (client.connect("192.168.40.183", 443)) {  // change ip to the server
    Serial.println("Connected");
  } else {
    Serial.println("Connection failed.");
    while(1) {
      // Hang on failure
    }
  }

  // Handshake with the server
  webSocketClient.path = path;
  webSocketClient.host = host;
  if (webSocketClient.handshake(client)) {
    Serial.println("Handshake successful");
  } else {
    Serial.println("Handshake failed.");
    while(1) {
      // Hang on failure
    }  
  }
}



void wifiLoop(){
  String data;

  if (client.connected()) {
    
    webSocketClient.getData(data);
    if (data.length() > 0) {
      Serial.print("Received data: ");
      Serial.println(data);
    }

    String dataArray = "[1,2,3]";
    
    webSocketClient.sendData(dataArray);
    
  } else {
    Serial.println("Client disconnected.");
    while (1) {
      // Hang on disconnect.
    }
  }
  
  // wait to fully let the client disconnect
  delay(3000);
}



void callPress(){
  // ---------------------------------
  // Sending data when the button is pressed
  // ---------------------------------

  if (client.connected()) {
    if (digitalRead(CALL1)) {
      digitalWrite(IND1, LOW );
      digitalWrite(IND2, HIGH);
      digitalWrite(IND3, HIGH);
      updateTime();
      webSocketClient.sendData("[" + String(curr_hour) + "," + String(curr_mins) + "," + console_id + "," + calls[0] + "]");
    } else if (digitalRead(CALL2)) {
      digitalWrite(IND1, HIGH);
      digitalWrite(IND2, LOW );
      digitalWrite(IND3, HIGH);
      updateTime();
      webSocketClient.sendData("[" + String(curr_hour) + "," + String(curr_mins) + "," + console_id + "," + calls[1] + "]");
    } else if (digitalRead(CALL3)) {
      digitalWrite(IND1, HIGH);
      digitalWrite(IND2, HIGH);
      digitalWrite(IND3, LOW );
      updateTime();
      webSocketClient.sendData("[" + String(curr_hour) + "," + String(curr_mins) + "," + console_id + "," + calls[2] + "]");
    } else {
      digitalWrite(IND1, HIGH);
      digitalWrite(IND2, HIGH);
      digitalWrite(IND3, HIGH);
    }
        
  } else {
    Serial.println("Client disconnected.");
    while (1) {
      // Hang on disconnect.
    }
  }
  
  // wait to fully let the client disconnect
  delay(3000);
}



void updateTime() {
  // -----
  // Getting the time from the NTP server
  // -----

  struct tm timeinfo;

  // Check if there is any error in connection
  if (!getLocalTime(&timeinfo)) {
    disp_write("Connection Error",0,5,1);
    delay(200);
    disp_cls();
    return;
  }

  curr_hour   = timeinfo.tm_hour;
  curr_mins = timeinfo.tm_min;
}



void getCalls(String & call1, String & call2, String & call3) {
  // ------------------------------ 
  // Gets the statuses                                                                    ***** EDIT THIS *****
  // ------------------------------ 

  //test
  bool esSet = true;
  if (esSet) {
    call1 = "Status 1";
    call2 = "Status 2";
    call3 = "Status 3";
  } else {
    call1 = "";
    call2 = "";
    call3 = "";
  }
}



int checkButtonPress() {
  // ------------------------------ 
  // Checks for the buttons pressed
  // ------------------------------ 

  if (digitalRead(UP)) {
    disp_cls();
    return 1;
  } else if (digitalRead(SELOK)) {
    disp_cls();
    return 2;
  } else if (digitalRead(DOWN)) {
    disp_cls();
    return 3;
  } else {
    return 0;
  }
}



void showMainMenu(){      // Main Menu
  spacing = default_spacing;
  getCalls(calls[0], calls[1], calls[2]);

  bool statset = (!calls[0].isEmpty() || !calls[1].isEmpty() || !calls[2].isEmpty()); 

  if (!statset) {         // If no statuses set 
    disp_write("  Do the initial Setup..", 2, 5, 1);
    disp_write("> Settings (Press OK)", 2, 5, 2);

  } else {                // If statuses set
    disp_write(calls[0], 2, 10, 1);
    disp_write(calls[1], 2, 10, 2);
    disp_write(calls[2], 2, 10, 3);
  }

  if (checkButtonPress()==2) {
    disp_cls();
    settings();           // Goto the settings menu
  }
}



void showSettings(int menu_item) {
  disp_write(" Connect to WiFi", 2, 5, 1);
  disp_write(" Choose Calls", 2, 5, 2);
  disp_write(" Set Timezone", 2, 5, 3);
  disp_write(" Exit", 2, 5, 4);

  // Cursor
  if(menu_item==1)       { disp_write(">",2,5,1); }
  else if (menu_item==2) { disp_write(">",2,5,2); }
  else if (menu_item==3) { disp_write(">",2,5,3); }
  else if (menu_item==4) { disp_write(">",2,5,4); }
}



void connectWiFi() {

}



void chooseCalls() {

}



void showTimezone(String timezone_name, int menu_item) {
  // -----
  // Displaying the menu for setting timezone
  // -----

  // Menu items
  disp_write("  Timezone  "+timezone_name, 2,5,1);
  disp_write("  Save", 2,5,2);

  // Cursor
  if (menu_item == 1)       { disp_write(">", 2,5,1); } 
  else if (menu_item == 2)  { disp_write(">", 2,5,2); }
}



void setTimezone() {
  // -----
  // Changes the timezone from presaved values
  // -----

  // Contains all the standard timezones and their offsets
  int timezone_times[]    = {-43200,-39600,-36000, -34200, -32400, -28800, -25200, -21600, -18000, -14400, -12600, -10800, -7200, -3600, 
                             0, 3600, 7200, 10800, 12600, 14400, 16200, 18000, 19800, 20700, 21600, 23400, 25200, 28800, 31500, 32400, 
                             34200, 36000, 37800, 39600, 43200, 45900, 46800, 50400};
  String timezone_names[] = {" -12:00", " -11:00", " -10:00", "  -9:30", "  -9:00", "  -8:00", "  -7:00", "  -6:00", "  -5:00", "  -4:00", 
                             "  -3:30", "  -3:00", "  -2:00", "  -1:00", "+/-0:00", "  +1:00", "  +2:00", "  +3:00", "  +3:30", "  +4:00", 
                             "  +4:30", "  +5:00", "  +5:30", "  +5:45", "  +6:00", "  +6:30", "  +7:00", "  +8:00", "  +8:45", "  +9:00", 
                             "  +9:30", " +10:00", " +10:30", " +11:00", " +12:00", " +12:45", " +13:00", " +14:00"};
  
  int menu_item = 1;
  int button = 0;
  int timezone = 14;

  while (true) {
    showTimezone(timezone_names[timezone], menu_item);
    delay(refresh_rate);
    button = checkButtonPress();

    if (button == 2) {                                // Go UP
      menu_item--;
      if (menu_item<1) { menu_item = 2 - menu_item; }

    } else if (button == 3) {                         // Go DOWN
      menu_item++;
      if (menu_item>2) { menu_item = menu_item - 2; }

    } else if (button == 1) {
      if (menu_item == 1) {                           // Incrementing timezone
        timezone++;
        if (timezone>37) { timezone = timezone - 38; }

      } else {                                        // Saving and updating the NTP connection settings
        UTC_OFFSET = timezone_times[timezone];
        configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
        break;
      }
    }
  }
}



void settings() {
  int menu_item = 1;
  int button = 0;

  while (true) {
    showSettings(menu_item);           // Increment
    delay(refresh_rate);
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
        chooseCalls();
      } else if (menu_item==3) {
        disp_cls();
        setTimezone();
      } else {
        break;
      }
    }
  }
}



void setup() {
  pinMode(    UP,  INPUT);
  pinMode(  DOWN,  INPUT);
  pinMode( SELOK,  INPUT);
  pinMode( CALL1,  INPUT);
  pinMode( CALL2,  INPUT);
  pinMode( CALL3,  INPUT);
  pinMode(  IND1, OUTPUT);
  pinMode(  IND2, OUTPUT);
  pinMode(  IND3, OUTPUT);
  pinMode(  DEBG, OUTPUT);
  
  // WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.
  disp.initR(INITR_BLACKTAB);
  disp_cls();
  disp_start();

  // put your setup code here, to run once:
  Serial.begin(115200);
  wifiSetup();

  digitalWrite(IND1, HIGH);
  digitalWrite(IND2, HIGH);
  digitalWrite(IND3, HIGH);
}



void loop() {
  showMainMenu();
  
  if (checkButtonPress() == 2) {
    settings();
  }
  //wifiLoop();
  callPress();
}
