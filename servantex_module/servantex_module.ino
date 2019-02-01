/**
   servantex_module.ino

    Created on: 04.12.2018

    https://github.com/alisinabh/servantex_esp8266
*/

#include <Arduino.h>

#include <ESP8266WiFi.h>

#include <ESP8266HTTPClient.h>

#include <ArduinoJson.h>

#define WIFI_SSID         "WIFI_SSID"
#define WIFI_PASSWORD     "WIFI_PASSWORD"
#define SERVICE_TOKEN     "SERVANTEX_THING_TOKEN"
//#define HW_ID             ""
//#define DEBUG             1

//#define USE_SERIAL        Serial

#define SERVER_URL        "http://SERVER_URL/"
//#define CERT_FINGERPRINT  ""

#define PINMODE_URL       "thing_api/pin_modes"
#define PUSH_URL          "thing_api/push"
#define PULL_URL          "thing_api/pull"

#define FW_VERSION        "1"
#define MAX_GPIO          17
#define LOOP_DELAY        100
#define SYNC_DELAY        1500
#define NOT_SET           -1

const int PINMODE_STACK_SIZE = JSON_ARRAY_SIZE(12) + JSON_OBJECT_SIZE(1) + 13*JSON_OBJECT_SIZE(4) + 290;
const int PULL_STACK_SIZE    = JSON_ARRAY_SIZE(12) + JSON_OBJECT_SIZE(1) + 13*JSON_OBJECT_SIZE(4) + 290;

int pinStatus[MAX_GPIO];
int pinRefs[MAX_GPIO];
bool inputs[MAX_GPIO];
bool gotPinModes = false;

void syncPinModes();
void syncPinStates();
void debug(String);
void setPin(int, int);
int readPin(int);

HTTPClient http;

void setup() {
#ifdef DEBUG
  USE_SERIAL.begin(115200);
#endif

  debug("Servantex setup initiated!");

  int i;
  for (i = 0; i < MAX_GPIO; i++)
    pinStatus[i] = NOT_SET;

  
  for (uint8_t t = 5; t > 0; t--) {
    delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  delay(5000);
  debug("Servantex setup complete!");
}

int httpRequest(String relativeUrl, int isPost, String payload, String *p_response) {
    debug("[HTTP] begin...\n");

    String url = SERVER_URL + relativeUrl;

    http.setReuse(true);
    http.setTimeout(5000);

#ifdef CERT_FINGERPRINT
    http.begin(url, CERT_FINGERPRINT);
#else
    http.begin(url);
#endif

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", SERVICE_TOKEN);
    http.addHeader("servantex_fw", FW_VERSION);
#ifdef HW_ID
    http.addHeader("Servantex-HWID", HW_ID);
#else
    http.addHeader("Servantex-HWID", "MAC: " + WiFi.macAddress());
#endif

    debug("[HTTP] " + String(((isPost  == 1)? "POST": "GET")));
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
      debug("[HTTP] GET... code: " + String(httpCode));

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();

        *p_response = response;

        debug("Got response.");
        
      } else {
        String payload = http.getString();
        debug("HTTP ERROR: " + String(httpCode));
      }
    } else {
      debug("[HTTP] GET... failed, error:\n");
    }

    http.end();

    return httpCode;
}

int httpRequestJson(String relativeUrl, int isPost, String payload, int stackSize, JsonObject* &root) {
  
  String response;
  int httpCode = httpRequest(relativeUrl, isPost, payload, &response);

  if(httpCode == HTTP_CODE_OK) {
    debug("Got response, Deserialize...");
  
    DynamicJsonBuffer jsonBuffer(stackSize);
  
    JsonObject& rootObj = jsonBuffer.parseObject(response);
  
    root = &rootObj;
    debug("Deserialize done");
  }

  return httpCode;
}

