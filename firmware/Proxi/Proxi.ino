#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <cstring>

// ==========================================
// PROXI - ROBOT CON INTELIGENCIA ARTIFICIAL
// ESP32-S3 + GROQ + VOICE RSS
// ==========================================

// ==========================================
// 1. CREDENCIALES PERSONALES
// ==========================================

#include "config.h"

// ==========================================
// 2. CONFIGURACION DEL HARDWARE
// ==========================================

// Boton
#define BOTON 4

// Microfono INMP441
#define MIC_SCK 8
#define MIC_WS  9
#define MIC_SD  10

// Amplificador MAX98357A
#define AMP_BCLK 11
#define AMP_LRC  12
#define AMP_DIN  13

// Audio
#define SAMPLE_RATE 16000
#define MAX_SECONDS 5

const size_t MAX_SAMPLES =
  SAMPLE_RATE * MAX_SECONDS;

const size_t MAX_WAV_SIZE =
  44 + MAX_SAMPLES * 2;

// Memoria maxima para descargar la voz
const size_t MAX_TTS_BYTES = 900000;

I2SClass microfono;
I2SClass bocina;

bool sistemaListo = false;

// ==========================================
// 3. FUNCIONES WAV
// ==========================================

void poner16(uint8_t* p, uint16_t v) {
  p[0] = v & 255;
  p[1] = (v >> 8) & 255;
}

void poner32(uint8_t* p, uint32_t v) {

  for (int i = 0; i < 4; i++) {
    p[i] = (v >> (8 * i)) & 255;
  }
}

uint16_t leer16(const uint8_t* p) {

  return (uint16_t)p[0] |
         ((uint16_t)p[1] << 8);
}

uint32_t leer32(const uint8_t* p) {

  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

void crearCabeceraWAV(
  uint8_t* wav,
  uint32_t bytesAudio
) {

  memcpy(wav, "RIFF", 4);

  poner32(wav + 4, 36 + bytesAudio);

  memcpy(wav + 8, "WAVEfmt ", 8);

  poner32(wav + 16, 16);

  poner16(wav + 20, 1);
  poner16(wav + 22, 1);

  poner32(wav + 24, SAMPLE_RATE);
  poner32(wav + 28, SAMPLE_RATE * 2);

  poner16(wav + 32, 2);
  poner16(wav + 34, 16);

  memcpy(wav + 36, "data", 4);

  poner32(wav + 40, bytesAudio);
}

// ==========================================
// 4. CODIFICACION DE TEXTO PARA URL
// ==========================================

String codificarURL(const String& texto) {

  const char* hex = "0123456789ABCDEF";

  String resultado;

  resultado.reserve(
    texto.length() * 3 + 1
  );

  for (size_t i = 0; i < texto.length(); i++) {

    uint8_t c = (uint8_t)texto[i];

    if (
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '-' ||
      c == '_' ||
      c == '.' ||
      c == '~'
    ) {

      resultado += (char)c;

    } else {

      resultado += '%';
      resultado += hex[c >> 4];
      resultado += hex[c & 15];

    }
  }

  return resultado;
}

// ==========================================
// 5. BUFFER PARA DESCARGAR AUDIO
// ==========================================

class BufferAudio : public Stream {

public:

  uint8_t* datos = nullptr;

  size_t longitud = 0;
  size_t capacidad = 0;

  BufferAudio(size_t maximo) {

    capacidad = maximo;

    datos = (uint8_t*)ps_malloc(maximo);
  }

  ~BufferAudio() {

    if (datos) {
      free(datos);
    }
  }

  size_t write(uint8_t valor) override {

    if (!datos || longitud >= capacidad) {
      return 0;
    }

    datos[longitud++] = valor;

    return 1;
  }

  size_t write(
    const uint8_t* origen,
    size_t cantidad
  ) override {

    if (!datos || !origen) {
      return 0;
    }

    if (cantidad > capacidad - longitud) {
      return 0;
    }

    memcpy(
      datos + longitud,
      origen,
      cantidad
    );

    longitud += cantidad;

    return cantidad;
  }

  int available() override {
    return 0;
  }

  int read() override {
    return -1;
  }

  int peek() override {
    return -1;
  }

  void flush() override {}
};

// ==========================================
// 6. CONECTAR WIFI
// ==========================================

bool conectarWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println();
  Serial.println("Conectando WiFi...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED) {

    if (millis() - inicio > 20000) {

      Serial.println("ERROR WIFI");

      return false;
    }

    delay(500);

    Serial.print(".");
  }

  Serial.println();
  Serial.println("WIFI CONECTADO");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  return true;
}

