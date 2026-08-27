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
    relay de GPIO18, que conecta la punta positiva a un divisor de
    voltaje simple (R1=1K, R2=10K, sin comparador) cuyo nodo llega a
    GPIO25. Pensado para un generador de funciones de hasta 3.3V de
    amplitud y frecuencia maxima de referencia de 10KHz (ver comentario
    de cabecera "IMPORTANTE (frecuencia)"). La frecuencia se calcula
    por conteo reciproco (tiempo exacto entre el primer y el ultimo
    pulso de cada ciclo, mas preciso que contar pulsos en una ventana
    fija).
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

  IMPORTANTE (control web): ademas del Monitor Serial, el ESP32 crea su
  propia red WiFi (Access Point, ver AP_SSID/AP_PASSWORD) y sirve una
  pagina web con botones para los mismos 6 modos. Conectate a esa red
  desde el celular/PC y entra a http://192.168.4.1 (o http://multimetro.local).
  Los comandos que llegan por WebSocket usan los mismos caracteres
  ('1'-'6', 'x'/'9') y alimentan la misma maquina de estados que el
  Serial, asi que ambos caminos de control quedan siempre sincronizados.
  El modo Frecuencia se dibuja como un osciloscopio (onda cuadrada
  sintetizada en el navegador a partir del Hz medido) -- ver nota en
  "IMPORTANTE (frecuencia)" sobre por que no se captura la forma de
  onda real.

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
  cierra, conecta la punta+ a un divisor de voltaje simple -- SIN
  comparador -- de R1=1K (en serie, hacia la punta+ del generador de
  funciones) y R2=10K (a GND comun); el nodo entre ambas va a GPIO25.
  Vout = Vin * 10/11, o sea con 3.3V de entrada llega ~3.0V al pin,
  bien arriba del umbral digital del ESP32 (a diferencia del divisor
  20K/10K anterior, que con una fuente de solo 3.3V dejaba apenas
  ~1.1V -- insuficiente para disparar la interrupcion).

  Al no haber comparador, la limpieza de la señal depende de que el
  generador entregue onda CUADRADA (flancos rapidos); con senoidal o
  triangular, un cruce lento por el umbral digital puede generar
  disparos falsos. FILTRO_RUIDO_US se subio a 30us como mitigacion por
  software, calculado para la frecuencia maxima de referencia de este
  modo (10KHz, periodo de 100us) -- deja margen amplio (30% del
  periodo) para rechazar ruido sin recortar pulsos reales. Si mides
  frecuencias mas altas, hay que bajar ese valor.

  IMPORTANTE (frecuencia / osciloscopio web): GPIO25 es un canal ADC2
  del ESP32, y el ADC2 no se puede leer de forma confiable mientras el
  WiFi esta activo (limitacion del propio chip, comparten hardware
  interno). Por eso la pagina web NO muestrea la forma de onda real
  por ADC -- sigue usando el conteo de flancos digitales (que no
  depende del ADC y ya es mas preciso que un muestreo) para calcular
  el Hz, y el navegador dibuja una onda cuadrada sintetica que se
  mueve en tiempo real a esa frecuencia. Se ve y se comporta como un
  osciloscopio en vivo, sin pelear contra la limitacion de ADC2+WiFi.

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
// ---------------------- WIFI / SERVIDOR WEB ------------------------
// ---------------------------------------------------------------
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>

// El ESP32 crea su propia red WiFi (Access Point) en vez de conectarse
// a una existente. Edita el nombre/password si quieres, la contraseña
// debe tener minimo 8 caracteres (requisito de WPA2).
const char* AP_SSID     = "Multi";
const char* AP_PASSWORD = "multimetro123";

// Con esto puesto, entras por http://multimetro.local en vez de
// tener que recordar la IP (normalmente 192.168.4.1 en modo AP).
const char* MDNS_HOSTNAME = "multimetro";

// WebServer (sincrono, incluido en el core de ESP32) + WebSocketsServer
// (Links2004, tambien sincrona) en vez de ESPAsyncWebServer/AsyncTCP --
// mas simples y sin el conflicto de locking de lwIP que dio tantos
// problemas con la version async. El unico costo: hay que llamar
// server.handleClient() y webSocket.loop() seguido en loop() para que
// no se sientan "colgadas" durante las esperas bloqueantes que ya
// tiene el firmware (delay(500), timeout de hasta 5s del capacimetro).
// El WebSocket corre en el puerto 81 (WebSocketsServer no comparte
// puerto con el HTTP como si hacia AsyncWebSocket).
WebServer server(80);
WebSocketsServer webSocket(81);

// Ultimo comando recibido por WebSocket, pendiente de procesar
// (0 = no hay ninguno). comandoPendiente() lo combina con
// Serial.available() para que Serial y la pagina web alimenten la
// misma maquina de estados sin duplicar logica.
volatile char wsComandoPendiente = 0;

void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type != WStype_TEXT || length == 0) {
    return;
  }
  if (payload[0] == '{') {
    // Accion de calibracion en JSON (agregar_punto / reset_tabla). Se
    // procesa aqui mismo -- webSocket.loop() corre sincrono dentro de
    // nuestro loop(), no hay problema de threading como con la libreria
    // async anterior.
    char buf[96];
    size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = 0;
    manejarAccionCalibracion(buf);
  } else {
    wsComandoPendiente = (char)payload[0];
  }
}

// Envia un mensaje JSON (ya armado) a todos los clientes web conectados.
void broadcastWS(const char* json) {
  webSocket.broadcastTXT(json, strlen(json));
}

