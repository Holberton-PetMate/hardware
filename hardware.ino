#include <esp_system.h>
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
JsonArray feeding_times;
int weight = 0;

WebServer server(80);  // Crea un servidor HTTP en el puerto 80

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
  Serial.println("----TIME");
  Serial.println(result);
  Serial.println("-----TI");
  return result;
}

String methodHTTP(String host, String url, String body = "") {
  String result = {};
  HTTPClient http;

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
  Serial.println(result);
  DeserializationError error = deserializeJson(response, result);
  if (error) {
    Serial.print("Error al analizar la cadena de caracteres JSON: ");
    Serial.println(error.c_str());
  }
  return response;
}
void getId() {
  feeder_id = getRequestMethod(backend_host, "/backend/pet-mate-app/public/api/feeders/check_redeemed?code_id=" + code_id)["id"];
  Serial.println(feeder_id);
}
void getFeedingTime() {
  feeding_times = getRequestMethod(backend_host, "/backend/pet-mate-app/public/api/feeders/" + String(feeder_id) + "/feeding_times").as<JsonArray>();
}
void setTimeNow() {
  JsonObject date = getRequestMethod("https://api.ipgeolocation.io", get_time_zone_url, "", 1).as<JsonObject>();
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
  int n = WiFi.scanNetworks();
  Serial.println("Redes WiFi encontradas:");
  DynamicJsonDocument networks(1024);
  for (int i = 0; i < n && i < 7; ++i) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    Serial.print(i);
  }
  serializeJson(networks, networks_dict);
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
      int old_weight = weight;
      Serial.println("Depositando");
      Serial.print(feeding_time["weight"].as<int>());
      while (feeding_time["weight"].as<int>() > weight) {
        setBalanzaWeight();
        Serial.println(weight);
        delay(100);
      }
      Serial.println("SE Deposito ");
      Serial.print(weight - old_weight);
      Serial.print("g");
    }
  }
}

void setBalanzaWeight() {
  weight += 10;
}
void setup() {
  Serial.begin(115200);
  code_id = String(ESP.getEfuseMac(), HEX);
  EEPROM.begin(254);
  delay(1000);

  WiFi.mode(WIFI_AP_STA);
  String access_point_name = "PetMate-" + code_id.substring(8);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(access_point_name.c_str(), "123456789", 6);
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
    wifi_desconnected_counter = 0;
    get_feeding_time_counter++;

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