// ==========================================
// 7. GRABAR AUDIO DEL MICROFONO
// ==========================================

uint8_t* grabarAudio(size_t& wavSize) {

  wavSize = 0;

  uint8_t* wav =
    (uint8_t*)ps_malloc(MAX_WAV_SIZE);

  if (!wav) {

    Serial.println("ERROR: memoria PSRAM");

    return nullptr;
  }

  microfono.setPins(
    MIC_SCK,
    MIC_WS,
    -1,
    MIC_SD
  );

  bool iniciado = microfono.begin(
    I2S_MODE_STD,
    SAMPLE_RATE,
    I2S_DATA_BIT_WIDTH_32BIT,
    I2S_SLOT_MODE_MONO,
    I2S_STD_SLOT_LEFT
  );

  if (!iniciado) {

    Serial.println("ERROR: microfono I2S");

    free(wav);

    return nullptr;
  }

  Serial.println();
  Serial.println("PROXI ESCUCHANDO...");

  int32_t muestras[256];

  size_t total = 0;

  while (
    digitalRead(BOTON) == LOW &&
    total < MAX_SAMPLES
  ) {

    size_t bytes = microfono.readBytes(
      (char*)muestras,
      sizeof(muestras)
    );

    size_t cantidad =
      bytes / sizeof(int32_t);

    for (size_t i = 0; i < cantidad; i++) {

      if (total >= MAX_SAMPLES) {
        break;
      }

      int16_t muestra =
        (int16_t)(muestras[i] >> 16);

      size_t posicion =
        44 + total * 2;

      wav[posicion] = muestra & 255;

      wav[posicion + 1] =
        ((uint16_t)muestra >> 8) & 255;

      total++;
    }
  }

  microfono.end();

  uint32_t bytesAudio = total * 2;

  crearCabeceraWAV(
    wav,
    bytesAudio
  );

  wavSize = 44 + bytesAudio;

  Serial.println("GRABACION TERMINADA");

  Serial.print("Duracion: ");

  Serial.print(
    (float)total / SAMPLE_RATE
  );

  Serial.println(" segundos");

  if (total < SAMPLE_RATE / 2) {

    Serial.println("Grabacion demasiado corta.");

    free(wav);

    return nullptr;
  }

  return wav;
}

// ==========================================
// 8. GROQ WHISPER - TRANSCRIBIR
// ==========================================

String transcribirAudio(
  uint8_t* wav,
  size_t wavSize
) {

  Serial.println();
  Serial.println("WHISPER: TRANSCRIBIENDO...");

  NetworkClientSecure cliente;

  // Configuracion temporal de prototipo.
  // Sustituir por TLS verificado para publicar.
  cliente.setInsecure();

  HTTPClient http;

  if (!http.begin(
    cliente,
    "https://api.groq.com/openai/v1/audio/transcriptions"
  )) {

    Serial.println("ERROR HTTPS");

    return "";
  }

  http.setTimeout(30000);

  String boundary = "ProxiBoundary2727";

  String inicio =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; "
    "name=\"model\"\r\n\r\n"
    "whisper-large-v3-turbo\r\n";

  inicio +=
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; "
    "name=\"language\"\r\n\r\n"
    "es\r\n";

  inicio +=
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; "
    "name=\"file\"; filename=\"proxi.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";

  String final =
    "\r\n--" + boundary + "--\r\n";

  size_t total =
    inicio.length() +
    wavSize +
    final.length();

  uint8_t* paquete =
    (uint8_t*)ps_malloc(total);

  if (!paquete) {

    Serial.println("Sin memoria HTTP");

    http.end();

    return "";
  }

  memcpy(
    paquete,
    inicio.c_str(),
    inicio.length()
  );

  memcpy(
    paquete + inicio.length(),
    wav,
    wavSize
  );

  memcpy(
    paquete + inicio.length() + wavSize,
    final.c_str(),
    final.length()
  );

  http.addHeader(
    "Authorization",
    String("Bearer ") + GROQ_API_KEY
  );

  http.addHeader(
    "Content-Type",
    "multipart/form-data; boundary=" + boundary
  );

  int codigo = http.POST(
    paquete,
    total
  );

  String respuesta =
    http.getString();

  free(paquete);

  http.end();

  Serial.print("WHISPER HTTP: ");
  Serial.println(codigo);

  if (codigo != 200) {

    Serial.println(respuesta);

    return "";
  }

  JsonDocument resultado;

  if (deserializeJson(resultado, respuesta)) {

    Serial.println("ERROR JSON WHISPER");

    return "";
  }

  String texto =
    resultado["text"].as<String>();

  texto.trim();

  Serial.println();
  Serial.println("PREGUNTA:");
  Serial.println(texto);

  return texto;
}