const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Multimetro ESP32</title>
<style>
  :root {
    --bg-1: #2b2f36;
    --bg-2: #16181c;
    --case-border: #3d424a;
    --panel: #2f333a;
    --panel-border: #454a53;
    --accent: #3fb27f;
    --accent-dark: #2f8f63;
    --digit: #5cffb0;
    --text: #e8edf2;
    --muted: #8b98a5;
    --danger: #e2574c;
    --screen-bg: #0a120e;
    --screen-border: #1f2b23;
    --scope-grid: #14351f;
  }
  * { box-sizing: border-box; }
  html, body {
    margin: 0;
    min-height: 100%;
    background: radial-gradient(circle at 50% 0%, #23262b, #0c0d0f 70%);
  }
  body {
    font-family: -apple-system, "Segoe UI", Roboto, sans-serif;
    color: var(--text);
    padding: 20px 12px 40px;
  }
  .chassis {
    position: relative;
    max-width: 420px;
    margin: 0 auto;
    background: linear-gradient(180deg, var(--bg-1), var(--bg-2));
    border: 1px solid var(--case-border);
    border-radius: 26px;
    padding: 18px 18px 22px;
    box-shadow: 0 16px 40px rgba(0,0,0,.55), inset 0 1px 0 rgba(255,255,255,.05);
  }
  .screw {
    position: absolute;
    width: 9px; height: 9px;
    border-radius: 50%;
    background: radial-gradient(circle at 35% 30%, #a3a8b0, #2a2d32 75%);
    box-shadow: inset 0 0 2px #000;
  }
  .screw.tl { top: 10px; left: 10px; }
  .screw.tr { top: 10px; right: 10px; }
  .screw.bl { bottom: 10px; left: 10px; }
  .screw.br { bottom: 10px; right: 10px; }

  .chassis-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    padding: 0 6px 12px;
  }
  .brand { font-weight: 800; font-size: 0.95rem; letter-spacing: .3px; color: #d7dee6; }
  .brand small { display: block; font-weight: 500; font-size: 0.6rem; color: var(--muted); letter-spacing: 1.5px; margin-top: 1px; }
  .status {
    font-size: 0.6rem;
    letter-spacing: .5px;
    color: var(--muted);
    padding: 3px 9px;
    border-radius: 20px;
    border: 1px solid rgba(255,255,255,.08);
    background: rgba(255,255,255,.03);
  }
  .status.conectado { color: var(--digit); border-color: rgba(92,255,176,.35); box-shadow: 0 0 8px rgba(92,255,176,.15); }

  .screen {
    background: var(--screen-bg);
    border: 2px solid var(--screen-border);
    border-radius: 14px;
    padding: 14px 16px 16px;
    box-shadow: inset 0 3px 14px rgba(0,0,0,.65);
    font-family: "Consolas", "Courier New", monospace;
    min-height: 188px;
    display: flex;
    flex-direction: column;
  }
  .screen-topline {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 0.6rem;
    letter-spacing: 1.5px;
    color: rgba(92,255,176,.55);
    text-transform: uppercase;
    min-height: 14px;
  }
  .screen-badge {
    border: 1px solid rgba(92,255,176,.35);
    border-radius: 4px;
    padding: 1px 6px;
    color: rgba(92,255,176,.7);
  }
  .screen-body { flex: 1; display: flex; flex-direction: column; justify-content: center; }
  .valor {
    text-align: center;
    font-size: 2.9rem;
    font-weight: 700;
    color: var(--digit);
    text-shadow: 0 0 14px rgba(92,255,176,.5);
    letter-spacing: 1px;
    line-height: 1.1;
  }
  .unidad { display: block; text-align: center; font-size: 1rem; color: rgba(92,255,176,.75); letter-spacing: 3px; margin-top: 2px; }
  .rango { text-align: center; font-size: 0.68rem; color: var(--muted); margin-top: 8px; min-height: 12px; letter-spacing: .5px; }
  .cont-indicador {
    width: 20px; height: 20px; border-radius: 50%;
    background: #16201a;
    border: 2px solid rgba(92,255,176,.25);
    margin: 12px auto 0;
  }
  .cont-indicador.on { background: var(--digit); border-color: var(--digit); box-shadow: 0 0 16px var(--digit); }

  .scope-wrap { display: none; flex: 1; flex-direction: column; }
  .scope-wrap.visible { display: flex; }
  .scope-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    font-size: 0.68rem;
    color: var(--muted);
    margin-bottom: 6px;
    letter-spacing: .5px;
  }
  .scope-header b { color: var(--digit); font-size: 1rem; }
  canvas#scope {
    display: block;
    width: 100%;
    flex: 1;
    min-height: 140px;
    background: transparent;
    border-radius: 6px;
  }

  .dial {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 9px;
    margin-top: 16px;
  }
  .dial button {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
    padding: 12px 4px 9px;
    border: 1px solid var(--panel-border);
    border-radius: 12px;
    background: linear-gradient(180deg, var(--panel), #23262b);
    color: var(--text);
    cursor: pointer;
    box-shadow: 0 2px 0 rgba(0,0,0,.4), inset 0 1px 0 rgba(255,255,255,.05);
  }
  .dial button:active { transform: translateY(1px); box-shadow: none; }
  .dial button b { font-size: 1.25rem; font-weight: 800; }
  .dial button small { font-size: 0.58rem; color: var(--muted); letter-spacing: .3px; }
  .dial button.activo {
    background: linear-gradient(180deg, var(--accent), var(--accent-dark));
    border-color: var(--accent);
    color: #04140c;
    box-shadow: 0 0 16px rgba(63,178,127,.5), inset 0 1px 0 rgba(255,255,255,.25);
  }
  .dial button.activo small { color: rgba(4,20,12,.7); }

  .calib {
    display: none;
    margin-top: 14px;
    padding: 12px 14px;
    background: rgba(255,255,255,.03);
    border: 1px solid var(--panel-border);
    border-radius: 12px;
  }
  .calib.visible { display: block; }
  .calib-title { font-size: 0.6rem; letter-spacing: 1.5px; color: var(--muted); text-transform: uppercase; margin-bottom: 8px; }
  .calib-row { display: flex; align-items: center; gap: 8px; font-size: 0.75rem; color: var(--text); margin-bottom: 8px; }
  .calib-row span.lbl { color: var(--muted); min-width: 78px; }
  .calib-row span.val { color: var(--digit); font-family: "Consolas", "Courier New", monospace; }
  .calib input[type="number"] {
    flex: 1;
    background: var(--screen-bg);
    border: 1px solid var(--screen-border);
    border-radius: 8px;
    color: var(--digit);
    font-family: "Consolas", "Courier New", monospace;
    padding: 6px 8px;
    font-size: 0.85rem;
    min-width: 0;
  }
  .calib-btns { display: flex; gap: 8px; margin-top: 4px; }
  .calib-btns button {
    flex: 1;
    padding: 8px 4px;
    border: 1px solid var(--panel-border);
    border-radius: 8px;
    background: var(--panel);
    color: var(--text);
    font-size: 0.7rem;
    letter-spacing: .5px;
    cursor: pointer;
  }
  .calib-btns button.primario { background: linear-gradient(180deg, var(--accent), var(--accent-dark)); border-color: var(--accent); color: #04140c; font-weight: 700; }
  .calib-btns button.borrar { color: var(--danger); border-color: rgba(226,87,76,.4); }
  .calib-msg { font-size: 0.68rem; color: var(--muted); margin-top: 6px; min-height: 12px; }

  .jacks {
    display: flex;
    justify-content: center;
    gap: 30px;
    margin-top: 18px;
  }
  .jack { display: flex; flex-direction: column; align-items: center; gap: 4px; }
  .jack-hole {
    width: 24px; height: 24px;
    border-radius: 50%;
    background: radial-gradient(circle at 35% 30%, #3c4048, #101214 75%);
    border: 2px solid #52565e;
  }
  .jack-hole.red { border-color: #b1443a; box-shadow: inset 0 0 6px rgba(177,68,58,.6); }
  .jack small { font-size: 0.56rem; color: var(--muted); letter-spacing: 1px; }
</style>
</head>
<body>
  <div class="chassis">
    <div class="screw tl"></div><div class="screw tr"></div>
    <div class="screw bl"></div><div class="screw br"></div>

    <div class="chassis-header">
      <span class="brand">MULTÍMETRO<small>ESP32 &middot; DIGITAL</small></span>
      <span class="status" id="status">Conectando...</span>
    </div>

    <div class="screen">
      <div class="screen-topline">
        <span id="screenMode">&nbsp;</span>
        <span class="screen-badge">AUTO</span>
      </div>

      <div class="screen-body" id="displayPanel">
        <div id="valor" class="valor">- - - -</div>
        <span id="unidad" class="unidad"></span>
        <div id="rango" class="rango"></div>
        <div id="contIndicador" class="cont-indicador" style="display:none;"></div>
      </div>

      <div class="screen-body scope-wrap" id="scopeWrap">
        <div class="scope-header">
          <span>OSCILOSCOPIO</span>
          <b id="scopeFreqLabel">-- Hz</b>
        </div>
        <canvas id="scope"></canvas>
      </div>
    </div>

    <div class="dial">
      <button data-cmd="1" data-modo="voltaje"><b>V</b><small>VOLTAJE</small></button>
      <button data-cmd="2" data-modo="capacitancia"><b>F</b><small>CAPACIT.</small></button>
      <button data-cmd="3" data-modo="continuidad"><b>)))</b><small>CONTIN.</small></button>
      <button data-cmd="4" data-modo="resistencia"><b>&Omega;</b><small>RESIST.</small></button>
      <button data-cmd="5" data-modo="frecuencia"><b>Hz</b><small>FRECUEN.</small></button>
      <button data-cmd="6" data-modo="corriente"><b>A</b><small>CORRIENTE</small></button>
    </div>

    <div class="calib" id="calibPanel">
      <div class="calib-title">Calibración</div>
      <div class="calib-row"><span class="lbl">Lectura cruda:</span> <span class="val" id="calibCrudo">--</span></div>
      <div class="calib-row">
        <span class="lbl">Valor real:</span>
        <input type="number" step="any" id="calibReal" placeholder="ej. 12.34">
      </div>
      <div class="calib-btns">
        <button class="primario" id="btnAgregarPunto">Agregar punto</button>
        <button class="borrar" id="btnResetTabla">Reiniciar tabla</button>
      </div>
      <div class="calib-row"><span class="lbl">Puntos guardados:</span> <span class="val" id="calibNumPuntos">0</span></div>
      <div class="calib-msg" id="calibMsg"></div>
    </div>

    <div class="jacks">
      <div class="jack"><span class="jack-hole red"></span><small>V &Omega; Hz</small></div>
      <div class="jack"><span class="jack-hole"></span><small>COM</small></div>
    </div>
  </div>

<script>
  var statusEl = document.getElementById('status');
  var screenModeEl = document.getElementById('screenMode');
  var valorEl = document.getElementById('valor');
  var unidadEl = document.getElementById('unidad');
  var rangoEl = document.getElementById('rango');
  var contEl = document.getElementById('contIndicador');
  var displayPanel = document.getElementById('displayPanel');
  var scopeWrap = document.getElementById('scopeWrap');
  var scopeFreqLabel = document.getElementById('scopeFreqLabel');
  var canvas = document.getElementById('scope');
  var ctx = canvas.getContext('2d');
  var botones = document.querySelectorAll('button[data-cmd]');
  var calibPanel = document.getElementById('calibPanel');
  var calibCrudo = document.getElementById('calibCrudo');
  var calibReal = document.getElementById('calibReal');
  var calibNumPuntos = document.getElementById('calibNumPuntos');
  var calibMsg = document.getElementById('calibMsg');
  var btnAgregarPunto = document.getElementById('btnAgregarPunto');
  var btnResetTabla = document.getElementById('btnResetTabla');

  var modoActivo = null;
  var freqActual = 0;      // Hz mas reciente reportado por el ESP32
  var freqUltimoMsg = 0;   // timestamp (ms) del ultimo mensaje de frecuencia

  function marcarActivo(modo) {
    botones.forEach(function (b) {
      b.classList.toggle('activo', b.dataset.modo === modo);
    });
    screenModeEl.textContent = modo ? modo.toUpperCase() : ' ';
  }

  function resetDisplay() {
    modoActivo = null;
    valorEl.textContent = '--';
    unidadEl.textContent = '';
    rangoEl.textContent = '';
    contEl.style.display = 'none';
    displayPanel.style.display = 'flex';
    scopeWrap.classList.remove('visible');
    calibPanel.classList.remove('visible');
    freqActual = 0;
    marcarActivo(null);
  }

  // Dibuja una onda cuadrada continua a partir de freqActual, con
  // "look" de osciloscopio (fondo oscuro, cuadricula, traza verde).
  // No es una captura real de la señal (ver nota en el .ino sobre
  // ADC2 + WiFi) -- es una sintesis en vivo a partir del Hz medido.
  function dibujarOsciloscopio(tMs) {
    var w = canvas.width = canvas.clientWidth;
    var h = canvas.height = canvas.clientHeight;
    ctx.fillStyle = '#04120a';
    ctx.fillRect(0, 0, w, h);

    // Cuadricula estilo osciloscopio (8 columnas x 4 filas).
    ctx.strokeStyle = '#0f3d24';
    ctx.lineWidth = 1;
    var cols = 8, rows = 4;
    for (var c = 1; c < cols; c++) {
      var x = (w / cols) * c;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
    }
    for (var r = 1; r < rows; r++) {
      var y = (h / rows) * r;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
    }

    var freqParaDibujar = freqActual;
    var señalViva = freqParaDibujar > 0 && (tMs - freqUltimoMsg) < 3000;

    if (!señalViva) {
      ctx.strokeStyle = '#39ff8f';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(0, h / 2);
      ctx.lineTo(w, h / 2);
      ctx.stroke();
      ctx.fillStyle = '#5c7a6a';
      ctx.font = '13px -apple-system, Segoe UI, Roboto, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Esperando señal...', w / 2, h / 2 - 10);
      requestAnimationFrame(dibujarOsciloscopio);
      return;
    }

    // Cuantos periodos mostrar en pantalla a la vez (mas periodos si
    // la frecuencia es alta, para que siempre se vea una onda legible).
    var periodosVisibles = 4;
    var periodoS = 1 / freqParaDibujar;
    var ventanaS = periodosVisibles * periodoS;

    var altoAmp = h * 0.32; // amplitud de la onda dibujada
    var centroY = h / 2;
    var tS = tMs / 1000;

    ctx.strokeStyle = '#39ff8f';
    ctx.lineWidth = 2;
    ctx.shadowColor = '#39ff8f';
    ctx.shadowBlur = 6;
    ctx.beginPath();

    var pasos = Math.max(200, Math.min(2000, Math.floor(w)));
    for (var i = 0; i <= pasos; i++) {
      var x = (i / pasos) * w;
      var tAtX = tS - ventanaS + (i / pasos) * ventanaS; // ventana que "llega" hasta ahora
      var fase = (tAtX * freqParaDibujar) % 1;
      if (fase < 0) fase += 1;
      var y = centroY + (fase < 0.5 ? -altoAmp : altoAmp);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.shadowBlur = 0;

    requestAnimationFrame(dibujarOsciloscopio);
  }
  requestAnimationFrame(dibujarOsciloscopio);

  function conectarWS() {
    var ws = new WebSocket('ws://' + location.hostname + ':81/');

    ws.onopen = function () {
      statusEl.textContent = 'Conectado';
      statusEl.classList.add('conectado');
    };

    ws.onclose = function () {
      statusEl.textContent = 'Desconectado, reintentando...';
      statusEl.classList.remove('conectado');
      setTimeout(conectarWS, 1500);
    };

    ws.onerror = function () {
      ws.close();
    };

    ws.onmessage = function (evt) {
      var msg;
      try { msg = JSON.parse(evt.data); } catch (e) { return; }

      if (msg.modo === 'menu') {
        resetDisplay();
        return;
      }

      if (msg.modo === 'calibracion') {
        if (typeof msg.numPuntos === 'number') calibNumPuntos.textContent = msg.numPuntos;
        if (msg.error) {
          calibMsg.textContent = 'Error: ' + msg.error;
        } else if (msg.evento === 'agregado') {
          calibMsg.textContent = 'Punto agregado. Puntos guardados: ' + msg.numPuntos;
        } else if (msg.evento === 'tabla_llena') {
          calibMsg.textContent = 'Tabla llena (max ' + msg.numPuntos + ' puntos).';
        } else if (msg.evento === 'reset') {
          calibMsg.textContent = 'Tabla reiniciada.';
        }
        return;
      }

      modoActivo = msg.modo;
      marcarActivo(msg.modo);

      // Actualiza el panel de calibracion para cualquier modo numerico
      // (todos menos continuidad, que es binaria).
      if (msg.modo !== 'continuidad' && (typeof msg.crudo === 'number' || typeof msg.numPuntos === 'number')) {
        calibPanel.classList.add('visible');
        if (typeof msg.crudo === 'number') calibCrudo.textContent = msg.crudo.toFixed(4);
        if (typeof msg.numPuntos === 'number') calibNumPuntos.textContent = msg.numPuntos;
      }

      if (msg.modo === 'frecuencia') {
        displayPanel.style.display = 'none';
        scopeWrap.classList.add('visible');
        contEl.style.display = 'none';
        if (typeof msg.valor === 'number') {
          freqActual = msg.valor;
          freqUltimoMsg = performance.now();
          scopeFreqLabel.textContent = msg.valor.toFixed(msg.valor >= 1000 ? 1 : 3) + ' Hz';
        }
        return;
      }

      displayPanel.style.display = 'flex';
      scopeWrap.classList.remove('visible');

      if (msg.modo === 'continuidad') {
        calibPanel.classList.remove('visible');
        contEl.style.display = 'block';
        contEl.classList.toggle('on', !!msg.estado);
        valorEl.textContent = msg.estado ? 'SI' : 'NO';
        unidadEl.textContent = '';
        return;
      }

      if (msg.error) {
        valorEl.textContent = 'Error';
        unidadEl.textContent = msg.error;
        return;
      }

      contEl.style.display = 'none';

      if (typeof msg.valor === 'number') {
        valorEl.textContent = msg.valor.toFixed(msg.valor >= 1000 ? 1 : 3);
        unidadEl.textContent = msg.unidad || '';
      }
      rangoEl.textContent = msg.rango || '';
    };

    botones.forEach(function (b) {
      b.onclick = function () {
        ws.send(b.dataset.cmd);
        marcarActivo(b.dataset.modo);
        // Cambia el panel de inmediato al hacer clic, sin esperar a
        // que llegue el primer dato del ESP32 (que puede tardar si
        // todavia no hay señal/lectura). El firmware cancela el modo
        // anterior y entra al nuevo automaticamente con este mismo
        // comando, sin necesidad de un boton de cancelar aparte.
        if (b.dataset.modo === 'frecuencia') {
          displayPanel.style.display = 'none';
          scopeWrap.classList.add('visible');
        } else {
          displayPanel.style.display = 'flex';
          scopeWrap.classList.remove('visible');
        }
        // Se oculta hasta que llegue telemetria del modo nuevo, para no
        // mostrar por un instante datos de calibracion del modo anterior.
        calibPanel.classList.remove('visible');
        calibMsg.textContent = '';
        calibCrudo.textContent = '--';
        calibNumPuntos.textContent = '0';
      };
    });

    btnAgregarPunto.onclick = function () {
      var real = parseFloat(calibReal.value);
      if (isNaN(real)) {
        calibMsg.textContent = 'Escribe un valor real valido primero.';
        return;
      }
      ws.send(JSON.stringify({ accion: 'agregar_punto', real: real }));
      calibReal.value = '';
    };

    btnResetTabla.onclick = function () {
      if (!confirm('¿Reiniciar la tabla de calibración de este modo? Se borran todos sus puntos guardados.')) {
        return;
      }
      ws.send(JSON.stringify({ accion: 'reset_tabla' }));
    };
  }

  conectarWS();
</script>
</body>
</html>
)HTMLPAGE";

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
// ---------------- CALIBRACION (INTERPOLACION LINEAL) ---------------
// ---------------------------------------------------------------
// Metodo de calibracion compartido por todos los modos numericos:
// interpolacion lineal por tramos sobre una tabla de puntos
// "lectura cruda -> valor real medido con un instrumento de
// referencia". A diferencia de un ajuste polinomial (minimos
// cuadrados), esto pasa exacto por cada punto que midas y no oscila
// entre puntos (sin fenomeno de Runge).
//
// Requisito: xs[] debe estar ordenado ascendente. Fuera del rango de
// la tabla, se extrapola linealmente con la pendiente del tramo mas
// cercano (en vez de saturar en el borde). n==0 (tabla vacia) es
// identidad: no altera la lectura.
float interpolarLineal(float x, const float xs[], const float ys[], int n) {
  if (n <= 0) return x;
  if (n == 1) return ys[0];

  int i;
  if (x <= xs[0]) {
    i = 0; // extrapola hacia abajo con la pendiente del primer tramo
  } else if (x >= xs[n - 1]) {
    i = n - 2; // extrapola hacia arriba con la pendiente del ultimo tramo
  } else {
    i = 0;
    while (i < n - 2 && x > xs[i + 1]) {
      i++;
    }
  }

  float t = (x - xs[i]) / (xs[i + 1] - xs[i]);
  return ys[i] + t * (ys[i + 1] - ys[i]);
}

