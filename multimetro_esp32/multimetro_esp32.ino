/*
  Voltímetro + Capacímetro + Continuidad + Ohmetro + Frecuencímetro +
  Amperímetro con menú de selección de modo - ESP32
  (solo monitor serial, sin LCD)

  Cómo funciona:
  - Al iniciar (o al volver del menú) se muestra un menú por Serial.
  - Escribes "1" y Enter para entrar a modo VOLTAJE -> activa el relay
    de GPIO23, que conecta la punta positiva al circuito del voltímetro.
  - Escribes "2" y Enter para entrar a modo CAPACITANCIA -> activa el
    relay de GPIO22, que conecta la punta positiva al circuito RC del
    capacímetro.
  - Escribes "3" y Enter para entrar a modo CONTINUIDAD -> deja
    apagado (des-energizado) el relay de GPIO19 (el mismo relay del
    ohmetro: la punta de continuidad quedo cableada fisicamente al
    contacto normalmente cerrado (NC) del relay de RESISTENCIA, ya no
    tiene relay propio, y el NC solo queda conectado cuando el relay
    esta apagado). Mientras haya continuidad (PROBE_PIN en LOW gracias
    al pull-up interno), el buzzer (BUZZER_PIN) suena. En cuanto se
    pierde la continuidad, se apaga de inmediato.
  - Escribes "4" y Enter para entrar a modo RESISTENCIA (OHMETRO) ->
    activa el relay de GPIO19, que habilita el circuito divisor del
    ohmetro. El propio código prueba las escalas de referencia
    (100/1K/10K/100K) de menor a mayor, de forma automática, hasta
    encontrar una lectura válida (rango total: 0-100K ohms).
  - Escribes "5" y Enter para entrar a modo FRECUENCIA -> activa el
    relay de GPIO18, que conecta la punta positiva al circuito de
    acondicionamiento de frecuencia (divisor + comparador LM358), cuya
    salida entrega a GPIO25 una onda cuadrada lista para contar. La
    frecuencia se calcula por conteo reciproco (tiempo exacto entre el
    primer y el ultimo pulso de cada ciclo, mas preciso que contar
    pulsos en una ventana fija).
  - Escribes "6" y Enter para entrar a modo CORRIENTE (AMPERIMETRO) ->
    activa el relay de GPIO17, que conecta la punta positiva a un
    shunt de bajo valor (0.5 ohm por defecto) referenciado a GND
    (medicion de "bajo lado", igual que en la imagen de referencia:
    Arduino - Different Ways for Measuring Current - Shunt
    Measurement). GPIO39 lee el voltaje en la union entre la punta+ y
    el shunt; la corriente se calcula con Ley de Ohm (I = V/R_shunt).
  - Estando dentro de cualquiera de los modos, escribe "x" o "9"
    y Enter en cualquier momento para apagar relays y buzzer, cancelar
    de inmediato lo que se esté haciendo (incluso a mitad de una carga
    o descarga del capacímetro) y regresar al menú.

  IMPORTANTE (hardware): cada relay debe manejarse con su propio
  transistor (ej. TIP21C) + resistencia de base + diodo flyback en
  paralelo con la bobina. NUNCA conectes la bobina de un relay directo
  a un GPIO.

  IMPORTANTE (continuidad): PROBE_PIN --[resistencia 1k, protección]--
  punta de prueba A; GND -- punta de prueba B. El buzzer es ACTIVO, se
  controla con digitalWrite (HIGH = suena, LOW = apagado).

  IMPORTANTE (ohmetro): GPIO34-39 son solo entrada en el ESP32, por
  eso ohmAnalogPin (35) no se reutiliza para nada más. Se evitan los
  pines de strapping (0, 2, 12, 15) y los de Serial (1, 3) para los
  canales de referencia (13, 14, 26, 27). relayResistenciaPin (GPIO19)
  hace el papel de "apply_voltage": habilita la alimentación del
  circuito divisor mientras se está en modo ohmetro (relay energizado,
  contacto NA). Es el mismo relay que ahora comparte la punta de
  continuidad (modo 3), pero en el contacto NC: por eso el modo
  continuidad deja el relay apagado en vez de encenderlo.

  IMPORTANTE (frecuencia): la punta+ se comparte con los otros modos a
  traves del relay relayFrecuenciaPin (GPIO18). Cuando ese relay
  cierra, conecta la punta+ a un divisor de voltaje (R1=20k, R2=10k,
  limite 10V de amplitud con offset positivo) seguido de un comparador
  LM358 con histeresis (Vcc/2 de referencia, feedback de 100k), cuya
  salida limpia (cuadrada, sin importar la forma de onda de entrada)
  llega a GPIO25.

  IMPORTANTE (corriente): medicion de "bajo lado" (low-side), igual
  que en la imagen de referencia. El circuito bajo prueba (ej. un
  motor alimentado por una fuente externa) debe compartir GND con el
  ESP32. Se rompe el retorno (GND) de ese circuito y se inserta el
  shunt ahi: la punta+ del multimetro se conecta a la union entre el
  retorno del circuito y el shunt; el otro extremo del shunt va al GND
  comun. Cadena completa:

    Carga bajo prueba -- retorno -- punta+ -- relay GPIO17 --
      -- (nodo, GPIO39 lee aqui) -- shunt (0.5 ohm) -- GND comun

  ADVERTENCIA: a diferencia de los otros modos, aqui la CORRIENTE REAL
  de la carga pasa por los contactos del relay (no solo corriente de
  prueba de unos mA). Usa un relay clasificado para la corriente
  maxima que vayas a medir. El shunt de 0.5 ohm es un valor de partida
  (igual al de la imagen de referencia) pensado para corrientes del
  orden de cientos de mA a pocos amperios; para corrientes muy
  pequeñas (decenas de mA) la caida de voltaje sera muy chica y con
  poca resolucion -- en ese caso conviene un shunt de mayor valor o
  una etapa de amplificacion (INA219, INA219 con I2C, etc.).
*/

