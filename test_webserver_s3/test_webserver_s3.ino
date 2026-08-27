/*
  Sketch minimo de prueba -- SOLO WiFi + servidor web + WebSocket,
  sin nada del circuito del multimetro (sin relays, sin pinMode de
  sensores, sin attachInterrupt). Pensado para probar la interfaz web
  en el ESP32-S3 que tienes a la mano, mientras el ESP32 "convencional"
  del multimetro no tiene cable de alimentacion.

  Si esto SI levanta el Access Point sin colgarse, confirma que el
  problema esta en algo del setup() del sketch principal (probablemente
  un GPIO que en el S3 no se comporta igual que en el ESP32 original).
  Si esto TAMBIEN se cuelga igual, el problema es mas de fondo (la
  libreria WiFi/AsyncWebServer con este core/placa), y hay que revisar
  version de librerias o del core ESP32 instalado para S3.

  Ademas, cada segundo manda un valor de frecuencia simulado (variando
  entre 100 y 2000 Hz) por WebSocket, para que puedas ver el
  osciloscopio de la pagina animarse sin necesitar ningun circuito.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* AP_SSID     = "Multimetro-ESP32";
const char* AP_PASSWORD = "multimetro123";
const char* MDNS_HOSTNAME = "multimetro";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void onWsEvent(AsyncWebSocket *serverWs, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("Cliente WS conectado.");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("Cliente WS desconectado.");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT && len > 0) {
      Serial.print("Comando recibido por WS: ");
      Serial.write(data, len);
      Serial.println();
    }
  }
}

const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Prueba Web Server S3</title>
<style>
  :root {
    --bg: #10151c;
    --panel: #1b232d;
    --accent: #3fb27f;
    --text: #e8edf2;
    --muted: #8b98a5;
    --scope-bg: #04120a;
    --scope-grid: #0f3d24;
    --scope-trace: #39ff8f;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: -apple-system, "Segoe UI", Roboto, sans-serif;
    background: var(--bg);
    color: var(--text);
    padding: 16px;
  }
  h1 { font-size: 1.1rem; font-weight: 600; margin: 0 0 12px; color: var(--muted); text-align: center; }
  .status { text-align: center; font-size: 0.8rem; color: var(--muted); margin-bottom: 16px; }
  .status.conectado { color: var(--accent); }
  .scope-wrap { max-width: 480px; margin: 0 auto; }
  .scope-header {
    display: flex;
    justify-content: space-between;
    font-size: 0.75rem;
    color: var(--muted);
    margin-bottom: 6px;
  }
  .scope-header b { color: var(--scope-trace); font-size: 1rem; }
  canvas#scope {
    display: block;
    width: 100%;
    height: 220px;
    background: var(--scope-bg);
    border-radius: 10px;
    border: 1px solid #123322;
  }
  .nota { max-width: 480px; margin: 16px auto 0; font-size: 0.8rem; color: var(--muted); text-align: center; }
</style>
</head>
<body>
  <h1>Prueba Web Server S3 (datos simulados)</h1>
  <div class="status" id="status">Conectando...</div>

  <div class="scope-wrap">
    <div class="scope-header">
      <span>OSCILOSCOPIO (simulado)</span>
      <b id="scopeFreqLabel">-- Hz</b>
    </div>
    <canvas id="scope"></canvas>
  </div>

  <div class="nota">Si ves la onda moviendose, el WiFi + WebSocket + pagina funcionan bien en este ESP32-S3.</div>

<script>
  var statusEl = document.getElementById('status');
  var scopeFreqLabel = document.getElementById('scopeFreqLabel');
  var canvas = document.getElementById('scope');
  var ctx = canvas.getContext('2d');

  var freqActual = 0;
  var freqUltimoMsg = 0;

  function dibujarOsciloscopio(tMs) {
    var w = canvas.width = canvas.clientWidth;
    var h = canvas.height = canvas.clientHeight;
    ctx.fillStyle = '#04120a';
    ctx.fillRect(0, 0, w, h);

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

    var periodosVisibles = 4;
    var periodoS = 1 / freqParaDibujar;
    var ventanaS = periodosVisibles * periodoS;
    var altoAmp = h * 0.32;
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
      var tAtX = tS - ventanaS + (i / pasos) * ventanaS;
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
    var ws = new WebSocket('ws://' + location.hostname + '/ws');

    ws.onopen = function () {
      statusEl.textContent = 'Conectado';
      statusEl.classList.add('conectado');
    };
    ws.onclose = function () {
      statusEl.textContent = 'Desconectado, reintentando...';
      statusEl.classList.remove('conectado');
      setTimeout(conectarWS, 1500);
    };
    ws.onerror = function () { ws.close(); };

    ws.onmessage = function (evt) {
      var msg;
      try { msg = JSON.parse(evt.data); } catch (e) { return; }
      if (msg.modo === 'frecuencia' && typeof msg.valor === 'number') {
        freqActual = msg.valor;
        freqUltimoMsg = performance.now();
        scopeFreqLabel.textContent = msg.valor.toFixed(1) + ' Hz';
      }
    };
  }
  conectarWS();
</script>
</body>
</html>
)HTMLPAGE";

unsigned long ultimoEnvio = 0;
float freqSimulada = 100.0;
float direccion = 1.0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== Sketch de prueba: WiFi + Web Server + WebSocket (sin circuito del multimetro) ===");

  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("WiFi.softAP() devolvio: ");
  Serial.println(apOk ? "true (OK)" : "false (FALLO)");
  Serial.print("Access Point creado: ");
  Serial.println(AP_SSID);
  Serial.print("Conectate a esa red y entra a: http://");
  Serial.println(WiFi.softAPIP());

  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.print("Tambien disponible en: http://");
    Serial.print(MDNS_HOSTNAME);
    Serial.println(".local");
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });
  server.begin();

  Serial.println("Setup completo. Si ves esto, el WiFi/servidor SI arrancaron bien.");
}

void loop() {
  if (millis() - ultimoEnvio > 1000) {
    ultimoEnvio = millis();

    freqSimulada += direccion * 50.0;
    if (freqSimulada > 2000.0) { freqSimulada = 2000.0; direccion = -1.0; }
    if (freqSimulada < 100.0)  { freqSimulada = 100.0;  direccion = 1.0; }

    char wsMsg[64];
    snprintf(wsMsg, sizeof(wsMsg), "{\"modo\":\"frecuencia\",\"valor\":%.1f,\"unidad\":\"Hz\"}", freqSimulada);
    ws.textAll(wsMsg);

    Serial.print("Enviado por WS: ");
    Serial.println(wsMsg);
  }
}