// Las tablas de calibracion se pueden llenar desde la pagina web (sin
// recompilar) y se guardan en NVS (memoria no volatil del ESP32) via
// Preferences, asi que sobreviven reinicios. Cada tabla arranca vacia
// (numPuntos=0 = identidad, ver interpolarLineal) hasta que agregues
// puntos desde la web.
const int MAX_PUNTOS_CAL = 15;

struct TablaCal {
  const char* clave; // prefijo corto para las claves de NVS (max ~4 chars)
  float medido[MAX_PUNTOS_CAL];
  float real[MAX_PUNTOS_CAL];
  int numPuntos;
};

Preferences calibPrefs;

// Prototipos manuales: igual que con RangeOhm, evita que el IDE de
// Arduino genere sus propios prototipos automaticos al inicio del
// archivo (donde TablaCal todavia no existe), lo cual rompe la
// compilacion.
void cargarTabla(TablaCal &t);
void guardarTabla(TablaCal &t);
bool agregarPunto(TablaCal &t, float medido, float real);
void reiniciarTabla(TablaCal &t);
TablaCal* tablaActiva();

void cargarTabla(TablaCal &t) {
  char keyN[16], keyM[16], keyR[16];
  snprintf(keyN, sizeof(keyN), "%s_n", t.clave);
  snprintf(keyM, sizeof(keyM), "%s_m", t.clave);
  snprintf(keyR, sizeof(keyR), "%s_r", t.clave);

  int n = calibPrefs.getUChar(keyN, 0);
  if (n > MAX_PUNTOS_CAL) n = MAX_PUNTOS_CAL;
  t.numPuntos = n;
  if (n > 0) {
    calibPrefs.getBytes(keyM, t.medido, n * sizeof(float));
    calibPrefs.getBytes(keyR, t.real, n * sizeof(float));
  }
}

