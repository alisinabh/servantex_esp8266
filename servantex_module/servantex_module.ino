/**
   servantex_module.ino

    Created on: 04.12.2018

    https://github.com/alisinabh/servantex_esp8266
*/

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>

#include <ArduinoJson.h>

#define WIFI_SSID         ""
#define WIFI_PASSWORD     ""
#define SERVICE_TOKEN     ""
//#define HW_ID             ""
#define DEBUG             1

#define USE_SERIAL        Serial

#define SERVER_URL        "http://home.alisinabh.com/"
//#define CERT_FINGERPRINT  ""

#define PINMODE_URL       "api/pin_modes"
#define SYNC_URL          "api/sync"

#define FW_VERSION        "0.0.1"
#define MAX_GPIO          13
#define LOOP_DELAY        50
#define SYNC_DELAY        1500
#define NOT_SET           0
#define ON_HIGH           1
#define OFF_LOW           2

const int PINMODE_STACK_SIZE = JSON_ARRAY_SIZE(13) + JSON_OBJECT_SIZE(1) + 13*JSON_OBJECT_SIZE(3) + 220;
const int SYNC_STACK_SIZE    = JSON_ARRAY_SIZE(13) + JSON_OBJECT_SIZE(1) + 13*JSON_OBJECT_SIZE(3) + 220;

int pinStatus[MAX_GPIO];
int pinRefs[MAX_GPIO];
bool inputs[MAX_GPIO];

ESP8266WiFiMulti WiFiMulti;

void syncPinModes();
void syncPinStates();
void debug(String);
void setPin(int, int);
int readPin(int);

void setup() {

  USE_SERIAL.begin(115200);

  for (uint8_t t = 5; t > 0; t--) {
    delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
}

int httpRequestJson(String relativeUrl, int isPost, String payload, int stackSize, JsonObject* &root) {
   HTTPClient http;

    debug("[HTTP] begin...\n");

    String url = SERVER_URL + relativeUrl;

#ifdef CERT_FINGERPRINT
    http.begin(url, CERT_FINGERPRINT);
#else
    http.begin(url);
#endif

    http.addHeader("Authorization", SERVICE_TOKEN);
#ifdef HW_ID
    http.addHeader("Servantex-HWID", HW_ID);
#else
    http.addHeader("Servantex-HWID", "MAC: " + WiFi.macAddress());
#endif

    debug("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode;
    
    if(isPost) {
      httpCode = http.POST(payload);
    } else {
      httpCode = http.GET();
    }

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      debug("[HTTP] GET... code:\n");
      debug(httpCode + "");

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        debug(payload);
        
        DynamicJsonBuffer jsonBuffer(stackSize);

        JsonObject& rootObj = jsonBuffer.parseObject(payload);

        root = &rootObj;
        
      } else {
        String payload = http.getString();
        debug("HTTP ERROR");
        debug(payload);
      }
    } else {
      debug("[HTTP] GET... failed, error:\n");
    }

    http.end();

    return httpCode;
}

void syncPinStates() {
  JsonObject *root;

  String payload = "";

  int i;
  for (i = 0; i < MAX_GPIO; i++) {
    payload += "pin" + String(i) + "=" + String(pinStatus[i]);

    if(i + 1 < MAX_GPIO)
      payload += "&";
  }

  int httpCode = httpRequestJson(SYNC_URL, 1, payload, SYNC_STACK_SIZE, root);

  if(httpCode != 200 || !(*root).success()) {
    debug("Error parsing json in sync");
    return;
  }

  JsonArray& pinStatuses = (*root)["status"];

  for (i = 0; i < sizeof(pinStatuses); i++) {
    JsonObject& pin = pinStatuses[i];

    int pinNumber = pin["p"];
    int srvStatus = pin["s"];

    if (pinStatus[i] - 1 != srvStatus) {
      // A change has been made from server
      setPin(i, srvStatus);
    }
  }
}

void syncPinModes() {
  JsonObject *root;

  int httpCode = httpRequestJson(PINMODE_URL, 0, "", PINMODE_STACK_SIZE, root);

  if (httpCode != 200 || !(*root).success()) {
    debug("Error parsing json in pinmode");
    return;
  }

  JsonArray& pins = (*root)["pins"];

  int i;
  for(i = 0; i < sizeof(pins); i++) {
    JsonObject& pin = pins[i];
    int pinNumber = pin["p"];
    if(pinNumber < MAX_GPIO) {
      if (pin["m"] == 1) {
        pinMode(pinNumber, OUTPUT);

        if(pinStatus[pinNumber] == NOT_SET) {
          setPin(pin["p"], pin["v"]);
        }
      }
      else if (pin["m"] == 2) {
        pinMode(pinNumber, INPUT);
        inputs[pinNumber] = true;

        if(pin.containsKey("r")) {
          int r = pin["r"];
          pinRefs[pinNumber] = r;
        } else {
          pinRefs[pinNumber] = -1;
        }
      }
    } else {
      debug("GPIO pin Does not exist");
    }
  }
}

int counter = 0;
int wait = 0;
void loop() {
  // wait for WiFi connection
  if(wait >= SYNC_DELAY) {
    wait = 0; // Reset wait
    if ((WiFiMulti.run() == WL_CONNECTED)) {
      counter++;
  
      syncPinStates();
  
      if(counter > 10) {
        counter = 0;
        syncPinModes();
      }
    }
  }

  int i;
  for (i = 0; i < MAX_GPIO; i++) {
    if(inputs[i]) {
      int newStatus = readPin(i);
      if (pinRefs[i] != -1) {
        if(pinStatus[i] != newStatus + 1) {
          // Status changed!
          int lastRefPinState = pinStatus[pinRefs[i]];
          setPin(pinRefs[i], 255 - lastRefPinState + 1);
        }
      }
      pinStatus[i] = newStatus + 1;
    }
  }

  delay(LOOP_DELAY);
  wait += LOOP_DELAY;
}

void debug(String payload) {
#ifdef DEBUG
  USE_SERIAL.println(payload);
#endif
}

void setPin(int pin, int val) {
  pinStatus[pin] = val + 1;
  if(val >= 255) {
    digitalWrite(pin, HIGH);
  } else if (val <= 0) {
    digitalWrite(pin, LOW);
  } else {
    analogWrite(pin, val);
  }
}

int readPin(int pin) {
  int val = digitalRead(pin);

  if (val == HIGH)
    return 255;
  else if (val == LOW)
    return 0;
}

