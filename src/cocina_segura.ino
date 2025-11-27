#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Servo.h>

// ====== OLED ======
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool displayOK = false;

// ====== Pines sensores ======
const int PIN_MQ2      = A0;  // MQ-2 AOUT
const int PIN_PIR      = 2;   // PIR simulando sensor de flama
const int PIN_DHT      = 3;   // DHT22 DATA
const int PIN_DS18B20  = 4;   // DS18B20 DQ

// ====== Pines actuadores ======
const int PIN_RELE     = 5;
const int PIN_SERVO    = 6;
const int PIN_BUZZER   = 7;

// ====== Pines botones (INPUT_PULLUP) ======
const int BTN_SILENCIO = 8;
const int BTN_RESET    = 12;
const int BTN_PRUEBA   = 13;
const int BTN_MENU     = A1;

// ====== LED RGB (común cátodo) ======
const int PIN_LED_R    = 9;
const int PIN_LED_G    = 10;
const int PIN_LED_B    = 11;

// ====== DHT22 ======
#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

// ====== DS18B20 ======
OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);

// ====== Servo ======
Servo servoGas;
const int SERVO_ABIERTO = 90; // gas abierto
const int SERVO_CERRADO = 0;  // gas cerrado

// ====== Estados de riesgo ======
enum Riesgo {
  RIESGO_NINGUNO,
  RIESGO_LLAMA,
  RIESGO_GAS,
  RIESGO_TEMP_ZONA,
  RIESGO_TEMP_AMBIENTE
};

// ====== Umbrales (se ajustan en pruebas) ======
const int   MQ2_UMBRAL      = 400; // valor analógico
const float TEMP_ZONA_MAX   = 80.0;
const float TEMP_AMB_MAX    = 35.0;
const float HUM_AMB_MAX     = 80.0;

// ====== Variables globales de medición ======
int   mq2Valor   = 0;
bool  hayLlama   = false;
float tempAmb    = 0;
float humAmb     = 0;
float tempZona   = 0;

// ====== Control de estado ======
Riesgo riesgoActual = RIESGO_NINGUNO;
bool buzzerSilenciado = false;
unsigned long tiempoSilencio = 0;
const unsigned long DURACION_SILENCIO_MS = 15000; // 15 s

// ====== Prototipos ======
void leerSensores();
Riesgo evaluarRiesgo();
void aplicarAcciones(Riesgo r);
void actualizarDisplay(Riesgo r);
void manejarBotones();
void modoPrueba();
void setLEDColor(uint8_t r, uint8_t g, uint8_t b);

// ====== SETUP ======
void setup() {
  Serial.begin(9600);

  pinMode(PIN_MQ2, INPUT);
  pinMode(PIN_PIR, INPUT);

  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);

  pinMode(BTN_SILENCIO, INPUT_PULLUP);
  pinMode(BTN_RESET,    INPUT_PULLUP);
  pinMode(BTN_PRUEBA,   INPUT_PULLUP);
  pinMode(BTN_MENU,     INPUT_PULLUP);

  dht.begin();
  ds18b20.begin();

  servoGas.attach(PIN_SERVO);
  servoGas.write(SERVO_ABIERTO);   // estado normal

  digitalWrite(PIN_RELE, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  setLEDColor(0, 255, 0);          // verde = normal

  displayOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (displayOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Cocina Segura");
    display.println("Inicializando...");
    display.display();
  }
}

// ====== LOOP ======
void loop() {
  leerSensores();
  manejarBotones();

  Riesgo nuevoRiesgo = evaluarRiesgo();
  riesgoActual = nuevoRiesgo;

  aplicarAcciones(riesgoActual);
  actualizarDisplay(riesgoActual);

  // tiempo de silencio del buzzer
  if (buzzerSilenciado && millis() - tiempoSilencio > DURACION_SILENCIO_MS) {
    buzzerSilenciado = false;
  }

  delay(500); // ritmo de actualización
}

// ====== Implementación de funciones ======