void guardarTabla(TablaCal &t) {
  char keyN[16], keyM[16], keyR[16];
  snprintf(keyN, sizeof(keyN), "%s_n", t.clave);
  snprintf(keyM, sizeof(keyM), "%s_m", t.clave);
  snprintf(keyR, sizeof(keyR), "%s_r", t.clave);

  calibPrefs.putUChar(keyN, (uint8_t)t.numPuntos);
  if (t.numPuntos > 0) {
    calibPrefs.putBytes(keyM, t.medido, t.numPuntos * sizeof(float));
    calibPrefs.putBytes(keyR, t.real, t.numPuntos * sizeof(float));
  }
}

// Inserta un punto ordenado por "medido". Si ya hay un punto muy
// cercano en "medido", lo reemplaza (evita duplicados casi iguales).
// Devuelve false si la tabla ya esta llena y el punto es nuevo.
bool agregarPunto(TablaCal &t, float medido, float real) {
  const float TOLERANCIA_CAL = 1e-4f; // "EPS" choca con una macro del core de Xtensa (specreg.h)
  for (int i = 0; i < t.numPuntos; i++) {
    if (fabs(t.medido[i] - medido) < TOLERANCIA_CAL) {
      t.real[i] = real;
      guardarTabla(t);
      return true;
    }
  }
  if (t.numPuntos >= MAX_PUNTOS_CAL) {
    return false;
  }
  int i = 0;
  while (i < t.numPuntos && t.medido[i] < medido) {
    i++;
  }
  for (int j = t.numPuntos; j > i; j--) {
    t.medido[j] = t.medido[j - 1];
    t.real[j] = t.real[j - 1];
  }
  t.medido[i] = medido;
  t.real[i] = real;
  t.numPuntos++;
  guardarTabla(t);
  return true;
}

