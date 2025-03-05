#include "StateMachineLib.h"
#include "AsyncTaskLib.h"
#include "DHT.h"

#define DHTPIN 4
#define LDRPIN 34
#define DHTTYPE DHT11
#define LED_PIN 5    // Pin para el LED de alarma

// Declaración de funciones para lectura de sensores, timeout y despliegue
void ReadTempFunct(void);
void ReadHumFunct(void);
void ReadLuzFunct(void);
void timeout(void);
void DisplayDatosF(void);

// Tareas asíncronas para los sensores y timeout
AsyncTask readTempTask(2500, true, ReadTempFunct);
AsyncTask readHumTask(3200, true, ReadHumFunct);
AsyncTask readLuzTask(1600, true, ReadLuzFunct);
AsyncTask timeoutTask(5000, false, timeout); // Se ajusta el intervalo en cada estado

// Tareas para parpadeo de LED en el estado de alarma
AsyncTask ledOnTask(700, []() { digitalWrite(LED_PIN, HIGH); });
AsyncTask ledOffTask(400, []() { digitalWrite(LED_PIN, LOW); });

// Definición de estados y entradas
enum State { 
  Monitoreo_Amb = 0, 
  Monitoreo_Luz = 1, 
  Alarma = 2 
};

enum Input { 
  Signt = 0,  // Señal de timeout o transición forzada
  Signl = 1,  // Señal del sensor LDR
  Signh = 2,  // Señal del sensor DHT11 (temperatura y humedad)
  Unknown = 3 
};

// Crear la máquina de estados
StateMachine stateMachine(4, 9);
Input input = Unknown;

// Variables globales para almacenar las lecturas de los sensores
float temperatura = 0;
float humedad = 0;
int luz = 0;

// Variables para control del despliegue por tiempo
unsigned long lastDisplayMillis = 0;
const unsigned long displayInterval = 2000; // Cada 2 segundos se muestra la información

// Instanciar el sensor DHT
DHT dht(DHTPIN, DHTTYPE);

// Prototipos de funciones para acciones al entrar/salir de cada estado
void initAmb(void);
void endAmb(void);
void initLuz(void);
void endLuz(void);
void initAlarma(void);
void endAlarma(void);

// Configurar la máquina de estados y sus transiciones
void setupStateMachine() {
  // En Monitoreo Ambiental:
  // - Si se cumple la condición DHT (temp>24 y hum>70) se pasa a Alarma.
  // - Si transcurre 5 s sin esa condición se pasa a Monitoreo de Luz.
  stateMachine.AddTransition(Monitoreo_Amb, Alarma, []() { return input == Signh; });
  stateMachine.AddTransition(Monitoreo_Amb, Monitoreo_Luz, []() { return input == Signt; });
  
  // En Monitoreo de Luz:
  // - Si el sensor LDR supera 500 se pasa a Alarma.
  // - Si transcurren 3 s sin esa condición se retorna a Monitoreo Ambiental.
  stateMachine.AddTransition(Monitoreo_Luz, Alarma, []() { return input == Signl; });
  stateMachine.AddTransition(Monitoreo_Luz, Monitoreo_Amb, []() { return input == Signt; });
  
  // En Alarma:
  // - Después de 6 s se retorna a Monitoreo Ambiental.
  stateMachine.AddTransition(Alarma, Monitoreo_Amb, []() { return input == Signt; });
  
  // Configurar las acciones al entrar y salir de cada estado
  stateMachine.SetOnEntering(Monitoreo_Amb, initAmb);
  stateMachine.SetOnLeaving(Monitoreo_Amb, endAmb);
  
  stateMachine.SetOnEntering(Monitoreo_Luz, initLuz);
  stateMachine.SetOnLeaving(Monitoreo_Luz, endLuz);
  
  stateMachine.SetOnEntering(Alarma, initAlarma);
  stateMachine.SetOnLeaving(Alarma, endAlarma);
}

// Acciones para el estado Monitoreo Ambiental
void initAmb() {
  Serial.println("----- Estado: Monitoreo Ambiental -----");
  readTempTask.Start();
  readHumTask.Start();
  // Timeout de 5 segundos para pasar a monitoreo de luz si no se cumple la condición
  timeoutTask.SetIntervalMillis(5000);
  timeoutTask.Start();
  // Asegurarse de que el LED esté apagado en este estado
  digitalWrite(LED_PIN, LOW);
}

