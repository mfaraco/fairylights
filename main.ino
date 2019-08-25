/*******************************************************************************
* ESP8266 MQTT implementation for Fairy lights and Room Control
* Author: mfaraco@gmail.com
* 
*******************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Streaming.h>
#include <ArduinoOTA.h>
#include "config.h"

#define VERSION                 "0.1"    // Software version

// Wifi
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// MQTT
const char* mqtt_server            = MQTT_SERVER;
const int   mqtt_port              = MQTT_PORT;
const char* mqtt_username          = MQTT_USER;
const char* mqtt_password          = MQTT_PASSWORD;

// Channels
const char* mqtt_topic_agus_light = "lights/agus/light";
const char* mqtt_topic_agus_temperature = "sensors/agus/temperature";
const char* mqtt_topic_infra_agus_status = "infrastructure/agus/status";
const char* mqtt_topic_infra_agus_ip      = "infrastructure/agus/ip";
const char* mqtt_topic_infra_agus_version = "infrastructure/agus/version";
const char* mqtt_topic_infra_agus_chipid  = "infrastructure/agus/chipid";

// Global Variables
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

// Thermometer
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

char temperatureString[6];

// Timmers
unsigned long previousMillis = 0; // Timmer flag, when was the last update sent?
const long send_interval = 60000; // Interval between sending

/*******************************************************************************
 *  Setup
*******************************************************************************/
void setup() {
  // Initialize Outputs
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);   
  pinMode(FAIRYLIGHTS, OUTPUT);    
 
  Serial.begin(115200);
 
  // Turn both leds off
  digitalWrite(LED_RED, HIGH); 
  digitalWrite(LED_BLUE, HIGH);
  analogWrite(FAIRYLIGHTS, LOW);
  //Set PWM frequency 500, default is 1000
  //Set range 0~100, default is 0~1023
  analogWriteFreq(500);
  analogWriteRange(100);
  
  // Setup Comms
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  // setup OneWire bus
  sensors.begin();

  // Setup OTA Client
  ArduinoOTA.setPassword((const char *)OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("Start updating ");
    blink_data();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    blink_data();
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
    blink_data();
    blink_data();
  });
  ArduinoOTA.begin();

}

/*******************************************************************************
 *  Setup Wifi connection
 *  Blue led blinking = Setup in progress
 *  Blue led ON = Setup complete
*******************************************************************************/
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Set Wifi mode to only connect and not advertiser as AP
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  digitalWrite(LED_BLUE, HIGH); //Blue led 0FF Connecting
  
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BLUE, HIGH); 
    delay(300);
    Serial.print(".");
    digitalWrite(LED_BLUE, LOW); 
    delay(200);
  
  }

  Serial.println("");
  Serial.println("WiFi connected");
  
  // Echo local IP on the console for Debugging
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_BLUE, LOW); //Blue led 0N = Connected 
}

/*******************************************************************************
 *  Blink Red led when data is received over MQTT
*******************************************************************************/
void blink_data() {
  digitalWrite(LED_RED, LOW);   // Turn the LED on (Note that LOW is the voltage level)
  delay(100);                   // Wait for a second
  digitalWrite(LED_RED, HIGH);  // Turn the LED off by making the voltage HIGH
  delay(100);                   // Wait for two seconds (to demonstrate the active low LED)
  digitalWrite(LED_RED, LOW);   // Turn the LED on (Note that LOW is the voltage level
  delay(100);                   // Wait for a second
  digitalWrite(LED_RED, HIGH);  // Turn the LED off by making the voltage HIGH
  delay(100);                   // Wait for two seconds (to demonstrate the active low LED)
}

/*******************************************************************************
 *  Read OneWire Temperature Sensor
 *  Returns Temperature in Celsius OR -127 as Error
*******************************************************************************/
float getTemperature(int index) {
  Serial << "Requesting DS18B20 temperature for sensor " << index+1 << " ..." << endl;
  float temp;
  int count; // count to 10 read errors and send -127.0
  count = 0;
  do {
    sensors.requestTemperatures(); 
    temp = sensors.getTempCByIndex(index);
    Serial << "Read Temperature for sensor " << index + 1 <<": " << temp << endl;
    delay(100);
    if (count == 100){
      temp = -127.0;
      break;
    }
    count++;
  } while (temp == 85.0 || temp == (-127.0));
  return temp;
}

/*******************************************************************************
 * Process MQTT Channel payload + pin number and changes Relay Status
 * 1 = ON, 0 = OFF
*******************************************************************************/
void process_payload(byte* payload, int pin, unsigned int length){
    // Switch on the Relay if an 1 was received as first character
    //Serial.println((char)payload[0]);
    char buffer[128];
    // Make sure here that `length` is smaller than the above buffer size. 
    // Otherwise, you'd need a bigger buffer

    // Form a C-string from the payload
    memcpy(buffer, payload, length);
    buffer[length] = '\0';

    // Convert it to integer
    char *end = nullptr;
    long value = strtol(buffer, &end, 10);

    analogWrite(pin, value);   // Set PWM Brightness
    Serial.println(value);
  }

/*******************************************************************************
 *  Callback listening to MQTT Messages for subscribed channels
*******************************************************************************/
void callback(char* topic, byte* payload, unsigned int length) {
  Serial  << "Message arrived ["  << topic << "] "  << endl;
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  String strTopic = String((char*)topic);
  blink_data();
  
  // Depending on the topic that received the message activate relays
  if (strTopic == mqtt_topic_agus_light) {
      Serial.print("Fairy Lights ");
      process_payload(payload, FAIRYLIGHTS,length);
  }
  else { 
      Serial.println("No match");
    }
  delay(1);        // delay in between reads for stability
}

/*******************************************************************************
 * Reconnects to wifi
*******************************************************************************/
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("lightsClient", mqtt_username, mqtt_password)) {
      Serial.println("connected");
      
      // Send Device Information to Openhab
      char ip[15];
      WiFi.localIP().toString().toCharArray(ip,15);
      client.publish(mqtt_topic_infra_agus_ip, ip);
      client.publish(mqtt_topic_infra_agus_version, VERSION);
      client.publish(mqtt_topic_infra_agus_status, "ON");
      client.publish(mqtt_topic_infra_agus_chipid, String(ESP.getChipId(), DEC).c_str());
      
      // Send Unknown as Temp until next read
      client.publish(mqtt_topic_agus_temperature, "unknown");
  
      
      // Resubscribe
      client.subscribe(mqtt_topic_agus_light);
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= send_interval) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;

    blink_data();
    float temperature = 0;
    
    temperature = getTemperature(0);
    // convert temperature to a string with two digits before the comma and 2 digits for precision
    dtostrf(temperature, 2, 2, temperatureString);
    // send temperature to the serial console
    Serial << "Sending temperature: " << temperatureString << endl;
    // send temperature to the MQTT topic
    client.publish(mqtt_topic_agus_temperature, temperatureString);
  }
  /*int i;
  for(i=0; i<100; i++){
      analogWrite(FAIRYLIGHTS, i);
      delay(10);
    }
  for(i=100; i>0; i--){
    analogWrite(FAIRYLIGHTS, i);
    delay(10);
  }*/
  ArduinoOTA.handle();
}