void reiniciarTabla(TablaCal &t) {
  t.numPuntos = 0;
  guardarTabla(t);
}

// ---------------------------------------------------------------
// ---------------------- VOLTÍMETRO ------------------------------
// ---------------------------------------------------------------
bool debug = true;
const int R1 = 68000;
const int R2 = 10000;

const int VinPin = 34;      // Entrada analógica del divisor de voltaje

const float Vref = 3.3;
const int adcMax = 4095;

// Tabla de calibracion -- se llena desde la pagina web, persiste en NVS.
TablaCal tablaVoltaje = { "V" };
float ultimoCrudoVoltaje = 0; // VB1 sin calibrar, para "agregar punto" desde la web

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

// Tabla de calibracion (en microFarads) -- se llena desde la pagina
// web, persiste en NVS.
TablaCal tablaCap = { "C" };
float ultimoCrudoCap = 0; // microFarads sin calibrar, para "agregar punto" desde la web

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

// Cada rango tiene su propia tabla de calibracion (por interpolacion
// lineal, en OHMS) -- se llenan desde la pagina web, persisten en NVS.
// Empiezan vacias (identidad).
struct RangeOhm {
  int          pin;          // pin que conecta la resistencia de referencia
  float        R1_ohms;      // valor real de la resistencia de referencia, en OHMS
  const char*  label;        // etiqueta de la escala
  float        unitDivisor;  // 1000 = mostrar en K ohms, 1000000 = mostrar en M ohms
  const char*  unit;         // "K" o "M"
  TablaCal*    cal;          // tabla de calibracion propia de este rango
};