// ==========================================
// 9. GROQ - GENERAR RESPUESTA
// ==========================================

String preguntarGroq(const String& pregunta) {

  Serial.println();
  Serial.println("PROXI PENSANDO...");

  NetworkClientSecure cliente;

  cliente.setInsecure();

  HTTPClient http;

  if (!http.begin(
    cliente,
    "https://api.groq.com/openai/v1/chat/completions"
  )) {

    Serial.println("ERROR HTTPS GROQ");

    return "";
  }

  http.setTimeout(30000);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  http.addHeader(
    "Authorization",
    String("Bearer ") + GROQ_API_KEY
  );

  JsonDocument solicitud;

  solicitud["model"] =
    "openai/gpt-oss-20b";

  solicitud["reasoning_effort"] = "low";

  solicitud["include_reasoning"] = false;

  solicitud["temperature"] = 0.4;

  solicitud["max_completion_tokens"] = 420;

  JsonArray mensajes =
    solicitud["messages"].to<JsonArray>();

  JsonObject sistema =
    mensajes.add<JsonObject>();

  sistema["role"] = "system";

  sistema["content"] =
    "Eres Proxi, un pequeno robot con "
    "inteligencia artificial creado por "
    "Maximiliano para Procastia. "
    "Responde siempre en espanol mexicano. "
    "Eres amigable, curioso y natural. "
    "Responde con una o dos frases cortas. "
    "Maximo 150 caracteres. "
    "No uses Markdown, emojis ni listas. "
    "Tu respuesta se reproducira en voz alta.";

  JsonObject usuario =
    mensajes.add<JsonObject>();

  usuario["role"] = "user";

  usuario["content"] = pregunta;

  String cuerpo;

  serializeJson(solicitud, cuerpo);

  int codigo = http.POST(cuerpo);

  String respuesta = http.getString();

  http.end();

  Serial.print("GROQ HTTP: ");
  Serial.println(codigo);

  if (codigo != 200) {

    Serial.println(respuesta);

    return "";
  }

  JsonDocument resultado;

  if (deserializeJson(resultado, respuesta)) {

    Serial.println("ERROR JSON GROQ");

    return "";
  }

  String texto =
    resultado["choices"][0]
             ["message"]["content"]
             .as<String>();

  texto.replace("\n", " ");
  texto.replace("*", "");
  texto.replace("#", "");

  texto.trim();

  // Limitar respuestas demasiado largas.
  // Buscamos un espacio para no cortar
  // en mitad de un caracter UTF-8.

  if (texto.length() > 240) {

    int corte = texto.lastIndexOf(' ', 240);

    if (corte > 0) {

      texto = texto.substring(0, corte);

      texto += ".";
    }
  }

  Serial.println();
  Serial.println("RESPUESTA DE PROXI:");
  Serial.println(texto);

  return texto;
}

// ==========================================
// 10. REPRODUCIR AUDIO WAV
// ==========================================