// ---------------------------------------------------------------
// ---------------------- RELAYS DE MODO ---------------------------
// ---------------------------------------------------------------
// relayModo3Pin (continuidad, GPIO21) y relayAuxPin (GPIO16) se
// eliminaron fisicamente. La punta de continuidad quedo en el
// contacto NC de relayResistenciaPin (ver comentarios de cabecera).
// relayCorrientePin (GPIO17) sigue siendo su propio relay dedicado.
#define relayVoltajePin      23   // Controla el relay que conecta la punta+ al voltímetro
#define relayCapacitanciaPin 22   // Controla el relay que conecta la punta+ al capacímetro
#define relayResistenciaPin  19   // Relay ENCENDIDO (NA) -> divisor del ohmetro. Relay APAGADO (NC) -> circuito de continuidad
#define relayFrecuenciaPin   18   // Controla el relay que conecta la punta+ al circuito de frecuencia
#define relayCorrientePin    17   // Controla el relay que conecta la punta+ al shunt de corriente

// Con transistor NPN (TIP21C) manejando el relay: HIGH en el GPIO
// satura el transistor -> energiza el relay. Si tu circuito quedara
// invertido, cambia esta bandera a false.
const bool RELAY_ACTIVO_EN_ALTO = true;

void relayEncender(int pin) {
  digitalWrite(pin, RELAY_ACTIVO_EN_ALTO ? HIGH : LOW);
}

void relayApagar(int pin) {
  digitalWrite(pin, RELAY_ACTIVO_EN_ALTO ? LOW : HIGH);
}

void apagarTodosLosRelays() {
  relayApagar(relayVoltajePin);
  relayApagar(relayCapacitanciaPin);
  relayApagar(relayResistenciaPin);
  relayApagar(relayFrecuenciaPin);
  relayApagar(relayCorrientePin);
}

// ---------------------------------------------------------------
// ---------------------- MÁQUINA DE ESTADOS ------------------------
// ---------------------------------------------------------------
enum Modo { MENU_PRINCIPAL, MODO_VOLTAJE, MODO_CAPACITANCIA, MODO_3, MODO_4, MODO_5, MODO_6 };
Modo modoActual = MENU_PRINCIPAL;
bool menuMostrado = false;