// NOTA: R1_ohms usa el valor nominal de cada resistencia (100/1K/10K/100K).
// Para mejor precision, reemplaza cada valor por el que midas con un
// ohmetro de referencia sobre la resistencia real que soldaste (igual
// que se hacia antes con los rangos de 2K/20K/200K/1M).
TablaCal tablaOhm100  = { "R1" };
TablaCal tablaOhm1K   = { "R2" };
TablaCal tablaOhm10K  = { "R3" };
TablaCal tablaOhm100K = { "R4" };

RangeOhm rangesOhm[] = {
  { ohmCh100,  100.0f,    "0 - 100",    1000.0f, "K", &tablaOhm100  },
  { ohmCh1K,   1000.0f,   "100 - 1K",   1000.0f, "K", &tablaOhm1K   },
  { ohmCh10K,  10000.0f,  "1K - 10K",   1000.0f, "K", &tablaOhm10K  },
  { ohmCh100K, 100000.0f, "10K - 100K", 1000.0f, "K", &tablaOhm100K },
};
const int numRangesOhm = sizeof(rangesOhm) / sizeof(rangesOhm[0]);

float ultimoCrudoOhm = 0;                  // R2_ohms sin calibrar del ultimo rango usado
RangeOhm* ultimoRangoOhmActivo = NULL;     // que rango (y por lo tanto que tabla) se uso

// Prototipo manual: evita que el IDE de Arduino genere su propio
// prototipo automatico al inicio del archivo (donde RangeOhm todavia
// no existe), lo cual rompe la compilacion.
float calibrarOhmetro(float r, const RangeOhm &rg);

// ---------------------------------------------------------------
// -------------------- FRECUENCIA (MODO 5) --------------------------
// ---------------------------------------------------------------
// Comparte la punta+ con los otros modos a traves de relayFrecuenciaPin
// (GPIO18). Requiere el circuito externo descrito en el comentario de
// cabecera (divisor simple 1K/10K, sin comparador) entre el contacto
// de salida del relay y GPIO25.
const int freqInputPin = 25;
const unsigned long freqGateMs = 1000; // cada cuanto se revisa si ya hay suficientes pulsos

// Filtro de ruido/rebote: sin comparador que limpie la señal, este es
// el unico filtro contra disparos falsos. 30us = 30% del periodo a la
// frecuencia maxima de referencia (10KHz -> 100us de periodo). Si vas
// a medir frecuencias mas altas, baja este valor proporcionalmente.
const unsigned long FILTRO_RUIDO_US = 30;

volatile uint32_t freqPulseCount = 0;
volatile unsigned long freqPrimerPulsoUs = 0;
volatile unsigned long freqUltimoPulsoUs = 0;

unsigned long freqLastCheck = 0;
float frequency = 0;

// Tabla de calibracion (en Hz) -- se llena desde la pagina web, persiste en NVS.
TablaCal tablaFreq = { "F" };
float ultimoCrudoFreq = 0; // Hz sin calibrar, para "agregar punto" desde la web

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

// Tabla de calibracion (en Amps) -- se llena desde la pagina web, persiste en NVS.
TablaCal tablaCorriente = { "A" };
float ultimoCrudoCorriente = 0; // Amps sin calibrar, para "agregar punto" desde la web

// ---------------------------------------------------------------
// -------------------- CALIBRACION DESDE LA WEB -----------------------
// ---------------------------------------------------------------
// Mapea el modo activo a su tabla de calibracion. NULL en Continuidad
// (es binario, no se calibra) y en el menu principal.
TablaCal* tablaActiva() {
  switch (modoActual) {
    case MODO_VOLTAJE:      return &tablaVoltaje;
    case MODO_CAPACITANCIA: return &tablaCap;
    case MODO_4:             return ultimoRangoOhmActivo ? ultimoRangoOhmActivo->cal : NULL;
    case MODO_5:             return &tablaFreq;
    case MODO_6:             return &tablaCorriente;
    default:                 return NULL;
  }
}

// Ultimo valor crudo (sin calibrar) del modo activo, capturado por su
// funcion de lectura correspondiente.
float crudoDelModoActual() {
  switch (modoActual) {
    case MODO_VOLTAJE:      return ultimoCrudoVoltaje;
    case MODO_CAPACITANCIA: return ultimoCrudoCap;
    case MODO_4:             return ultimoCrudoOhm;
    case MODO_5:             return ultimoCrudoFreq;
    case MODO_6:             return ultimoCrudoCorriente;
    default:                 return 0;
  }
}

