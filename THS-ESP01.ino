// раздефайнить или задефайнить для использования
#define DEBUG_ENABLE

#ifdef DEBUG_ENABLE
#define DEBUG(x) Serial.println(x)
#else
#define DEBUG(x)
#endif

#include <MQTT.h>
#include <PubSubClient.h>
#include <PubSubClient_JSON.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <ESP8266WiFi.h>
#include <config.h>
#include <GyverFilters.h>

//------------------------------------------------------------------------

bool debug = false;  //Отображение отладочной информации в серийный порт

#define DHTPIN 0             // Пин подключения датчика влажности и температуры
#define RELAYPIN 2          // Пин подключения реле
#define DHTTYPE DHT22         // DHT 22  (AM2302) Тип датчика влажности
#define REPORT_INTERVAL 30000 // Интервал отправки данных брокеру
#define BUFFER_SIZE 200       // Размер буфера для получения сообщения 

const char* ssid = "Password";        //Имя WIFI сети
const char* password = "bdcPVN5786";  //Пароль WIFI
const char* device1 = "Switch01";      //Имя управляемого устройства №1

String topic = "/sensors/dht1";                   //Топик для отправки
String debug_topic = "/debug";        //Топик отладочной информации
String sub_topic = "/homebridge/to/set"; //Топик подписки
char* hellotopic = "/hello_topic";       //Топик приветствия
char message_buff[2048];                //Размер буфера для принятого сообщения

IPAddress mqtt_server(192, 168, 1, 31);     //Первый сервер MQTT
IPAddress mqtt_server2(95, 174, 107, 100);  //Второй сервер MQTT
//String mqtt_server = "iot.eff-t.ru";
int mqtt_port = 1883;                       //Порт MQTT сервера

unsigned long currentTime;    //Переменная для преобразования времени работы модуля
unsigned long currentUtimeReport;
int err_conn = 0;             //Счетчик ошибок подключения к MQTT серверу

float h, filteredH;          //Значение влажности
float t, filteredT;          //Значение температуры
float oldH;        //Предыдущее значение влажности
float oldT;        //Предыдущее значение температуры
String clientName;  //Имя клиента
//========================================================================

DHT dht(DHTPIN, DHTTYPE);
WiFiClient wifiClient;
PubSubClient client(wifiClient);
GKalman lightFilter(20, 20, 1);
GKalman tempFilter(0.2, 0.2, 0.1);
GKalman humiFilter(2, 2, 1);

void setup() {
  #ifdef DEBUG_ENABLE
    Serial.begin(115200);
  #endif
  Serial.println();
  Serial.println();
  Serial.println("Start system!");
  delay(20);

  Serial.println("Initialization Relay PIN");
  pinMode(RELAYPIN, OUTPUT);
  digitalWrite(RELAYPIN, HIGH);

  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  clientName += "esp01-";
  uint8_t mac[6];
  WiFi.macAddress(mac);
  clientName += macToStr(mac);

  Serial.print("Connecting to ");
  Serial.print(mqtt_server);
  Serial.print(" as ");
  Serial.println(clientName);
  //Подключаемся к MQTT серверу
  while (!client.connected()) {
    mqtt_connect();
  }
  dht.begin();
  oldH = -1;
  oldT = -1;

  filteredT = tempFilter.filtered(dht.readTemperature());
  filteredH = humiFilter.filtered(dht.readHumidity());
}

void loop() {

  if (client.connected()) {
    if (millis() - currentTime > REPORT_INTERVAL) // Если время контроллера millis, больше переменной на REPORT_INTERVAL, то запускаем условие if
    {
      currentTime = millis();        // Приравниваем переменную текущего времени к времени контроллера, чтобы через REPORT_INTERVAL опять сработал наш цикл.
      sendTemperature();
    }
     client.loop();
  }
  else {
    while (!client.connected()) {
      mqtt_connect();
    }
  }
}

void sendTemperature() {

  h = dht.readHumidity();
  t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    Serial.println(t);
    Serial.println(h);
    return;
  }

  filteredT = round(tempFilter.filtered(t) * 10) / 10;
  filteredH = round(humiFilter.filtered(h) * 10) / 10;

  String payload = "{\"id\":\"";
  payload += clientName;
  payload += "\",\"uptime\":\"";
  payload += uptime();
  payload += "\",\"humi\":\"";
  payload += filteredH;
  payload += "\",\"temp\":\"";
  payload += filteredT;
  payload += "\"}";
  
    Serial.print("OLD T: ");
    Serial.print(oldT, 6);
    Serial.print(" ; OLD H: ");
    Serial.println(oldH, 6);
    Serial.print("fltrT: ");
    Serial.print(filteredT, 6);
    Serial.print(" ; fltrH: ");
    Serial.println(filteredH, 6);

  if (filteredT != oldT || filteredH != oldH)  {
    if (client.connected()) {
      Serial.print("Client connected OK! ");

      if (client.publish(topic, (char*) payload.c_str())) {
        Serial.print("Publish ");
        Serial.print(topic);
        Serial.print(" ");
        Serial.println(payload.c_str());
      }
      else {
        Serial.println("Publish failed");
      }
    }
    oldT = filteredT;
    oldH = filteredH;
  }
  else {
    Serial.println("there is no new data to send");
  }
  Serial.println("--------------- End data send----------------");
}

String uptime() {
  long currentSeconds = millis() / 1000;
  int hours = currentSeconds / 3600;
  int minutes = (currentSeconds / 60) % 60;
  int seconds = currentSeconds % 60;
  String report = ((hours < 10) ? "0" + String(hours) : String(hours));
  report += ":";
  report += ((minutes < 10) ? "0" + String(minutes) : String(minutes));
  report += ":";
  report += ((seconds < 10) ? "0" + String(seconds) : String(seconds));
  return report;
}

void callback(const MQTT::Publish & pub) {


  String payload = pub.payload_string();
  if (payload != "") {
    if (String(pub.topic()) == sub_topic) {

      DynamicJsonDocument doc(2048);

      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }

      const char* device_name = doc["name"]; // "Switch1"
      const char* service_name = doc["service_name"]; // "Switch 1"
      const char* service = doc["service"]; // "Outlet"
      const char* characteristic = doc["characteristic"]; // "On"
      bool value = doc["value"]; // true|false

      if (strcmp (device_name, device1) == 0) {
        debug = value;
        digitalWrite(RELAYPIN,((value == true) ? LOW : HIGH));
        Serial.println((value == true) ? "ОТЛАДКА ВКЛЮЧЕНА" : "ОТЛАДКА ОТКЛЮЧЕНА");
      }
    }
  }
  else
  {
    if (debug)
      Serial.println("Empty payload");
  }
}

String macToStr(const uint8_t* mac){
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}
