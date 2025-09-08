# TFG — Sistema de Asistencia para Lectores de Azúcar en Personas con Discapacidad Sensorial

Proyecto desarrollado como parte de mi **Trabajo Fin de Grado (TFG)**.  
**El proyecto ha sido bautizado como _Gluco Alert_.**  
El sistema detecta vibraciones anómalas en un lector de glucosa y notifica al cuidador mediante aviso local (luz y vibración) y remoto (llamadas/SMS).

---

## ÍNDICE

1. [Resumen](#1-resumen)  
2. [Características principales](#2-características-principales)  
3. [Hardware (BOM) y conexiones](#3-hardware-bom-y-conexiones)  
4. [Flujo de funcionamiento](#4-flujo-de-funcionamiento)  
5. [Control remoto (SMS / DTMF)](#5-control-remoto-sms--dtmf)  
6. [Compilación, flasheo y monitor](#6-compilación-flasheo-y-monitor)  
7. [Estructura de ficheros en la microSD](#7-estructura-de-ficheros-en-la-microsd)  
8. [Pruebas recomendadas](#8-pruebas-recomendadas)  
9. [Solución de problemas (Troubleshooting)](#9-solución-de-problemas-troubleshooting)  
10. [Seguridad eléctrica](#10-seguridad-eléctrica)  
11. [Licencia y citación](#11-licencia-y-citación)  
12. [Agradecimientos](#12-agradecimientos)  
13. [Vídeo de demostración (placeholder)](#13-vídeo-de-demostración-placeholder)  

---

## 1) RESUMEN

El sistema se basa en un **ESP32-C6** con firmware ESP-IDF y un módem GSM.  
Un sensor de vibración (salida digital) se procesa con antirruido (contador de ≥3 golpes en una ventana de 800 ms).  
Al dispararse la alerta, se activan tres relés (dos motores vibradores y una luz), se abre una ventana de atención de 30 s (botón físico), y si no se atiende, se realizan llamadas a números preconfigurados, reproduciendo un aviso por I2S desde la tarjeta microSD.  
Un pin de “armado” detecta la presencia del lector. Además, existe control remoto básico por SMS (con clave) y soporte DTMF en llamada.

---

## 2) CARACTERÍSTICAS PRINCIPALES

- Detección robusta de vibración con umbral configurable (≥3 en 800 ms).  
- Aviso local: dos motores vibradores + luz (a través de relés).  
- Ventana de atención: 30 s con botón físico (enciende luz de atención).  
- Llamadas automáticas a dos números, con reproducción de mensaje de voz por I2S/SD.  
- Entrada “ARM” para activar el sistema cuando el lector esté presente.  
- Control y diagnóstico por SMS (clave por defecto `0000`).  
- Prueba remota de sistema (activa relés durante 10 s, sin llamadas).  
- Diseño eléctrico con tres raíles: 5 V (lógica), 4,3 V (GSM), 12 V (motores).

---

## 3) HARDWARE (BOM) Y CONEXIONES

**Placa principal:**
- ESP32-C6-DevKitC-1

**Comunicación:**
- Módem GSM (SIM800/SIM8xx) alimentado a 4,3 V  
  (con >1000 µF cerca del VCC)

**Almacenamiento / audio:**
- Módulo microSD a 5 V (con regulador y adaptación de nivel)  
- DAC I2S integrado del ESP32-C6 (WAV mono 16-bit desde SD)

**Actuadores:**
- Módulo de 4 relés (LOW-trigger, se usan 3)  
- 2 motores vibradores (12 V conmutados por relé)  
- Luz / lamparita (red eléctrica conmutada por relé)

**Sensores y E/S locales:**
- Sensor de vibración Piezoeléctrico (salida digital, 5 V)  
- Entrada ARM  
- Botón de atención

**Alimentación:**
- 5 V → ESP32, módulo SD, módulo relés  
- 4,3 V → Módem GSM  
- 12 V → Motores vibradores  
- Condensadores: 1000 µF por raíl + 100 µF de desacoplo  
- Masa común en punto estrella cerca de GND del ESP32  
- Cableado corto para SPI y UART

**Pinout ESP32-C6:**

| Función                  | GPIO        |
|--------------------------|-------------|
| Sensor vibración (D0)    | GPIO2       |
| ARM                      | GPIO3       |
| Botón atención           | GPIO4       |
| Relé Vib1                | GPIO8       |
| Relé Vib2                | GPIO10      |
| Relé Luz                 | GPIO11      |
| Relé reservado           | GPIO12      |
| UART1 TX (SIM RX)        | GPIO7       |
| UART1 RX (SIM TX)        | GPIO6       |
| SD Card CS               | GPIO5       |
| SD SCK                   | GPIO18      |
| SD MOSI                  | GPIO19      |
| SD MISO                  | GPIO21      |

**Notas:**
- PWRKEY del SIM a GND durante 1–2 s al encender  
- DTR a GND para evitar sleep  
- SPI: si es largo, reducir frecuencia a 4–10 MHz  
- Verificar niveles de MISO del módulo SD

---

## 4) FLUJO DE FUNCIONAMIENTO

1. **Arranque:** Inicialización de GPIO, SD, I2S, UART, timers  
2. **Lectura de ARM:** IDLE (ausente) / ARMED (presente)  
3. En estado ARMED:
   - Detección de ≥3 golpes → ALARM  
   - ALARM: activa vibradores y luz, inicia temporizador de 30 s  
   - Si se pulsa el botón: se atiende → relés OFF, colgado de módem  
   - Si NO se atiende: pasa a CALLING
4. **CALLING:**
   - Realiza llamadas a NUM1 y NUM2, reproduce mensaje por I2S/SD  
   - Vib OFF, luz queda ON  
   - Luego retorna a IDLE/ARMED según ARM

---

## 5) CONTROL REMOTO (SMS / DTMF)

**Por SMS (requiere clave, ej. `0000 prueba`)**

Comandos disponibles:
- Ayuda / menú  
- Prueba de relés (10 s)  
- Consulta de estado  
- Prueba de SMS/llamadas  
- Cancelar alerta  
- Reinicio del sistema  
- Cambio de clave

**Por DTMF (durante llamada):**
- Recepción de dígitos  
- Ejecución de acciones básicas con confirmación por audio  
- Archivos `/sdcard/0.wav` … `/sdcard/9.wav` opcionales

---

## 6) COMPILACIÓN, FLASHEO Y MONITOR

**Entorno recomendado:** PlatformIO + ESP-IDF (ESP32-C6)

```bash
# Compilar
pio run

# Flashear
pio run -t upload

# Monitor serie
pio device monitor -b 115200
```

**Configuración importante:**

- Velocidad del monitor: `115200` baudios
- Tamaño de la flash: debe coincidir con la configuración del módulo (ej. 2 MB si así está indicado en `sdkconfig` o `sdkconfig.defaults`)
- Mensajes del driver I2S tipo “legacy”: son normales, el sistema usa actualmente la API antigua y está pendiente de migrar a `i2s_std`

---

## 7) ESTRUCTURA DE FICHEROS EN LA MICROSD

- `/sdcard/ALERT.WAV` → mensaje de alerta que se reproduce durante las llamadas
- `/sdcard/0.WAV` a `/sdcard/9.WAV` → (opcional) locuciones o tonos DTMF para interacción en llamada

**Recomendaciones técnicas:**

- Formato: WAV PCM, 16-bit, mono, entre 8 y 16 kHz
- Nombres de archivos en mayúsculas
- Formato del sistema de archivos: FAT/FAT32

---

## 8) PRUEBAS RECOMENDADAS

Se recomienda realizar pruebas en las siguientes condiciones:

- Vibración real del lector de glucosa (simular golpes controlados)
- Verificación de la detección de ≥3 golpes en 800 ms
- Prueba de activación y cancelación con el botón físico
- Ensayo de ciclo completo: vibración → alerta → llamadas → reproducción de audio → vuelta a reposo
- Envío de comandos SMS (clave, prueba, consulta de estado)
- Prueba con diferentes niveles de batería / voltaje
- Control DTMF (si está habilitado)

---

## 9) SOLUCIÓN DE PROBLEMAS (TROUBLESHOOTING)

**SD Card:**

- Errores `0x107`, `0x108`, `0x109`:
  - Verifica alimentación 5 V y masa común
  - Chequea conexiones SPI (CS, SCK, MOSI, MISO)
  - Usa cableado corto
  - Reduce la frecuencia SPI (4–10 MHz)
  - Cambia la tarjeta por otra y reformatea en FAT

**Módem GSM:**

- No responde:
  - Alimentación de 4,3 V estable con picos de corriente disponibles
  - Condensador ≥1000 µF cerca del VCC
  - TX y RX correctamente conectados (no cruzados)
  - PWRKEY a GND 1–2 s al arrancar
  - Antena conectada, buena cobertura
  - SIM sin PIN activo

**Relés:**

- No activan:
  - Asegúrate de que el módulo es **LOW-trigger**
  - Si es **HIGH-trigger**, invierte la lógica en el firmware

**Vibración:**

- Falsos positivos:
  - Ajustar sensibilidad del sensor (potenciómetro)
  - Alimentar con 5 V estables
  - Separar el cableado de potencia de la señal
  - Evitar ruidos mecánicos cercanos

---

## 10) SEGURIDAD ELÉCTRICA

- Para cargas a 230 V:
  - Siempre conmutar la **fase (L)**, no el neutro (N)
  - Usar borneras, conectores y aislamientos apropiados
  - Mantener distancias de seguridad (fugas, aislamiento)
  - Instalar en caja cerrada sin partes accesibles a tensión de red
  - Revisión final por **personal cualificado**

---

## 11) LICENCIA Y CITACIÓN

Este proyecto se publica bajo la siguiente licencia:

**Creative Commons — Atribución-NoComercial-SinDerivadas 4.0 Internacional**  
[CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/deed.es)

- Puedes compartir el trabajo **citando al autor**
- No se permite el uso **comercial**
- No se permiten **obras derivadas**


---

## 13) VÍDEO DE DEMOSTRACIÓN (PLACEHOLDER)

Enlace al vídeo: **[pendiente de subir]**

---