// Interpreta las acciones de calibracion que llegan por WebSocket en
// JSON: {"accion":"agregar_punto","real":12.34} o {"accion":"reset_tabla"}.
// Parseo manual (strstr/atof) para no agregar ArduinoJson como dependencia.
void manejarAccionCalibracion(const char* json) {
  TablaCal* t = tablaActiva();
  if (!t) {
    broadcastWS("{\"modo\":\"calibracion\",\"error\":\"modo no calibrable\"}");
    return;
  }

  if (strstr(json, "reset_tabla")) {
    reiniciarTabla(*t);
    char msg[64];
    snprintf(msg, sizeof(msg), "{\"modo\":\"calibracion\",\"evento\":\"reset\",\"numPuntos\":%d}", t->numPuntos);
    broadcastWS(msg);
    return;
  }

  if (strstr(json, "agregar_punto")) {
    const char* p = strstr(json, "\"real\":");
    if (!p) return;
    float real = atof(p + 7);
    bool ok = agregarPunto(*t, crudoDelModoActual(), real);
    char msg[96];
    snprintf(msg, sizeof(msg), "{\"modo\":\"calibracion\",\"evento\":\"%s\",\"numPuntos\":%d}",
             ok ? "agregado" : "tabla_llena", t->numPuntos);
    broadcastWS(msg);
  }
}

// ---------------------------------------------------------------
// -------------------------- SETUP -------------------------------
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  calibPrefs.begin("calib", false);
  cargarTabla(tablaVoltaje);
  cargarTabla(tablaCap);
  cargarTabla(tablaOhm100);
  cargarTabla(tablaOhm1K);
  cargarTabla(tablaOhm10K);
  cargarTabla(tablaOhm100K);
  cargarTabla(tablaFreq);
  cargarTabla(tablaCorriente);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); // habilita lectura de 0 a ~3.3V (usado por el ohmetro)

  pinMode(relayVoltajePin, OUTPUT);
  pinMode(relayCapacitanciaPin, OUTPUT);
  pinMode(relayResistenciaPin, OUTPUT);
  pinMode(relayFrecuenciaPin, OUTPUT);
  pinMode(relayCorrientePin, OUTPUT);
  apagarTodosLosRelays();

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

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println();
  Serial.print("Access Point creado: ");
  Serial.println(AP_SSID);
  Serial.print("Conectate a esa red y entra a: http://");
  Serial.println(WiFi.softAPIP());
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.print("Tambien disponible en: http://");
    Serial.print(MDNS_HOSTNAME);
    Serial.println(".local");
  }

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWsEvent);
}

