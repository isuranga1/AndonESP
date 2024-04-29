#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <WebSocketClient.h>
#include <WiFiClientSecure.h>

char path[] = "/";
char host[] = "192.168.40.183"; // change ip to the server ////



WebSocketClient webSocketClient;
WiFiClient client;




void setup() {
    // WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    // it is a good practice to make sure your code sets wifi mode how you want it.

    // put your setup code here, to run once:
    Serial.begin(115200);
    wifiSetup();
   }

void loop() {
 wifiLoop();
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
    } 
    else {
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
