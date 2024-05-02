#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <WebSocketClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#define cs 10
#define dc 9
#define rst 8
#define UP 12
#define DOWN 14
#define SELOK 15

int spacing = 3;  // Linespacing for the display
int default_spacing = 3;

char path[] = "/";
char host[] = "192.168.40.183"; // change ip to the server ////

WebSocketClient webSocketClient;
WiFiClient client;

Adafruit_ST7735 disp = Adafruit_ST7735(cs, dc, rst);

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

String* getStatus() {
  // ------------------------------ 
  // Gets the statuses
  // ------------------------------ 

  String* statuses = new String[3];

  //test
  bool esSet = true;
  if (esSet) {
    statuses[0] = "Status 1";
    statuses[1] = "Status 2";
    statuses[2] = "Status 3";
  } else {
    for (int i=0; i<3; i++) {
      statuses[i] = "";
    }
  }

  return statuses;
}

int checkButtonPress() {
  // ------------------------------ 
  // Checks for the buttons pressed
  // ------------------------------ 

  if (digitalRead(UP)) {
    return 1;
  } else if (digitalRead(SELOK)) {
    return 2;
  } else if (digitalRead(DOWN)) {
    return 3;
  } else {
    return 0;
  }
}

void mainmenu(){          // Main Menu
  spacing = default_spacing;
  String* statuses = getStatus();

  bool statset = (!statuses[0].isEmpty() || !statuses[1].isEmpty() || !statuses[2].isEmpty()); 

  if (!statset) {         // If no statuses set 
    disp_write("  Set Status..", 2, 5, 1);
    disp_write("> Settings (Press OK)", 2, 5, 2);

  } else {                // If statuses set
    disp_write(statuses[0], 2, 10, 1);
    disp_write(statuses[1], 2, 10, 2);
    disp_write(statuses[2], 2, 10, 3);
  }

  if (checkButtonPress()==2) {
    disp_cls();
    settings();           // Goto the settings menu
  }
}

void settings() {

}

void setup() {
  // WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.
  disp.initR(INITR_BLACKTAB);
  disp_cls();
  disp_start();

  // put your setup code here, to run once:
  Serial.begin(115200);
  wifiSetup();
}

void loop() {
  wifiLoop();
}