// ---------------------------------------------------------------
// ---------------------- VOLTÍMETRO ------------------------------
// ---------------------------------------------------------------
bool debug = true;
const int R1 = 68000;
const int R2 = 10000;

const int VinPin = 34;      // Entrada analógica del divisor de voltaje

const float Vref = 3.3;
const int adcMax = 4095;

const int numPuntos = 10;
float V_medido[numPuntos] = {0.05, 1.42, 4.23, 6.57, 8.78, 11.03, 12.867, 13.74, 16.50, 18.04};
float V_real[numPuntos]   = {1.00, 3.00, 5.40, 7.80, 10.1, 12.40, 14.30, 15.20, 17.60, 19.60};

const int grado = 2;
float coef[grado + 1];

float VB1;
int mV;

// ---------------------------------------------------------------
// -------------------- CAPACÍMETRO -------------------------------
// ---------------------------------------------------------------
#define analogPinCap   36
#define chargePin      32
#define dischargePin   33
#define resistorValue  9989.0F

unsigned long startTime;
unsigned long elapsedTime;
float microFarads;
float nanoFarads;

const int umbralADC = 2588;

// Tiempo máximo de espera (ms) para carga/descarga, evita que el
// ESP32 se quede colgado (y se reinicie por watchdog) si no hay
// capacitor conectado o el circuito está abierto.
const unsigned long TIMEOUT_MS = 5000;

// ---------------------------------------------------------------
// -------------------- CONTINUIDAD (MODO 3) ------------------------
// ---------------------------------------------------------------
const int PROBE_PIN  = 4;    // GPIO con pull-up interno -> sensor de continuidad
const int BUZZER_PIN = 5;    // GPIO de salida -> activa el buzzer (ACTIVO)

const unsigned long CONT_DEBOUNCE_MS = 30; // filtra rebotes al tocar las puntas

bool contEstadoAnterior = false;
unsigned long contUltimoCambio = 0;

// ---------------------------------------------------------------
// -------------------- OHMETRO (MODO 4) -----------------------------
// ---------------------------------------------------------------
// Auto-rango: se prueban las escalas de menor a mayor (100/1K/10K/100K)
// hasta encontrar una lectura valida. Rango total del ohmetro: 0-100K.
// El relay relayResistenciaPin hace de "apply_voltage": habilita la
// alimentacion del circuito divisor mientras estemos en este modo
// (igual que los demas relays).
//
// IMPORTANTE (hardware): GPIO34-39 son solo entrada en el ESP32; por
// eso ohmAnalogPin (35) no se reutiliza para nada mas. Se evitan los
// pines de strapping (0, 2, 12, 15) y los de Serial (1, 3) para los
// canales de referencia.
const int ohmAnalogPin = 35;   // ADC1, solo entrada -> punto medio del divisor de resistencia
const int ohmCh100  = 13;      // conecta la resistencia de referencia de 100 ohms
const int ohmCh1K   = 14;      // conecta la resistencia de referencia de 1K
const int ohmCh10K  = 26;      // conecta la resistencia de referencia de 10K
const int ohmCh100K = 27;      // conecta la resistencia de referencia de 100K

// Por debajo de este valor la lectura es demasiado baja para la escala
// actual (R1 muy chica frente a la resistencia medida) y se prueba la
// siguiente escala.
const int ADC_LOW_THRESHOLD_OHM = 40;

// Cada rango tiene su propio polinomio de calibracion (grado 3):
//   R_calibrada = calA*R^3 + calB*R^2 + calC*R + calD
// Por defecto calA=calB=0 y calC=1, calD=0 -> no hace nada (pasa el
// valor tal cual). Para calibrar: mide resistencias de valor conocido
// con este ohmetro, ajusta un polinomio (Rmedido -> Rreal) y copia los
// coeficientes aqui.
struct RangeOhm {
  int         pin;          // pin que conecta la resistencia de referencia
  float       R1_ohms;      // valor real de la resistencia de referencia, en OHMS
  const char* label;        // etiqueta de la escala
  float       unitDivisor;  // 1000 = mostrar en K ohms, 1000000 = mostrar en M ohms
  const char* unit;         // "K" o "M"
  float       calA, calB, calC, calD; // coeficientes cubico, cuadratico, lineal, independiente
};