bool reproducirWAV(
  const uint8_t* wav,
  size_t longitud
) {

  if (
    !wav ||
    longitud < 44 ||
    memcmp(wav, "RIFF", 4) != 0 ||
    memcmp(wav + 8, "WAVE", 4) != 0
  ) {

    Serial.println("ERROR: archivo no es WAV");

    return false;
  }

  uint16_t formato = 0;
  uint16_t canales = 0;
  uint16_t bits = 0;

  uint32_t frecuencia = 0;

  size_t inicioAudio = 0;
  size_t bytesAudio = 0;

  bool formatoOK = false;
  bool datosOK = false;

  size_t posicion = 12;

  // Buscar bloques fmt y data.

  while (posicion + 8 <= longitud) {

    const uint8_t* bloque =
      wav + posicion;

    uint32_t tamano =
      leer32(bloque + 4);

    size_t inicio =
      posicion + 8;

    if (tamano > longitud - inicio) {
      break;
    }

    if (
      memcmp(bloque, "fmt ", 4) == 0 &&
      tamano >= 16
    ) {

      formato =
        leer16(wav + inicio);

      canales =
        leer16(wav + inicio + 2);

      frecuencia =
        leer32(wav + inicio + 4);

      bits =
        leer16(wav + inicio + 14);

      formatoOK = true;
    }

    if (
      memcmp(bloque, "data", 4) == 0
    ) {

      inicioAudio = inicio;

      bytesAudio = tamano;

      datosOK = true;
    }

    posicion =
      inicio + tamano + (tamano & 1);
  }

  if (
    !formatoOK ||
    !datosOK ||
    formato != 1 ||
    canales != 1 ||
    bits != 16 ||
    frecuencia != SAMPLE_RATE
  ) {

    Serial.println(
      "ERROR: formato WAV incompatible"
    );

    Serial.println(
      "Necesitamos PCM 16kHz, 16-bit, mono."
    );

    return false;
  }

  bocina.setPins(
    AMP_BCLK,
    AMP_LRC,
    AMP_DIN
  );

  bool iniciado = bocina.begin(
    I2S_MODE_STD,
    SAMPLE_RATE,
    I2S_DATA_BIT_WIDTH_16BIT,
    I2S_SLOT_MODE_STEREO
  );

  if (!iniciado) {

    Serial.println("ERROR: bocina I2S");

    return false;
  }

  Serial.println();
  Serial.println("PROXI HABLANDO...");

  int16_t estereo[512];

  size_t muestras = bytesAudio / 2;

  size_t procesadas = 0;

  bool correcto = true;

  while (procesadas < muestras) {

    size_t cantidad =
      min(
        (size_t)256,
        muestras - procesadas
      );

    for (size_t i = 0; i < cantidad; i++) {

      size_t p =
        inicioAudio + (procesadas + i) * 2;

      int16_t muestra =
        (int16_t)(
          (uint16_t)wav[p] |
          ((uint16_t)wav[p + 1] << 8)
        );

      // Volumen inicial: 50 %

      muestra = muestra / 2;

      // Audio en ambos canales

      estereo[i * 2] = muestra;

      estereo[i * 2 + 1] = muestra;
    }

    size_t bytes = cantidad * 4;

    size_t enviados =
      bocina.write(
        estereo,
        bytes
      );

    if (enviados != bytes) {

      Serial.println(
        "ERROR enviando audio I2S"
      );

      correcto = false;

      break;
    }

    procesadas += cantidad;
  }

  // Silencio breve al terminar.

  int16_t silencio[512] = {0};

  bocina.write(
    silencio,
    sizeof(silencio)
  );

  delay(100);

  bocina.end();

  Serial.println("AUDIO TERMINADO");

  return correcto;
}

// ==========================================
// 11. VOICE RSS - GENERAR VOZ
// ==========================================

