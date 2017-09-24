/*Bibliotecas necessárias
 * 
 * PubSubClient
 * ** Como instalar https://www.youtube.com/watch?v=yM1eljamBwM&list=PLxDLawCWayzCsoqDt7A7kBVDFVt0RSm1g&index=2
 * 
 * WiFiManager
 * 
 * COMANDOS MQTT
 * **** Os comandos devem ser enviados para o topico system/set/YOURHOSTAME ****
 * REBOOT - Faz restart ao ESP
 * OTA_ON - Liga o Modo OTA e publica uma mesagem no topico system/log/YOURHOSTAME "OTA SETUP ON"
 * OTA_OFF - Desliga o Modo OTA, este comando só necessita de ser utilizado caso o ESP não tenha feito restart
 * */

//MQTT
#include <PubSubClient.h>
//ESP
#include <ESP8266WiFi.h>
//Wi-Fi Manger library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>//https://github.com/tzapu/WiFiManager
//OTA
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#define AP_TIMEOUT 180
#define SERIAL_BAUDRATE 115200
#define MQTT_AUTH false
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define SENSOR A0
//CONSTANTS
const String HOSTNAME  = "MedidorConsumo";
const char * OTA_PASSWORD  = "otapower";
const String MQTT_LOG = "system/log/"+HOSTNAME;
const String MQTT_SYSTEM_CONTROL_TOPIC = "system/set/"+HOSTNAME;
const char * MQTT_AMPS_TOPIC = "current/messure/amperage";
const char * MQTT_WATTS_TOPIC = "current/messure/watts";
//MQTT BROKERS GRATUITOS PARA TESTES https://github.com/mqtt/mqtt.github.io/wiki/public_brokers
const char* MQTT_SERVER = "iot.eclipse.org";

int mVperAmp = 100; // 100 for 20A Module
double voltage = 0;
double vRMS = 0;
double ampsRMS = 0;

WiFiClient wclient;
PubSubClient client(MQTT_SERVER,1883,wclient);

//CONTROL FLAGS
bool OTA = false;
bool OTABegin = false;

long lastMsg = 0;

float getVPP() {
  float result;
  int readValue;             
  int maxValue = 0;         
  int minValue = 1024;          

  uint32_t start_time = millis();
  while ((millis() - start_time) < 500) { 
    readValue = analogRead(SENSOR);
    if (readValue > maxValue) {
      maxValue = readValue;
    }
    if (readValue < minValue) {
      minValue = readValue;
    }
  }
  result = ((maxValue - minValue) * 5.0) / 1024.0;
  return result;
}
void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  WiFiManager wifiManager;
  //reset saved settings
  //wifiManager.resetSettings();
  /*define o tempo limite até o portal de configuração ficar novamente inátivo,
   útil para quando alteramos a password do AP*/
  wifiManager.setTimeout(AP_TIMEOUT);
  wifiManager.autoConnect(HOSTNAME.c_str());
  client.setCallback(callback); 
  pinMode(SENSOR, INPUT);
}
//Chamada de recepção de mensagem 
void callback(char* topic, byte* payload, unsigned int length) {
  String payloadStr = "";
  for (int i=0; i<length; i++) {
    payloadStr += (char)payload[i];
  }
  String topicStr = String(topic);
 if(topicStr.equals(MQTT_SYSTEM_CONTROL_TOPIC)){
  if(payloadStr.equals("OTA_ON")){
    OTA = true;
    OTABegin = true;
  }else if (payloadStr.equals("OTA_OFF")){
    OTA = true;
    OTABegin = true;
  }else if (payloadStr.equals("REBOOT")){
    ESP.restart();
  }
 }
} 
  
bool checkMqttConnection(){
  if (!client.connected()) {
    if (MQTT_AUTH ? client.connect(HOSTNAME.c_str(),MQTT_USERNAME, MQTT_PASSWORD) : client.connect(HOSTNAME.c_str())) {
      //SUBSCRIÇÃO DE TOPICOS
      client.subscribe(MQTT_SYSTEM_CONTROL_TOPIC.c_str());
      Serial.println("MQTT CONNECTED");
    }else{
      Serial.println("MQTT ERROR");}
  }
  return client.connected();
}

void readSensorAndPublish(){
    voltage = getVPP();  //voltagem pico a pico
    vRMS = (voltage / 2.0) * 0.707; //voltage root mean square
    //0.707 (datasheet ACS712)
    ampsRMS = (vRMS * 1000) / mVperAmp; //calcula a corrente
    if (ampsRMS <= 0.09) ampsRMS = 0; //limpa ruido

    Serial.print(ampsRMS, 3);
    Serial.println(" Amps RMS");
    client.publish(MQTT_AMPS_TOPIC,String(ampsRMS,3).c_str(),true);
    float watts = ampsRMS * 240;

    Serial.print(watts);
    Serial.println(" Watts");
    //MQTT
    client.publish(MQTT_WATTS_TOPIC,String(watts,1).c_str(),true);
  
}
void loop() {
if (WiFi.status() == WL_CONNECTED) {
    if (checkMqttConnection()){
        
      client.loop();
      long now = millis();
    if (now - lastMsg > 5000) {
      lastMsg = now;  // de 5 em 5 segundos
      readSensorAndPublish();
      }
      if(OTA){
        if(OTABegin){
          setupOTA();
          OTABegin= false;
        }
        ArduinoOTA.handle();
      }
    }
  }
}

void setupOTA(){
  if (WiFi.status() == WL_CONNECTED && checkMqttConnection()) {
    client.publish(MQTT_LOG.c_str(),"OTA SETUP ON");
    ArduinoOTA.setHostname(HOSTNAME.c_str());
    ArduinoOTA.setPassword((const char *)OTA_PASSWORD);
    
    ArduinoOTA.onStart([]() {
    client.publish(MQTT_LOG.c_str(),"START");
  });
  ArduinoOTA.onEnd([]() {
    client.publish(MQTT_LOG.c_str(),"END");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    String p = "Progress: "+ String( (progress / (total / 100)));
    client.publish(MQTT_LOG.c_str(),p.c_str());
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) client.publish(MQTT_LOG.c_str(),"Auth Failed");
    else if (error == OTA_BEGIN_ERROR)client.publish(MQTT_LOG.c_str(),"Auth Failed"); 
    else if (error == OTA_CONNECT_ERROR)client.publish(MQTT_LOG.c_str(),"Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)client.publish(MQTT_LOG.c_str(),"Receive Failed");
    else if (error == OTA_END_ERROR)client.publish(MQTT_LOG.c_str(),"End Failed"); 
  });
 ArduinoOTA.begin();
 }  
}