// NOTA: R1_ohms usa el valor nominal de cada resistencia (100/1K/10K/100K).
// Para mejor precision, reemplaza cada valor por el que midas con un
// ohmetro de referencia sobre la resistencia real que soldaste (igual
// que se hacia antes con los rangos de 2K/20K/200K/1M).
RangeOhm rangesOhm[] = {
  { ohmCh100,  100.0f,    "0 - 100",    1000.0f, "K", 0.0f, 0.0f, 1.0f, 0.0f },
  { ohmCh1K,   1000.0f,   "100 - 1K",   1000.0f, "K", 0.0f, 0.0f, 1.0f, 0.0f },
  { ohmCh10K,  10000.0f,  "1K - 10K",   1000.0f, "K", 0.0f, 0.0f, 1.0f, 0.0f },
  { ohmCh100K, 100000.0f, "10K - 100K", 1000.0f, "K", 0.0f, 0.0f, 1.0f, 0.0f },
};
const int numRangesOhm = sizeof(rangesOhm) / sizeof(rangesOhm[0]);

// Prototipo manual: evita que el IDE de Arduino genere su propio
// prototipo automatico al inicio del archivo (donde RangeOhm todavia
// no existe), lo cual rompe la compilacion.
float calibrarOhmetro(float r, const RangeOhm &rg);

// ---------------------------------------------------------------
// -------------------- FRECUENCIA (MODO 5) --------------------------
// ---------------------------------------------------------------
// Comparte la punta+ con los otros modos a traves de relayFrecuenciaPin
// (GPIO18). Requiere el circuito externo descrito en el comentario de
// cabecera (divisor 20k/10k + comparador LM358) entre el contacto de
// salida del relay y GPIO25.
const int freqInputPin = 25;
const unsigned long freqGateMs = 1000; // cada cuanto se revisa si ya hay suficientes pulsos

// Filtro de ruido/rebote: respaldo extra ademas del LM358.
const unsigned long FILTRO_RUIDO_US = 10;

volatile uint32_t freqPulseCount = 0;
volatile unsigned long freqPrimerPulsoUs = 0;
volatile unsigned long freqUltimoPulsoUs = 0;

unsigned long freqLastCheck = 0;
float frequency = 0;

// IRAM_ATTR asegura que el codigo quede en RAM (requisito de las ISR en ESP32).
void IRAM_ATTR contarPulsoFrecuencia() {
  unsigned long ahoraUs = micros();

  if (ahoraUs - freqUltimoPulsoUs < FILTRO_RUIDO_US && freqPulseCount > 0) {
    return; // ruido cerca del cruce, se ignora
  }

  if (freqPulseCount == 0) {
    freqPrimerPulsoUs = ahoraUs;
  }
  freqUltimoPulsoUs = ahoraUs;
  freqPulseCount++;
}

// ---------------------------------------------------------------
// -------------------- CORRIENTE (MODO 6) -----------------------------
// ---------------------------------------------------------------
// Medicion de bajo lado (low-side) con shunt, igual que en la imagen
// de referencia (Arduino - Different Ways for Measuring Current -
// Shunt Measurement): el shunt va entre la punta+ (union con el
// retorno de la carga) y GND comun. GPIO39 lee el voltaje en esa
// union; la corriente se calcula con Ley de Ohm.
const int ampAnalogPin = 39;      // ADC1, solo entrada -> union punta+/shunt
const float R_SHUNT_OHMS = 0.5f;  // valor del shunt (ajusta al que realmente uses)

// Calibracion lineal simple (ajustar si hace falta, igual idea que en
// el voltimetro pero mas simple): corriente_calibrada = corriente*GAIN + OFFSET
const float CORRIENTE_CAL_GAIN = 1.0f;
const float CORRIENTE_CAL_OFFSET = 0.0f;

