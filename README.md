# Proxi — tu micro asistente con IA

Proxi es un pequeño robot de voz hecho con un **ESP32-S3**: le haces una pregunta, la entiende con inteligencia artificial y te responde en voz alta por su bocina.

**¡Felicidades por animarte a crear tu propio asistente con IA!** Esta es una versión sencilla que usa el plan gratuito de Groq, así que tiene algunos límites. Más adelante compartiré una versión más avanzada de Proxi, también gratis. Suscríbete a **Procast IA** en YouTube para no perdértela.

## Cómo funciona

1. Mantén presionado el botón y habla cuando aparezca `PROXI ESCUCHANDO` en el monitor serie.
2. Suelta el botón al terminar (puedes hablar hasta 5 segundos).
3. Proxi envía tu voz a **Groq**, que la convierte en texto y genera una respuesta corta.
4. **Voice RSS** convierte la respuesta en voz y Proxi la reproduce por la bocina.

Cada pregunta es independiente: Proxi todavía no recuerda conversaciones anteriores.

## Materiales

Estos son los materiales que usé. Los enlaces son de afiliado: si compras con ellos me apoyas sin que te cueste más.

| Cantidad | Material | Enlace |
|---|---|---|
| 1 | ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM) con pantalla OLED 0.96" | [Comprar](https://meli.la/2UwXFWU) |
| 1 | Micrófono INMP441 (I2S) | [Comprar](https://meli.la/1gGa1T7) |
| 1 | Amplificador MAX98357A (I2S) | [Comprar](https://meli.la/2kCNzDu) |
| 1 | Bocina 4 Ω, 3 W (el paquete trae 2) | [Comprar](https://meli.la/2KKmXdw) |
| 1 | Botón pulsador 12 × 12 mm (el paquete trae 50) | [Comprar](https://meli.la/1A1ifjE) |
| 1 | Protoboard de 830 puntos (el paquete trae 2) | [Comprar](https://meli.la/2giDBnC) |
| Varios | Cables Dupont de 20 cm | [Comprar](https://meli.la/1KTiYS3) |
| 1 | Cable USB-C **de datos** (uno solo de carga no sirve para programar) | — |

También necesitas WiFi de **2.4 GHz** y una cuenta gratuita en [Groq](https://console.groq.com/) y en [Voice RSS](https://www.voicerss.org/).

> **Importante:** el programa necesita un ESP32-S3 **con PSRAM**. Un ESP32 cualquiera no funcionará.

## Conexiones

| ESP32-S3 | Se conecta a |
|---|---|
| GPIO 4 | Botón (la otra pata del botón va a GND) |
| GPIO 8 | Micrófono INMP441 · SCK |
| GPIO 9 | Micrófono INMP441 · WS |
| GPIO 10 | Micrófono INMP441 · SD |
| GPIO 11 | Amplificador MAX98357A · BCLK |
| GPIO 12 | Amplificador MAX98357A · LRC |
| GPIO 13 | Amplificador MAX98357A · DIN |

Consejos:

- Todos los módulos deben compartir **GND**.
- Alimenta el micrófono con **3.3 V** (nunca 5 V) y conecta su pin **L/R a GND**.
- La bocina va entre **SPK+** y **SPK−** del amplificador. SPK− **no** es GND.
- Usa **una sola** bocina; dos en paralelo pueden dañar el amplificador.
- Los números son **GPIO**, no la posición del pin en la placa. Revisa el pinout de tu modelo.

## Instalación

1. Instala [Arduino IDE](https://www.arduino.cc/en/software) y, desde el gestor de tarjetas, el paquete **esp32 by Espressif Systems** (versión 3.x).
2. Desde el gestor de bibliotecas, instala **ArduinoJson** (versión 7.x).
3. Abre `firmware/Proxi/Proxi.ino`. No cambies el nombre de la carpeta `Proxi` ni del archivo.
4. En la misma carpeta, copia `config.example.h` y renombra la copia como `config.h`.
5. Abre `config.h` y escribe tu WiFi, tu clave de Groq y tu clave de Voice RSS.
6. En **Herramientas**, elige tu placa ESP32-S3, el puerto y activa la **PSRAM** según tu modelo (para la N16R8 normalmente: *ESP32S3 Dev Module*, Flash 16 MB, PSRAM OPI).
7. Sube el programa y abre el monitor serie a **115200 baudios**.

## Cuida tus claves

- Tus claves van **solo** en `config.h`. Nunca subas ese archivo ni lo muestres en capturas o videos.
- El repositorio incluye un `.gitignore` que evita subir `config.h` con Git. Si subes archivos desde el navegador, revisa la lista antes de confirmar.
- Si una clave se publica por accidente, bórrala y crea una nueva desde el panel de Groq o de Voice RSS.

---

Hecho por **Procast IA**. ¿Lo armaste? Déjame una ⭐ y cuéntame en los comentarios del video.