// ---------------------------------------------------------------
// -------------------------- LOOP --------------------------------
// ---------------------------------------------------------------
void loop() {
  server.handleClient();
  webSocket.loop();

  switch (modoActual) {

    case MENU_PRINCIPAL:
      if (!menuMostrado) {
        mostrarMenu();
        menuMostrado = true;
      }
      {
        char c;
        if (comandoPendiente(c)) {
          procesarComando(c);
        }
      }
      break;

    case MODO_VOLTAJE:
      if (salirSiSePide()) break;
      readVoltage();
      Serial.print("VB1 Voltaje: ");
      Serial.print(VB1);
      Serial.println("V");
      {
        char wsMsg[112];
        snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"voltaje\",\"valor\":%.3f,\"unidad\":\"V\",\"crudo\":%.4f,\"numPuntos\":%d}",
                 VB1, ultimoCrudoVoltaje, tablaVoltaje.numPuntos);
        broadcastWS(wsMsg);
      }
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

// Revisa si hay un comando pendiente por Serial o por WebSocket. Si lo
// hay, lo pone en c, lo consume y devuelve true. Con esto Serial y la
// pagina web alimentan la misma maquina de estados sin duplicar logica.
bool comandoPendiente(char &c) {
  if (wsComandoPendiente != 0) {
    c = wsComandoPendiente;
    wsComandoPendiente = 0;
    return true;
  }
  if (Serial.available()) {
    c = Serial.read();
    return true;
  }
  return false;
}

// Interpreta un comando de un caracter ('1'-'6') y entra al modo
// correspondiente. Lo llaman tanto el Serial como el WebSocket (via
// comandoPendiente), asi que ambos caminos de control quedan
// sincronizados sobre el mismo estado.
void procesarComando(char c) {
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

// Revisa si llego un comando que saca del modo actual: 'x'/'9' cancela
// y regresa al menu; '1'-'6' cancela el modo actual y entra directo al
// nuevo (sin pasar por el menu ni requerir un boton de cancelar aparte
// en la web). Devuelve true si el modo actual debe abandonarse ya.
bool salirSiSePide() {
  char c;
  if (comandoPendiente(c)) {
    if (c == 'x' || c == 'X' || c == '9') {
      cancelarYRegresarAlMenu();
      return true;
    }
    if (c >= '1' && c <= '6') {
      limpiarEstadoModo();
      procesarComando(c);
      return true;
    }
  }
  return false;
}

// Apaga relays, corta carga/descarga en curso, apaga buzzer y reinicia
// contadores -- limpieza comun tanto al cancelar (volver al menu) como
// al cambiar directo de un modo a otro.
void limpiarEstadoModo() {
  apagarTodosLosRelays();
  digitalWrite(chargePin, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  contEstadoAnterior = false;
  allRangesOffOhm();
  noInterrupts();
  freqPulseCount = 0;
  interrupts();
}

// Regresa al menu principal (usado por 'x'/'9'). A diferencia de un
// cambio directo de modo, aqui si se avisa al menu y se manda el
// broadcast "menu" para que la pagina web vuelva al estado inicial.
void cancelarYRegresarAlMenu() {
  limpiarEstadoModo();
  Serial.println(">> Cancelado. Saliendo al menu principal.");
  modoActual = MENU_PRINCIPAL;
  menuMostrado = false;
  broadcastWS("{\"modo\":\"menu\"}");
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
void readVoltage(){
  // analogReadMilliVolts() usa la calibracion de fabrica del ADC
  // (guardada en eFuse) en vez de una conversion lineal ingenua de
  // cuentas a voltaje. El ADC del ESP32 es poco lineal cerca de 0V
  // (con analogRead() crudo, voltajes bajos podian leer 0); esta
  // funcion corrige buena parte de esa no-linealidad sin tocar nada
  // del circuito (nada de "tierra virtual" por hardware necesario).
  long sumMv = 0;
  const int samples = 20;
  for (int i = 0; i < samples; i++){
    sumMv += analogReadMilliVolts(VinPin);
    delayMicroseconds(200);
  }
  float VoutMv = (float)sumMv / samples;

  float Vout = VoutMv / 1000.0f;
  float VB1_sinCalibrar = Vout * (R1 + R2) / R2;
  ultimoCrudoVoltaje = VB1_sinCalibrar;

  VB1 = interpolarLineal(VB1_sinCalibrar, tablaVoltaje.medido, tablaVoltaje.real, tablaVoltaje.numPuntos);
  mV = VB1 * 1000;

  if (debug) {
    Serial.print("mV ADC (calibrado): ");
    Serial.print(VoutMv, 1);
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
      broadcastWS("{\"modo\":\"capacitancia\",\"error\":\"timeout carga\"}");
      return;
    }
  }
  elapsedTime = micros() - startTime;
  microFarads = ((float)elapsedTime / resistorValue);
  ultimoCrudoCap = microFarads;
  microFarads = interpolarLineal(microFarads, tablaCap.medido, tablaCap.real, tablaCap.numPuntos);

  if (microFarads > 1) {
    Serial.print("Capacitancia: ");
    Serial.print(microFarads, 3);
    Serial.println(" uF");
    char wsMsg[128];
    snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"capacitancia\",\"valor\":%.3f,\"unidad\":\"uF\",\"crudo\":%.4f,\"numPuntos\":%d}",
             microFarads, ultimoCrudoCap, tablaCap.numPuntos);
    broadcastWS(wsMsg);
  } else {
    nanoFarads = microFarads * 1000.0;
    Serial.print("Capacitancia: ");
    Serial.print(nanoFarads, 3);
    Serial.println(" nF");
    char wsMsg[128];
    snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"capacitancia\",\"valor\":%.3f,\"unidad\":\"nF\",\"crudo\":%.4f,\"numPuntos\":%d}",
             nanoFarads, ultimoCrudoCap, tablaCap.numPuntos);
    broadcastWS(wsMsg);
  }

  // --- Descarga ---
  digitalWrite(chargePin, LOW);
  pinMode(dischargePin, OUTPUT);
  digitalWrite(dischargePin, LOW);
  inicioEspera = millis();
  while (analogRead(analogPinCap) > 0) {
    server.handleClient(); // nada cronometrado aqui, seguro atender la web mientras se espera
    webSocket.loop();
    if (hayCancelacionPendiente()) {
      pinMode(dischargePin, INPUT);
      cancelarYRegresarAlMenu();
      return;
    }
    if (millis() - inicioEspera > TIMEOUT_MS) {
      Serial.println("Timeout esperando descarga.");
      broadcastWS("{\"modo\":\"capacitancia\",\"error\":\"timeout descarga\"}");
      break;
    }
  }
  pinMode(dischargePin, INPUT);
}

// Revisa (sin bloquear) si llegó 'x' o '9' por Serial o por WebSocket
// mientras se está a mitad de una medición de capacitancia.
bool hayCancelacionPendiente() {
  if (wsComandoPendiente != 0) {
    if (wsComandoPendiente == 'x' || wsComandoPendiente == 'X' || wsComandoPendiente == '9') {
      wsComandoPendiente = 0; // consumido
      return true;
    }
    return false; // no es cancelacion, se deja pendiente para despues
  }
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

    char wsMsg[48];
    snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"continuidad\",\"estado\":%s}", continuidad ? "true" : "false");
    broadcastWS(wsMsg);
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

// Aplica la interpolacion lineal de calibracion propia de cada rango
// a una resistencia medida.
float calibrarOhmetro(float r, const RangeOhm &rg) {
  return interpolarLineal(r, rg.cal->medido, rg.cal->real, rg.cal->numPuntos);
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
    ultimoCrudoOhm = R2_ohms;
    ultimoRangoOhmActivo = &rangesOhm[i];
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

    {
      char wsMsg[160];
      snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"resistencia\",\"valor\":%.3f,\"unidad\":\"%s\",\"rango\":\"%s\",\"crudo\":%.3f,\"numPuntos\":%d}",
               R2_ohms / rangesOhm[i].unitDivisor, rangesOhm[i].unit, rangesOhm[i].label,
               ultimoCrudoOhm, rangesOhm[i].cal->numPuntos);
      broadcastWS(wsMsg);
    }

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
  ultimoCrudoFreq = frequency;
  frequency = interpolarLineal(frequency, tablaFreq.medido, tablaFreq.real, tablaFreq.numPuntos);

  noInterrupts();
  freqPulseCount = 0;
  interrupts();

  // Formato "F,<valor>" para que sea facil de leer desde una app en
  // Python (por ejemplo, graficar en tiempo real con
  // graficar_frecuencia.py).
  Serial.print("F,");
  Serial.println(frequency, 3);

  char wsMsg[128];
  snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"frecuencia\",\"valor\":%.3f,\"unidad\":\"Hz\",\"crudo\":%.3f,\"numPuntos\":%d}",
           frequency, ultimoCrudoFreq, tablaFreq.numPuntos);
  broadcastWS(wsMsg);
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
  ultimoCrudoCorriente = Vshunt / R_SHUNT_OHMS;
  float corrienteA = interpolarLineal(ultimoCrudoCorriente, tablaCorriente.medido, tablaCorriente.real, tablaCorriente.numPuntos);

  Serial.print("ADC crudo: ");
  Serial.print(adcValue);
  Serial.print(" | Vshunt: ");
  Serial.print(Vshunt, 4);
  Serial.print("V | Corriente: ");
  char wsMsg[128];
  if (fabs(corrienteA) >= 1.0f) {
    Serial.print(corrienteA, 4);
    Serial.println(" A");
    snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"corriente\",\"valor\":%.4f,\"unidad\":\"A\",\"crudo\":%.5f,\"numPuntos\":%d}",
             corrienteA, ultimoCrudoCorriente, tablaCorriente.numPuntos);
  } else {
    Serial.print(corrienteA * 1000.0f, 2);
    Serial.println(" mA");
    snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"corriente\",\"valor\":%.2f,\"unidad\":\"mA\",\"crudo\":%.5f,\"numPuntos\":%d}",
             corrienteA * 1000.0f, ultimoCrudoCorriente, tablaCorriente.numPuntos);
  }
  broadcastWS(wsMsg);
}