// ---------------------------------------------------------------
// -------------------------- SETUP -------------------------------
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); // habilita lectura de 0 a ~3.3V (usado por el ohmetro)

  pinMode(relayVoltajePin, OUTPUT);
  pinMode(relayCapacitanciaPin, OUTPUT);
  pinMode(relayResistenciaPin, OUTPUT);
  pinMode(relayFrecuenciaPin, OUTPUT);
  pinMode(relayCorrientePin, OUTPUT);
  apagarTodosLosRelays();

  calcularCoeficientes();
  Serial.println("Coeficientes de calibracion (c0 + c1*x + c2*x^2 + ...):");
  for (int i = 0; i <= grado; i++){
    Serial.print("c"); Serial.print(i); Serial.print(" = ");
    Serial.println(coef[i], 6);
  }

  pinMode(chargePin, OUTPUT);
  digitalWrite(chargePin, LOW);

  pinMode(PROBE_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(ohmAnalogPin, INPUT);
  allRangesOffOhm();

  pinMode(freqInputPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(freqInputPin), contarPulsoFrecuencia, RISING);
  freqLastCheck = millis();

  pinMode(ampAnalogPin, INPUT);
}

// ---------------------------------------------------------------
// -------------------------- LOOP --------------------------------
// ---------------------------------------------------------------
void loop() {
  switch (modoActual) {

    case MENU_PRINCIPAL:
      if (!menuMostrado) {
        mostrarMenu();
        menuMostrado = true;
      }
      if (Serial.available()) {
        char c = Serial.read();
        if (c == '1') {
          apagarTodosLosRelays();
          relayEncender(relayVoltajePin);
          Serial.println(">> Entrando a modo VOLTAJE. Escribe 'x' o '9' para cancelar y salir al menu.");
          modoActual = MODO_VOLTAJE;
          menuMostrado = false;
        } else if (c == '2') {
          apagarTodosLosRelays();
          relayEncender(relayCapacitanciaPin);
          Serial.println(">> Entrando a modo CAPACITANCIA. Escribe 'x' o '9' para cancelar y salir al menu.");
          modoActual = MODO_CAPACITANCIA;
          menuMostrado = false;
        } else if (c == '3') {
          apagarTodosLosRelays(); // continuidad usa el NC del relay de resistencia -> se deja apagado
          contEstadoAnterior = false;
          digitalWrite(BUZZER_PIN, LOW);
          Serial.println(">> Entrando a modo CONTINUIDAD. Escribe 'x' o '9' para cancelar y salir al menu.");
          modoActual = MODO_3;
          menuMostrado = false;
        } else if (c == '4') {
          apagarTodosLosRelays();
          relayEncender(relayResistenciaPin);
          Serial.println(">> Entrando a modo RESISTENCIA (OHMETRO). Escribe 'x' o '9' para cancelar y salir al menu.");
          modoActual = MODO_4;
          menuMostrado = false;
        } else if (c == '5') {
          apagarTodosLosRelays();
          relayEncender(relayFrecuenciaPin);
          noInterrupts();
          freqPulseCount = 0;
          interrupts();
          freqLastCheck = millis();
          Serial.println(">> Entrando a modo FRECUENCIA (relay GPIO18 -> GPIO25). Escribe 'x' o '9' para cancelar y salir al menu.");
          modoActual = MODO_5;
          menuMostrado = false;
        } else if (c == '6') {
          apagarTodosLosRelays();
          relayEncender(relayCorrientePin);
          Serial.println(">> Entrando a modo CORRIENTE (relay GPIO17 -> shunt -> GPIO39). Escribe 'x' o '9' para cancelar y salir al menu.");
          modoActual = MODO_6;
          menuMostrado = false;
        }
      }
      break;

    case MODO_VOLTAJE:
      if (salirSiSePide()) break;
      readVoltage();
      Serial.print("VB1 Voltaje: ");
      Serial.print(VB1);
      Serial.println("V");
      delay(500);
      break;

    case MODO_CAPACITANCIA:
      if (salirSiSePide()) break;
      readCapacitance();
      if (modoActual != MODO_CAPACITANCIA) break; // se canceló durante la medición
      Serial.println("Descargando...");
      delay(500);
      break;

    case MODO_3:
      if (salirSiSePide()) break;
      checkContinuidad();
      break;

    case MODO_4:
      if (salirSiSePide()) break;
      readResistencia();
      delay(500);
      break;

    case MODO_5:
      if (salirSiSePide()) break;
      leerFrecuencia();
      break;

    case MODO_6:
      if (salirSiSePide()) break;
      readCorriente();
      delay(500);
      break;
  }
}