void pullPinStates() {
  JsonObject *root;

  int httpCode = httpRequestJson(PULL_URL, 0, "", PULL_STACK_SIZE, root);

  if(httpCode != 200 || !(*root).success()) {
    debug("Error parsing json in sync");
    return;
  }

  JsonArray& pinStatuses = (*root)["states"];

  int i;
  for (i = 0; i < pinStatuses.size(); i++) {
    JsonObject& pin = pinStatuses[i];

    int pinNumber = pin["p"];
    int srvStatus = pin["s"];

    if (!inputs[pinNumber] && pinStatus[pinNumber] != srvStatus) {
      // A change has been made from server
      debug("Change in pin" +  String(pinNumber) + " status: " + String(pinStatus[pinNumber]) + " server: " + String(srvStatus));
      int transition = pin["t"];
      if(transition == 0)
        setPin(pinNumber, srvStatus); 
      else if(transition > 0 && transition < 500) {
        setPinTransition(pinNumber, srvStatus, transition);
      }
    }
  }

  debug("PULL complete");
}

void pushPinStates() {
  String payload = "";

  int i;
  for (i = 0; i < MAX_GPIO; i++) {
    payload += "pin" + String(i) + "=" + String(pinStatus[i]);

    if(i + 1 < MAX_GPIO)
      payload += "&";
  }

  String result;
  int httpCode = httpRequest(PUSH_URL, 1, payload, &result);

  if(httpCode != 200) {
    debug("PUSH Error!");
  } else {
    debug("PUSH complete."); 
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
  for(i = 0; i < pins.size(); i++) {
    JsonObject& pin = pins[i];
    int pinNumber = pin["p"];
    if(pinNumber < MAX_GPIO) {
      if (pin["m"] == 1) {
        pinMode(pinNumber, OUTPUT);
        debug("pin" + String(pinNumber) + " as output.");
        
        inputs[pinNumber] = false;

        if(pinStatus[pinNumber] == NOT_SET) {
          int val = pin["v"];
          setPin(pinNumber, val);
          debug("Init pin" + String(pinNumber) + ": " + String(val));
        }
      } else if (pin["m"] == 2 || pin["m"] == 3) {
        if(pin["m"] == 2)
          pinMode(pinNumber, INPUT);
        else
          pinMode(pinNumber, INPUT_PULLUP);
          
        debug("pin" + String(pinNumber) + " as input.");
        
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

  gotPinModes = true;
}

int counter = 0;
int wait = 0;
bool nextPull = true;
void loop() {
  // wait for WiFi connection
  if(wait >= SYNC_DELAY) {
    wait = 0; // Reset wait
    if (WiFi.status() == WL_CONNECTED) {
      counter++;

      if(!gotPinModes) {
        syncPinModes();
        
        delay(1000);

        int j;
        for (j = 0; j < MAX_GPIO; j++)
          if(inputs[j])
            pinStatus[j] = readPin(j);
      }

      if(nextPull)
        pullPinStates();
      else
        nextPull = true;
        
      pushPinStates();
  
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
        if(pinStatus[i] != newStatus) {
          // Status changed!
          int lastRefPinState = pinStatus[pinRefs[i]];
          int newState;
          
          if (lastRefPinState == 255)
            newState = 0;
          else
            newState = 255;
            
          setPin(pinRefs[i], 255 - lastRefPinState);
          nextPull = false;
        }
      }
      pinStatus[i] = newStatus;
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
  pinStatus[pin] = val;
  if(val >= 255) {
    digitalWrite(pin, HIGH);
  } else if (val <= 0) {
    digitalWrite(pin, LOW);
  } else {
    analogWrite(pin, val);
  }

  debug("Set pin" + String(pin) + ": " + String(val));
}

void setPinTransition(int pin, int val, int transDelay) {
  int lastVal = pinStatus[pin];

  int step;

  if (lastVal > val) {
    step = -1;
  } else {
    step = 1;
  }

  while (true) {
    lastVal += step;
    
    analogWrite(pin, lastVal);
      
    delay(transDelay);

    if(lastVal == val)
      break;
  }
  
  pinStatus[pin] = lastVal;
}

int readPin(int pin) {
  int val = digitalRead(pin);

  if (val == HIGH)
    return 255;
  else if (val == LOW)
    return 0;
}
