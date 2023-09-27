#include <WiFi.h>
#include <ArduinoJson.h>
#include <DHTesp.h>
#include <PubSubClient.h>

// Configurações de WiFi
const char *SSID = "";
const char *PASSWORD = "";  // Substitua pelo sua senha

// Configurações de MQTT
const char *BROKER_MQTT = "broker.hivemq.com";
const int BROKER_PORT = 1883;
const char *ID_MQTT = "ESP32_IOT_APLICADA";
const char *TOPIC_SUBSCRIBE_LED = "led";
const char *TOPIC_PUBLISH_TEMP_HUMI = "temphumi";
const char *TOPIC_PUBLISH_LDR = "ldr";

// Configurações de Hardware
#define DHT_PIN 14 // sensor de umidade e temperatura
#define LED_PIN 15 // LED para simular start da irrigação
#define LDR_PIN 39 // sensor de luminosidade ldr
#define PUBLISH_DELAY 2000 // delay de publicação do mqtt
const float gama = 0.7; // Inclinação do gráfico log(R) / log(lx)
const float rl10 = 50; // Resistência LDR @ 10lx (em kilo-ohms)


// Variáveis globais
WiFiClient espClient;
PubSubClient MQTT(espClient);
DHTesp dht;
unsigned long publishUpdate = 0;
TempAndHumidity sensorValues;
const int bufferLength = 200;


// Protótipos de funções
void updateSensorValues();
void initWiFi();
void initMQTT();
void callbackMQTT(char *topic, byte *payload, unsigned int length);
void reconnectMQTT();
void reconnectWiFi();
void checkWiFIAndMQTT();
void readLDR();

void updateSensorValues() {
  sensorValues = dht.getTempAndHumidity();
}

// Inicia a conexão com o Wifi
void initWiFi() {
  Serial.print("Conectando com a rede: ");
  Serial.println(SSID);

  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println();
  Serial.print("Conectado com sucesso: ");
  Serial.println(SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// Inicia o MQTT Server e verifica as mensagens
void initMQTT() {
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(callbackMQTT);
}

// Verificar as mensagens que recebe no topico
void callbackMQTT(char *topic, byte *payload, unsigned int length) {
  String msg = String((char*)payload).substring(0, length);
  
  Serial.printf("Mensagem recebida via MQTT: %s do tópico: %s\n", msg.c_str(), topic);

  StaticJsonDocument<bufferLength> json;
  DeserializationError error = deserializeJson(json, msg);
  
  if (error) {
    Serial.println("Falha na deserialização do JSON.");
    return;
  }

  // Serial.println(msg);

  // Se tiver key de led ele verifica o valor para ligar ou desligar o LED
  if (json.containsKey("led")) {
    int valor = json["led"];
    if (valor == 1) {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("LED ligado pelo MQTT");
    } else if (valor == 0) {
      digitalWrite(LED_PIN, LOW);
      Serial.println("LED desligado pelo MQTT");
    }
  }
}

// Reconecta no MQTT
void reconnectMQTT() {
  while (!MQTT.connected()) {
    Serial.print("Tentando conectar com o Broker MQTT: ");
    Serial.println(BROKER_MQTT);

    if (MQTT.connect(ID_MQTT)) {
      Serial.println("Conectado ao broker MQTT!");
      MQTT.subscribe(TOPIC_SUBSCRIBE_LED);
    } else {
      Serial.println("Falha na conexão com MQTT. Tentando novamente em 2 segundos.");
      delay(2000);
    }
  }
}

// Verifica o Wifi e o MQTT
void checkWiFIAndMQTT() {
  if (WiFi.status() != WL_CONNECTED) reconnectWiFi();
  if (!MQTT.connected()) reconnectMQTT();
}

// Reconecta no Wifi
void reconnectWiFi(void){
  if (WiFi.status() == WL_CONNECTED)
    return;

  WiFi.begin(SSID, PASSWORD); // Conecta na rede WI-FI

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Wifi conectado com sucesso");
  Serial.print(SSID);
  Serial.println("IP: ");
  Serial.println(WiFi.localIP());
}

// Faz a leitura do sensor LDR (Luz)
void readLDR() {
  int ldrValue = analogRead(LDR_PIN); // leitura do ldr
  ldrValue = map(ldrValue, 4095, 0, 1024, 0); // Altere o valor de leitura do sensor LDR do valor Arduino ADC para o valor ESP32 ADC
  float voltage  = ldrValue / 1024.*3.3; // calcula a tensão referente a 3.3
  float resistance = 2000 * voltage / (1 - voltage / 3.3); // calculo da resistencia
  float lux = pow(rl10*1e3*pow(10,gama)/resistance,(1/gama)); // calculo de luminosidade
  // Serial.print("lux = ");
  // Serial.println(lux);

  // Transforma em JSON para a key ldr e o value lux
  StaticJsonDocument<bufferLength> doc;
  doc["ldr"] = lux;

  char buffer[bufferLength];
  serializeJson(doc, buffer);
  MQTT.publish(TOPIC_PUBLISH_LDR, buffer);
  Serial.println(buffer);
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  dht.setup(DHT_PIN, DHTesp::DHT11); // DTH22 | DHT11
  initWiFi();
  initMQTT();
}

void loop() {
  checkWiFIAndMQTT();
  MQTT.loop();

  // Se o tempo atual - o tempo de publicação for maior ou igual que o tempo de delay de publicação ele continua, para não enviar toda hora 
  if ((millis() - publishUpdate) >= PUBLISH_DELAY) {
    publishUpdate = millis();
    updateSensorValues();

    readLDR();

    // Se tiver valores dos sensores de temperatura e umidade ele faz o json e faz a publicação no mqtt
    if (!isnan(sensorValues.temperature) && !isnan(sensorValues.humidity)) {
      StaticJsonDocument<bufferLength> doc;
      doc["temperatura"] = sensorValues.temperature;
      doc["umidade"] = sensorValues.humidity;

      char buffer[bufferLength];
      serializeJson(doc, buffer);
      MQTT.publish(TOPIC_PUBLISH_TEMP_HUMI, buffer);
      Serial.println(buffer);
    }
  }
}

/** 
Referencia:

https://docs.wokwi.com/pt-BR/parts/wokwi-photoresistor-sensor
*/