// Revisa si el usuario escribió 'x' o '9' para cancelar y salir del
// modo actual. Si es así, apaga relays, regresa al menú y devuelve true.
bool salirSiSePide() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'x' || c == 'X' || c == '9') {
      cancelarYRegresarAlMenu();
      return true;
    }
  }
  return false;
}

// Apaga relays, corta la carga/descarga en curso, apaga el buzzer y
// regresa al menú.
void cancelarYRegresarAlMenu() {
  apagarTodosLosRelays();
  digitalWrite(chargePin, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  contEstadoAnterior = false;
  allRangesOffOhm();
  noInterrupts();
  freqPulseCount = 0;
  interrupts();
  Serial.println(">> Cancelado. Saliendo al menu principal.");
  modoActual = MENU_PRINCIPAL;
  menuMostrado = false;
}

void mostrarMenu() {
  Serial.println();
  Serial.println("========= MENU =========");
  Serial.println("1) Modo Voltaje");
  Serial.println("2) Modo Capacitancia");
  Serial.println("3) Modo Continuidad (relay GPIO19 apagado/NC, compartido con Resistencia)");
  Serial.println("4) Modo Resistencia / Ohmetro (relay GPIO19 encendido/NA)");
  Serial.println("5) Modo Frecuencia (relay GPIO18)");
  Serial.println("6) Modo Corriente / Amperimetro (relay GPIO17)");
  Serial.println("Escribe el numero y Enter");
  Serial.println("=========================");
}

// ---------------------------------------------------------------
// -------------------- FUNCIONES VOLTÍMETRO -----------------------
// ---------------------------------------------------------------
void calcularCoeficientes(){
  const int n = grado + 1;
  double A[n][n + 1];

  for (int j = 0; j < n; j++){
    for (int k = 0; k < n; k++){
      double suma = 0;
      for (int i = 0; i < numPuntos; i++){
        suma += pow((double)V_medido[i], j + k);
      }
      A[j][k] = suma;
    }
    double sumaB = 0;
    for (int i = 0; i < numPuntos; i++){
      sumaB += pow((double)V_medido[i], j) * (double)V_real[i];
    }
    A[j][n] = sumaB;
  }

  for (int i = 0; i < n; i++){
    double pivote = A[i][i];
    for (int j = 0; j <= n; j++) A[i][j] /= pivote;
    for (int k = 0; k < n; k++){
      if (k != i){
        double factor = A[k][i];
        for (int j = 0; j <= n; j++){
          A[k][j] -= factor * A[i][j];
        }
      }
    }
  }

  for (int i = 0; i < n; i++){
    coef[i] = A[i][n];
  }
}

float calibrar(float valor){
  float resultado = coef[grado];
  for (int i = grado - 1; i >= 0; i--){
    resultado = resultado * valor + coef[i];
  }
  return resultado;
}

void readVoltage(){
  long sum = 0;
  const int samples = 20;
  for (int i = 0; i < samples; i++){
    sum += analogRead(VinPin);
    delayMicroseconds(200);
  }
  int adcValue = sum / samples;

  float Vout = (adcValue * Vref) / adcMax;
  float VB1_sinCalibrar = Vout * (R1 + R2) / R2;

  VB1 = calibrar(VB1_sinCalibrar);
  mV = VB1 * 1000;

  if (debug) {
    Serial.print("ADC crudo: ");
    Serial.print(adcValue);
    Serial.print(" | Sin calibrar: ");
    Serial.print(VB1_sinCalibrar, 3);
    Serial.print("V | ");
  }
}

// ---------------------------------------------------------------
// -------------------- FUNCIONES CAPACÍMETRO -----------------------
// ---------------------------------------------------------------
void readCapacitance(){
  // --- Carga ---
  digitalWrite(chargePin, HIGH);
  startTime = micros();
  unsigned long inicioEspera = millis();
  while (analogRead(analogPinCap) < umbralADC) {
    if (hayCancelacionPendiente()) { cancelarYRegresarAlMenu(); return; }
    if (millis() - inicioEspera > TIMEOUT_MS) {
      Serial.println("Timeout esperando carga (revisa el capacitor/conexion).");
      digitalWrite(chargePin, LOW);
      return;
    }
  }
  elapsedTime = micros() - startTime;
  microFarads = ((float)elapsedTime / resistorValue);

  if (microFarads > 1) {
    Serial.print("Capacitancia: ");
    Serial.print(microFarads, 3);
    Serial.println(" uF");
  } else {
    nanoFarads = microFarads * 1000.0;
    Serial.print("Capacitancia: ");
    Serial.print(nanoFarads, 3);
    Serial.println(" nF");
  }

  // --- Descarga ---
  digitalWrite(chargePin, LOW);
  pinMode(dischargePin, OUTPUT);
  digitalWrite(dischargePin, LOW);
  inicioEspera = millis();
  while (analogRead(analogPinCap) > 0) {
    if (hayCancelacionPendiente()) {
      pinMode(dischargePin, INPUT);
      cancelarYRegresarAlMenu();
      return;
    }
    if (millis() - inicioEspera > TIMEOUT_MS) {
      Serial.println("Timeout esperando descarga.");
      break;
    }
  }
  pinMode(dischargePin, INPUT);
}

// Revisa (sin bloquear) si llegó 'x' o '9' por Serial mientras se
// está a mitad de una medición de capacitancia.
bool hayCancelacionPendiente() {
  if (Serial.available()) {
    char c = Serial.peek();
    if (c == 'x' || c == 'X' || c == '9') {
      Serial.read(); // consume el caracter
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------
// -------------------- FUNCIONES CONTINUIDAD -----------------------
// ---------------------------------------------------------------
// No bloqueante: se llama en cada vuelta del loop mientras se está en
// MODO_3. Lee el pin de prueba, aplica debounce y prende/apaga el
// buzzer solo cuando el estado realmente cambió.
void checkContinuidad() {
  bool continuidad = (digitalRead(PROBE_PIN) == LOW);

  if (continuidad != contEstadoAnterior && millis() - contUltimoCambio > CONT_DEBOUNCE_MS) {
    contUltimoCambio = millis();
    contEstadoAnterior = continuidad;

    digitalWrite(BUZZER_PIN, continuidad ? HIGH : LOW);
    Serial.println(continuidad ? "Continuidad: SI -> buzzer ON" : "Continuidad: NO -> buzzer OFF");
  }
}

// ---------------------------------------------------------------
// -------------------- FUNCIONES OHMETRO -----------------------------
// ---------------------------------------------------------------
// Desconecta todas las resistencias de referencia (las deja en alta
// impedancia) para no cargar el divisor entre una medicion y otra.
void allRangesOffOhm() {
  pinMode(ohmCh100,  INPUT);
  pinMode(ohmCh1K,   INPUT);
  pinMode(ohmCh10K,  INPUT);
  pinMode(ohmCh100K, INPUT);
}

// Aplica el polinomio de calibracion propio de cada rango a una
// resistencia medida.
float calibrarOhmetro(float r, const RangeOhm &rg) {
  return rg.calA * r * r * r + rg.calB * r * r + rg.calC * r + rg.calD;
}

// Auto-rango: prueba las escalas de menor a mayor hasta encontrar una
// lectura valida e imprime el resultado por Serial. Si ninguna escala
// da una lectura utilizable (puntas abiertas / nada conectado), no
// imprime nada para no saturar la terminal.
void readResistencia() {
  for (int i = 0; i < numRangesOhm; i++) {
    allRangesOffOhm();
    pinMode(rangesOhm[i].pin, OUTPUT);
    digitalWrite(rangesOhm[i].pin, LOW);

    delay(15); // tiempo de asentamiento del divisor de voltaje

    int raw = analogRead(ohmAnalogPin);
    bool esUltimoRango = (i == numRangesOhm - 1);

    if (raw < ADC_LOW_THRESHOLD_OHM) {
      if (esUltimoRango) break; // se evaluo todo el banco, no hay nada que reportar
      continue; // pasa a probar el siguiente rango (R1 mayor)
    }

    float Vout    = (raw * Vref) / (float)adcMax;
    float bufferV = (Vref / Vout) - 1.0f;
    float R2_ohms = rangesOhm[i].R1_ohms * bufferV;
    R2_ohms = calibrarOhmetro(R2_ohms, rangesOhm[i]);

    Serial.print("----[");
    Serial.print(rangesOhm[i].label);
    Serial.println("]----");
    Serial.print("Lectura ADC: ");
    Serial.println(raw);
    Serial.print("Resistencia: ");
    Serial.print(R2_ohms / rangesOhm[i].unitDivisor, 3);
    Serial.print(" ");
    Serial.print(rangesOhm[i].unit);
    Serial.println(" ohms");

    if (raw >= adcMax - 40) {
      Serial.println("(Nota: cerca del limite de esta escala, valor aproximado)");
    }
    Serial.println();
    break;
  }

  allRangesOffOhm();
}

// ---------------------------------------------------------------
// -------------------- FUNCIONES FRECUENCIA (MODO 5) -----------------
// ---------------------------------------------------------------
// No bloqueante: cada freqGateMs revisa si ya se junto suficiente
// informacion (al menos 2 pulsos) para calcular la frecuencia con el
// metodo reciproco (tiempo exacto entre el primer y el ultimo pulso).
// Si la señal es muy lenta y todavia no hay 2 pulsos, sigue esperando
// en el siguiente ciclo, sin imprimir un valor a medias.
void leerFrecuencia() {
  unsigned long ahora = millis();
  if (ahora - freqLastCheck < freqGateMs) {
    return;
  }
  freqLastCheck = ahora;

  noInterrupts();
  uint32_t pulsos = freqPulseCount;
  unsigned long primero = freqPrimerPulsoUs;
  unsigned long ultimo = freqUltimoPulsoUs;
  interrupts();

  if (pulsos < 2) {
    return; // todavia no hay suficientes pulsos, se espera al siguiente ciclo
  }

  unsigned long tiempoTotalUs = ultimo - primero;
  if (tiempoTotalUs == 0) {
    return; // evita division por cero en un caso extremo
  }

  frequency = (float)(pulsos - 1) * 1000000.0f / (float)tiempoTotalUs;

  noInterrupts();
  freqPulseCount = 0;
  interrupts();

  // Formato "F,<valor>" para que sea facil de leer desde una app en
  // Python (por ejemplo, graficar en tiempo real con
  // graficar_frecuencia.py).
  Serial.print("F,");
  Serial.println(frequency, 3);
}

// ---------------------------------------------------------------
// -------------------- FUNCIONES CORRIENTE (MODO 6) -------------------
// ---------------------------------------------------------------
// Lee el voltaje en la union punta+/shunt y calcula la corriente con
// Ley de Ohm: I = V / R_shunt. Promedia varias muestras para reducir
// ruido, igual que el voltimetro.
void readCorriente(){
  long sum = 0;
  const int samples = 20;
  for (int i = 0; i < samples; i++){
    sum += analogRead(ampAnalogPin);
    delayMicroseconds(200);
  }
  int adcValue = sum / samples;

  float Vshunt = (adcValue * Vref) / adcMax;
  float corrienteA = (Vshunt / R_SHUNT_OHMS) * CORRIENTE_CAL_GAIN + CORRIENTE_CAL_OFFSET;

  Serial.print("ADC crudo: ");
  Serial.print(adcValue);
  Serial.print(" | Vshunt: ");
  Serial.print(Vshunt, 4);
  Serial.print("V | Corriente: ");
  if (fabs(corrienteA) >= 1.0f) {
    Serial.print(corrienteA, 4);
    Serial.println(" A");
  } else {
    Serial.print(corrienteA * 1000.0f, 2);
    Serial.println(" mA");
  }
}