void endAmb() {
  Serial.println("Saliendo de Monitoreo Ambiental");
  readTempTask.Stop();
  readHumTask.Stop();
  timeoutTask.Stop();
}

// Acciones para el estado Monitoreo de Luz
void initLuz() {
  Serial.println("----- Estado: Monitoreo de Luz -----");
  readLuzTask.Start();
  // Timeout de 3 segundos para retornar a ambiental si no se activa la alarma
  timeoutTask.SetIntervalMillis(3000);
  timeoutTask.Start();
}

void endLuz() {
  Serial.println("Saliendo de Monitoreo de Luz");
  readLuzTask.Stop();
  timeoutTask.Stop();
}

// Acciones para el estado de Alarma
void initAlarma() {
  Serial.println("----- Estado: Alarma -----");
  // Se inician las tareas para parpadear el LED
  ledOnTask.Start();
  ledOffTask.Start();
  // Timeout de 6 segundos para retornar a monitoreo ambiental
  timeoutTask.SetIntervalMillis(6000);
  timeoutTask.Start();
}

void endAlarma() {
  Serial.println("Saliendo de Alarma");
  // Se detienen las tareas del LED y se apaga
  ledOnTask.Stop();
  ledOffTask.Stop();
  digitalWrite(LED_PIN, LOW);
  timeoutTask.Stop();
}

// Funciones de lectura de sensores
void ReadTempFunct() {
  temperatura = dht.readTemperature();
  // Si estamos en monitoreo ambiental y se cumplen las condiciones, se activa la señal para alarma
  if (stateMachine.GetState() == Monitoreo_Amb && (temperatura > 24) && (humedad > 70)) {
    input = Signh;
  }
}

void ReadHumFunct() {
  humedad = dht.readHumidity();
  if (stateMachine.GetState() == Monitoreo_Amb && (temperatura > 24) && (humedad > 70)) {
    input = Signh;
  }
}

void ReadLuzFunct() {
  luz = analogRead(LDRPIN);
  if (stateMachine.GetState() == Monitoreo_Luz && luz > 500) {
    input = Signl;
  }
}

// Función de timeout para provocar la transición según el tiempo transcurrido
void timeout() {
  input = Signt;
}

// Función para mostrar datos según el estado actual
void DisplayDatosF() {
  State currentState = static_cast<State>(stateMachine.GetState());
  if (currentState == Monitoreo_Amb) {
    Serial.println("----- Monitoreo Ambiental -----");
    Serial.print("Temperatura: ");
    Serial.print(temperatura);
    Serial.println(" °C");
    Serial.print("Humedad: ");
    Serial.print(humedad);
    Serial.println(" %");
  } else if (currentState == Monitoreo_Luz) {
    Serial.println("----- Monitoreo de Luz -----");
    Serial.print("LDR: ");
    Serial.println(luz);
  } else if (currentState == Alarma) {
    Serial.println("----- ALARMA ACTIVA -----");
  }
  Serial.println();
}


void setup() {
  Serial.begin(9600);
  Serial.println("Iniciando la Máquina de Estados...");
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  setupStateMachine();
  // Estado inicial: Monitoreo Ambiental
  stateMachine.SetState(Monitoreo_Amb, false, true);
}

void loop() {
  // Actualizar tareas de sensores y timeout
  readTempTask.Update();
  readHumTask.Update();
  readLuzTask.Update();
  timeoutTask.Update();
  
  // Actualizar tareas de parpadeo del LED (solo activas en estado Alarma)
  ledOnTask.Update(ledOffTask);
  ledOffTask.Update(ledOnTask);
  
  // Actualizar la máquina de estados
  stateMachine.Update();
  
  // Mostrar datos cada 'displayInterval' milisegundos, según el estado actual
  if (millis() - lastDisplayMillis >= displayInterval) {
    DisplayDatosF();
    lastDisplayMillis = millis();
  }
  
  // Reiniciar la señal para evitar transiciones repetitivas
  input = Unknown;
}
