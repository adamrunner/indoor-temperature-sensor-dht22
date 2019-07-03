/*
  MQTT Temperature Client
  Hardware Setup:
  DHT22 Connected on Pin 0 / D3

  OTA Uploads - default port of 8266
  TEMP_SERVER - server IP
  MQTT_PORT   - default of 1883

  Reads temperature every 5m and posts it to the "outTopic" topic on the MQTT Server
    - message has this format HOSTNAME,TEMP_F (e.g. ESP_299021,36.84)
    - HOSTNAME is used to identify each sensor uniquely
    - HOSTNAME is based off of the MAC addr of each sensor
  Subscribes to the TEMP_REQ topic
    - when a 1 is received
    - it sends an immediate update from the temperature sensor

  TODO: Give the topics better names - inTopic / outTopic are awful
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WifiCreds.h>
#include "DHT.h"
#define DHTPIN 0
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#include "message_data.h"

#define MQTT_PORT 1883
uint8_t MAC_array[6];
char MAC_char[18];
#define MAX_AWAKE_MS 10 * 1500 // 15,000ms 15s
#define TEMP_MESSAGE_INTERVAL_MS 300000 // 300,000ms 300s 5m
#define TEMP_READ_INTERVAL_MS 5000 // 5s

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastTempMessageSentAt = 0;
unsigned long lastTempReadAt        = 0;

bool result;
bool firstBoot;
char msg[50];
char currentHostname[14];


float getTemp(){
  float temp;
  temp = dht.readTemperature(true);
  return temp;
}


void setup() {
  firstBoot = true;
  Serial.begin(115200);
  setup_wifi();
  client.setServer(TEMP_SERVER, MQTT_PORT);
  client.setCallback(callback);
  // Setup OTA Uploads
  ArduinoOTA.setPassword((const char *)"boarding");

  ArduinoOTA.onStart([]() {
    Serial.println("STARTING OTA UPDATE");
    char updateMessage[36]= "OTA UPDATE START - ";
    strcat(updateMessage, currentHostname);
    client.publish("debugMessages", updateMessage);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nCOMPLETED OTA UPDATE");
    char updateMessage[37]= "OTA UPDATE FINISH - ";
    strcat(updateMessage, currentHostname);
    client.publish("debugMessages", updateMessage);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

}

void setup_wifi() {

  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  WiFi.mode(WIFI_STA);
  Serial.println(MY_SSID);
  WiFi.begin(MY_SSID, MY_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  // Store the hostname to use later for MQTT ID
  String hostnameString = WiFi.hostname();
  hostnameString.toCharArray(currentHostname, 14);
  Serial.print("Hostname: ");
  Serial.println(WiFi.hostname());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  payload[length] = '\0';
  char *payloadS = (char *) payload;

  if (strcmp(topic, "TEMP_REQ") == 0){
    if ((char)payload[0] == '1') {
      float temp    = 0.000;
      float humid   = 0.000;
      float heatIdx = 0.000;
      MessageData data;
      data.hostname = currentHostname;

      while(invalidTempReading(temp) && millis() > (lastTempReadAt + TEMP_READ_INTERVAL_MS) ){
        // read the temperature from the sensor constantly
        // if there isnt a good reading
        temp    = dht.readTemperature(true);
        humid   = dht.readHumidity();
        heatIdx = dht.computeHeatIndex(temp, humid, true);
        lastTempReadAt = millis();
        Serial.println("### In Callback ###");
        Serial.print("Temp: ");
        Serial.println(temp);
        Serial.print("Humidity: ");
        Serial.println(humid);
        Serial.print("Heat Index: ");
        Serial.println(heatIdx);

      }

      sendUpdate(data, temp, humid, heatIdx);
    }
  }
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(currentHostname)) {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("SET_INTERVAL");
      client.subscribe("TEMP_REQ");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
bool sendTempUpdate(float temp){
  char temperature[7];
  dtostrf(temp,4,2,temperature);
  sprintf(msg, "%s,%s", currentHostname, temperature);

  bool result = sendMessage("outTopic", msg);
  return result;
}

bool invalidTempReading(float temp){
  if(isnan(temp)){
    return true;
  }
  bool result = (temp < -50.0 || temp > 120.0);
  return result;
}

bool sendMessage(char* topic, char* message){
  reconnect();
  client.loop();

  Serial.println("Sending..");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);
  bool result = client.publish(topic, message);
  Serial.print("Result: ");
  Serial.println(result);
  client.loop();
  return result;
}

bool sendMessage_v2(MessageData data)
{
  char message[72];
  sprintf(message, "HOSTNAME:%s,TEMP:%s,HUMIDITY:%s,HEAT_INDEX:%s", data.hostname, data.temperature, data.humidity, data.heatIndex);
  bool result = sendMessage("data", message);
  return result;
}

bool sendUpdate(MessageData data, float temp, float humidity, float heatIndex)
{
  bool result;
  dtostrf(heatIndex,4,2,data.heatIndex);
  dtostrf(humidity,4,2,data.humidity);
  dtostrf(temp,4,2,data.temperature);
  sendMessage_v2(data);
  result = sendTempUpdate(temp);
  // continue with v1
  delay(1500);
  return result;
}

bool timeToSendUpdate(){
  bool result = millis() > (lastTempMessageSentAt + TEMP_MESSAGE_INTERVAL_MS);
  return result;
}

void loop(){
  result = false;
  ArduinoOTA.handle();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  float temp    = 255.0000;
  float humid   = 255.0000;
  float heatIdx = 255.0000;
  MessageData data;
  data.hostname = currentHostname;

  while(invalidTempReading(temp) && millis() > (lastTempReadAt + TEMP_READ_INTERVAL_MS) ){
    // read the temperature from the sensor constantly
    // if there isnt a good reading
    temp    = dht.readTemperature(true);
    humid   = dht.readHumidity();
    heatIdx = dht.computeHeatIndex(temp, humid, true);

    if (isnan(humid) || isnan(temp) || isnan(heatIdx)) {
      Serial.println("Failed to read from DHT sensor!");
      delay(10000);
    }else{
      lastTempReadAt = millis();
      Serial.print("Temp: ");
      Serial.println(temp);
      Serial.print("Humidity: ");
      Serial.println(humid);
      Serial.print("Heat Index: ");
      Serial.println(heatIdx);
    }
  }
  // if it's time to send an update and we've got a valid temperature
  // send the update
  if((firstBoot || timeToSendUpdate()) && !invalidTempReading(temp) ){
    result    = sendUpdate(data, temp, humid, heatIdx);
    firstBoot = false;
  }
  if(result){
    lastTempMessageSentAt = millis();
  }
}