void leerSensores() {
  mq2Valor = analogRead(PIN_MQ2);
  hayLlama = digitalRead(PIN_PIR) == HIGH;

  tempAmb = dht.readTemperature();
  humAmb  = dht.readHumidity();

  ds18b20.requestTemperatures();
  tempZona = ds18b20.getTempCByIndex(0);

  Serial.print("MQ2: "); Serial.print(mq2Valor);
  Serial.print("  Llama: "); Serial.print(hayLlama);
  Serial.print("  T_amb: "); Serial.print(tempAmb);
  Serial.print("  HR: "); Serial.print(humAmb);
  Serial.print("  T_zona: "); Serial.println(tempZona);
}

Riesgo evaluarRiesgo() {
  if (hayLlama) {
    return RIESGO_LLAMA;
  }

  if (mq2Valor > MQ2_UMBRAL) {
    return RIESGO_GAS;
  }

  if (tempZona > TEMP_ZONA_MAX) {
    return RIESGO_TEMP_ZONA;
  }

  if (tempAmb > TEMP_AMB_MAX || humAmb > HUM_AMB_MAX) {
    return RIESGO_TEMP_AMBIENTE;
  }

  return RIESGO_NINGUNO;
}

void aplicarAcciones(Riesgo r) {
  bool buzzerOn = !buzzerSilenciado;

  switch (r) {
    case RIESGO_LLAMA:
      digitalWrite(PIN_RELE, HIGH);
      if (buzzerOn) digitalWrite(PIN_BUZZER, HIGH);
      setLEDColor(255, 0, 0);  // rojo
      servoGas.write(SERVO_CERRADO);
      break;

    case RIESGO_GAS:
      digitalWrite(PIN_RELE, HIGH);
      if (buzzerOn) digitalWrite(PIN_BUZZER, HIGH);
      setLEDColor(255, 0, 0);  // rojo
      servoGas.write(SERVO_CERRADO);
      break;

    case RIESGO_TEMP_ZONA:
      digitalWrite(PIN_RELE, HIGH);
      digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
      setLEDColor(255, 255, 0); // amarillo
      servoGas.write(SERVO_ABIERTO);
      break;

    case RIESGO_TEMP_AMBIENTE:
      digitalWrite(PIN_RELE, HIGH);
      digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
      setLEDColor(200, 200, 0); // amarillo suave
      servoGas.write(SERVO_ABIERTO);
      break;

    case RIESGO_NINGUNO:
    default:
      digitalWrite(PIN_RELE, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      setLEDColor(0, 255, 0); // verde
      servoGas.write(SERVO_ABIERTO);
      break;
  }
}

void actualizarDisplay(Riesgo r) {
  if (!displayOK) return;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);

  display.println("Cocina Segura");

  display.print("MQ2: ");   display.println(mq2Valor);
  display.print("T_amb: "); display.print(tempAmb); display.println(" C");
  display.print("HR: ");    display.print(humAmb);  display.println(" %");
  display.print("T_zona: ");display.print(tempZona);display.println(" C");

  display.print("Alarma: ");
  switch (r) {
    case RIESGO_LLAMA:         display.println("FUEGO"); break;
    case RIESGO_GAS:           display.println("GAS/FUGA"); break;
    case RIESGO_TEMP_ZONA:     display.println("Sobre T zona"); break;
    case RIESGO_TEMP_AMBIENTE: display.println("Ventilacion"); break;
    default:                   display.println("Normal"); break;
  }

  if (buzzerSilenciado) {
    display.println("Buzzer: SILENCIO");
  }

  display.display();
}

void manejarBotones() {
  if (digitalRead(BTN_SILENCIO) == LOW) {
    buzzerSilenciado = true;
    tiempoSilencio = millis();
  }

  if (digitalRead(BTN_RESET) == LOW) {
    buzzerSilenciado = false;
    riesgoActual = RIESGO_NINGUNO;
  }

  if (digitalRead(BTN_PRUEBA) == LOW) {
    modoPrueba();
  }

  // BTN_MENU por ahora no hace nada (para futuras mejoras)
}

void modoPrueba() {
  digitalWrite(PIN_RELE, HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  setLEDColor(255, 0, 255); // magenta

  if (displayOK) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("MODO PRUEBA");
    display.display();
  }

  delay(2000);

  digitalWrite(PIN_RELE, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  setLEDColor(0, 255, 0);
}

void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
  analogWrite(PIN_LED_B, b);
}
