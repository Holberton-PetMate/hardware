#include <esp_system.h>
#include "HX711.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <TimeLib.h>
#include "variables.h"


String ssid_wifi;
String password_wifi;
String code_id;
int feeder_id = 0;
int connected = 0;
int wifi_desconnected_counter = 599;
int get_feeding_time_counter = 0;
int food_counter = 50;
JsonArray feeding_times;
String networks_temp = "";
int weight = 0;

int pinBSCK = 4;
int pinBDT = 16;

int pinObs = 27;

int pinUltTriq = 21;
int pinUltEcho = 32;

int pinCloseEngine = 25;
int pinOpenEngine = 22;
int pinSubEngine = 26;

HX711 balance;

WebServer server(80);
HTTPClient http;

String methodHTTPS(String host, String url) {

  WiFiClientSecure client(443);

  client.setInsecure();

  if (!client.connect(host.c_str(), 443)) {
    Serial.println("Error al conectarse al servidor");
    return "";
  }
  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break;
    }
  }
  String result = "";
  while (client.available()) {
    char c = client.read();
    if (c == '{') {
      result += c;
      while (client.available()) {
        c = client.read();
        result += c;
      }
      break;
    }
  }
  client.stop();
  return result;
}

String methodHTTP(String host, String url, String body = "") {
  String result = {};

  http.begin(host + url + body);
  http.addHeader("Content-Type", "text/plain");

  if (http.GET() > 0) {
    result = http.getString();
  } else {
    Serial.println("Error on HTTP request");
  }
  return result;
}

DynamicJsonDocument getRequestMethod(String host, String url, String body = "", int https = 0) {
  DynamicJsonDocument response(1024);
  String result = https ? methodHTTPS(host, url) : methodHTTP(host, url, body);
  DeserializationError error = deserializeJson(response, result);
  if (error) {
    Serial.print("Error al analizar la cadena de caracteres JSON: ");
    Serial.println(error.c_str());
  }
  return response;
}
void getId() {
  feeder_id = 17;  //getRequestMethod(backend_host, "/backend/pet-mate-app/public/api/feeders/check_redeemed?code_id=" + code_id)["id"];
}
void getFeedingTime() {
  feeding_times = getRequestMethod(backend_host, "/backend/pet-mate-app/public/api/feeders/" + String(feeder_id) + "/feeding_times").as<JsonArray>();
}
void setTimeNow() {
  JsonObject date = getRequestMethod("api.ipgeolocation.io", get_time_zone_url, "", 1).as<JsonObject>();
  String time = date["time_24"];
  setTime(time.substring(0, 2).toInt(), time.substring(3, 5).toInt(), 0, date["date"].as<String>().substring(8, 10).toInt(), date["month"].as<int>(), date["year"].as<int>());
}


void serverHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}
void getWifiNetworks() {
  //WiFi.disconnect();

  String networks_dict;
  if (networks_temp == "") {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument networks(512);
    for (int i = 0; i < n; ++i) {
      JsonObject network = networks.createNestedObject();
      network["ssid"] = WiFi.SSID(i);
      network["rssi"] = WiFi.RSSI(i);
      Serial.println(i);
    }
    serializeJson(networks, networks_dict);

    networks_temp = networks_dict;

  } else {
    networks_dict = networks_temp;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(200, "application/json", networks_dict);
}
void wifiConfiguration() {
  if (server.method() == HTTP_OPTIONS) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(200, "text/plain", "OPTIONS");
    return;
  }
  DynamicJsonDocument response(1024);
  String pay = server.arg("plain");
  Serial.println(pay);
  DeserializationError error = deserializeJson(response, pay);
  if (error) {
    Serial.print("Error al analizar la cadena de caracteres JSON: ");
    Serial.println(error.c_str());
  }
  String ssid = response["ssid"];
  String password = response["password"];
  Serial.println("SSID: " + ssid);
  Serial.println("Password: " + password);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Conectando a la red ");
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i != 10) {
    delay(1000);
    Serial.print(".");
    i++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    EEPROM.put(0, ssid.c_str());
    EEPROM.put(128, password.c_str());
    EEPROM.commit();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(200, "application/json", "{\"status\":\"Connected\"}");
  } else {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(200, "application/json", "{\"status\":\"Fail\"}");
  }
}

void analyzeFeedingTimes() {
  for (JsonObject feeding_time : feeding_times) {
    if (hour() == feeding_time["hour"] && minute() == feeding_time["minute"]) {
      weight = balance.get_units(10);
      int open = 0;
      int old_weight = balance.get_units(10) - 10;
      int unlock_fail = 0;
      while ((weight + 10) < feeding_time["weight"]) {
        if (open == 0) {
          openDoor(500);
          open = 1;
        }
        if (old_weight == weight) {
          if (unlock_fail > 3)
          {
            return;
          }
          unlockDoor();
          unlock_fail++;
        }
        else {
          unlock_fail = 0;
        }
        
        old_weight = balance.get_units(10);
        Serial.println("old");
        Serial.println(old_weight);
        delay(100);

        weight = balance.get_units(10);
        Serial.println("Current");

        Serial.println(weight);
      }
      if (open) {
        closeDoor(1000);
      }
    }
  }
}