bool hablar(const String& texto) {

  if (texto.length() == 0) {
    return false;
  }

  Serial.println();
  Serial.println("VOICE RSS: GENERANDO VOZ...");

  String url =
    "https://api.voicerss.org/?key=";

  url += VOICERSS_API_KEY;

  url += "&hl=es-mx";

  url += "&v=Jose";

  url += "&c=WAV";

  url += "&f=16khz_16bit_mono";

  url += "&r=0";

  url += "&src=";

  url += codificarURL(texto);

  NetworkClientSecure cliente;

  cliente.setInsecure();

  HTTPClient http;

  if (!http.begin(cliente, url)) {

    Serial.println(
      "ERROR conectando Voice RSS"
    );

    return false;
  }

  http.setTimeout(40000);

  http.useHTTP10(true);

  int codigo = http.GET();

  Serial.print("VOICE RSS HTTP: ");
  Serial.println(codigo);

  if (codigo != 200) {

    Serial.println(http.getString());

    http.end();

    return false;
  }

  BufferAudio audio(MAX_TTS_BYTES);

  if (!audio.datos) {

    Serial.println(
      "ERROR: sin memoria PSRAM para voz"
    );

    http.end();

    return false;
  }

  int recibidos =
    http.writeToStream(&audio);

  http.end();

  if (
    recibidos < 0 ||
    audio.longitud < 44
  ) {

    Serial.println(
      "ERROR descargando audio"
    );

    return false;
  }

  // Voice RSS puede responder con texto
  // de error incluso si HTTP es 200.

  if (
    audio.longitud >= 6 &&
    memcmp(audio.datos, "ERROR:", 6) == 0
  ) {

    Serial.println("ERROR DE VOICE RSS:");

    for (
      size_t i = 0;
      i < audio.longitud && i < 160;
      i++
    ) {

      Serial.write(audio.datos[i]);
    }

    Serial.println();

    return false;
  }

  Serial.print("Audio descargado: ");

  Serial.print(audio.longitud);

  Serial.println(" bytes");

  return reproducirWAV(
    audio.datos,
    audio.longitud
  );
}

// ==========================================
// 12. INICIO DEL SISTEMA
// ==========================================

void setup() {

  Serial.begin(115200);

  pinMode(
    BOTON,
    INPUT_PULLUP
  );

  delay(2000);

  Serial.println();
  Serial.println("=========================");
  Serial.println("     PROXI IA ONLINE");
  Serial.println("=========================");

  if (!psramFound()) {

    Serial.println(
      "ERROR: Activa OPI PSRAM."
    );

    return;
  }

  Serial.println("PSRAM DETECTADA");

  sistemaListo = true;

  conectarWiFi();

  Serial.println();

  Serial.println(
    "Presiona el boton para hablar."
  );
}

// ==========================================
// 13. CONVERSACION COMPLETA
// ==========================================

void loop() {

  if (!sistemaListo) {

    delay(1000);

    return;
  }

  // Esperar al boton

  if (digitalRead(BOTON) != LOW) {

    delay(20);

    return;
  }

  // Antirrebote

  delay(30);

  if (digitalRead(BOTON) != LOW) {
    return;
  }

  if (!conectarWiFi()) {

    Serial.println("No hay internet.");

    while (digitalRead(BOTON) == LOW) {
      delay(10);
    }

    return;
  }

  // --------------------------------------
  // ETAPA 1: ESCUCHAR
  // --------------------------------------

  size_t wavSize = 0;

  uint8_t* audio =
    grabarAudio(wavSize);

  while (digitalRead(BOTON) == LOW) {
    delay(10);
  }

  if (!audio) {

    Serial.println("No se grabo audio.");

    delay(300);

    return;
  }

  // --------------------------------------
  // ETAPA 2: WHISPER
  // --------------------------------------

  String pregunta =
    transcribirAudio(
      audio,
      wavSize
    );

  free(audio);

  if (pregunta.length() == 0) {

    Serial.println(
      "No se pudo transcribir."
    );

    delay(300);

    return;
  }

  // --------------------------------------
  // ETAPA 3: INTELIGENCIA ARTIFICIAL
  // --------------------------------------

  String respuesta =
    preguntarGroq(pregunta);

  if (respuesta.length() == 0) {

    Serial.println("Groq no respondio.");

    delay(300);

    return;
  }

  // --------------------------------------
  // ETAPA 4: HABLAR
  // --------------------------------------

  bool sonidoOK =
    hablar(respuesta);

  if (sonidoOK) {

    Serial.println();
    Serial.println("PROXI LISTO");

  } else {

    Serial.println();
    Serial.println(
      "No se pudo reproducir la voz."
    );
  }

  delay(500);
}