void unlockDoor() {
  digitalWrite(pinSubEngine, HIGH);
  delay(500);
  digitalWrite(pinSubEngine, LOW);
}
void closeDoor(int time) {
  digitalWrite(pinOpenEngine, LOW);
  digitalWrite(pinCloseEngine, HIGH);
  delay(time);
  digitalWrite(pinCloseEngine, LOW);
}
void openDoor(int time) {
  digitalWrite(pinOpenEngine, HIGH);
  digitalWrite(pinCloseEngine, LOW);
  delay(time);
  digitalWrite(pinOpenEngine, LOW);
}

void setbalanceWeight() {
  weight += 10;
}
void setup() {
  Serial.begin(115200);
  pinMode(pinSubEngine, OUTPUT);
  pinMode(pinCloseEngine, OUTPUT);
  pinMode(pinOpenEngine, OUTPUT);

  balance.begin(pinBDT, pinBSCK);
  balance.set_scale(-871);
  balance.tare(20);

  pinMode(pinUltTriq, OUTPUT);
  pinMode(pinUltEcho, INPUT);

  // Sensor de obs
  pinMode(pinObs, INPUT);

  code_id = String(ESP.getEfuseMac(), HEX).substring(0, 6);
  EEPROM.begin(254);
  delay(1000);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_19dBm);

  String access_point_name = "PetMate-" + code_id.substring(2);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(access_point_name.c_str());
  WiFi.softAPsetHostname("PetMate");

  Serial.println("Access Point iniciado");
  Serial.print("IP del Access Point: ");
  Serial.println(WiFi.softAPIP());

  // Configura las rutas del servidor HTTP

  server.on("/wifi", wifiConfiguration);
  server.on("/scanNetworks", getWifiNetworks);

  server.begin();
  Serial.println("Servidor HTTP iniciado");
  Serial.println(code_id);
}

int getDepositPercentage() {
  digitalWrite(pinUltTriq, LOW);
  delayMicroseconds(2);
  digitalWrite(pinUltTriq, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinUltTriq, LOW);

  long duration = pulseIn(pinUltEcho, HIGH);

  int percentage = map((duration / 58.0) - 5, 0, 15, 100, 0);

  return percentage < 0 ? 0 : percentage > 100 ? 0
                                               : percentage;
}
void sendCurrentFood() {
  String jsonBody = "{\"food_served\":" + String(balance.get_units(10)) + ",\"food_storage\":" + String(getDepositPercentage()) + "}";

  // Configurar solicitud POST
  http.begin(backend_host + "/backend/pet-mate-app/public/api/feeders/" + feeder_id);
  http.addHeader("Content-Type", "application/json");

  // Enviar solicitud POST con cuerpo JSON
  int httpResponseCode = http.PUT(jsonBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println(response);
  } else {
    Serial.println("Error in the PUT request");
  }

  http.end();
}

void petEating()
{
  int count = 0;
  int free_space_count = 0;
  int old_weight = weight;
  while(free_space_count < 300)
  {
    if (analogRead(pinObs) < 500)
    {

    }
    else {
      
    }
    delay(100);
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!connected) {
      getId();
      get_feeding_time_counter = 59;
      setTimeNow();
      connected = 1;
    }

    if (get_feeding_time_counter == 60) {
      getFeedingTime();
      analyzeFeedingTimes();
      get_feeding_time_counter = 0;
    }
    if (food_counter == 60) {
      sendCurrentFood();
      food_counter = 0;
    }
    food_counter++;
    wifi_desconnected_counter = 0;
    get_feeding_time_counter++;
    if (analogRead(pinObs) < 500 && weight != balance.get_units(10))
    {
      Serial.println("The pet is eating...");
      petEating();
    }

  } else if (WiFi.status() != WL_CONNECTED) {
    if (connected) {
      connected = 0;
    }
    wifi_desconnected_counter++;
  }
  delay(1000);
  /*if (wifi_desconnected_counter == 600) {
    wifi_desconnected_counter = 0;
    EEPROM.get(0, ssid_wifi);
    EEPROM.get(128, password_wifi);
    Serial.println("Conectando...");
    Serial.println(ssid_wifi);
    Serial.println(password_wifi);
    WiFi.begin(ssid_wifi.c_str(), password_wifi.c_str());
  }*/
  server.handleClient();
}