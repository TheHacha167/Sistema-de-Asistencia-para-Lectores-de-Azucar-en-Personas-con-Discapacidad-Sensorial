# Lista de materiales (BOM) — Gluco Alert

Este documento recoge el *Bill of Materials* del proyecto **Glauco Alert** (TFG). Incluye los componentes principales, su función en el sistema y el motivo de elección, así como materiales recomendados adicionales para el montaje seguro y ordenado.

> **Nota:** La selección está orientada a un montaje doméstico con **ESP32‑C6 + módem GSM** y actuación por **relés** (motores de vibración y luz). Ajusta cantidades y referencias exactas según tu diseño final.

---

## 1) Componentes principales

| Componente                              | Modelo / referencia                                  | Función en el sistema                                     | Motivo de elección                                                                 |
|-----------------------------------------|-------------------------------------------------------|-----------------------------------------------------------|-------------------------------------------------------------------------------------|
| **ESP32‑C6**                            | DevKitC‑1 (u otro compatible)                         | Unidad de procesamiento principal                         | MCU con Wi‑Fi/BLE/802.15.4; permite futura expansión (Zigbee/BLE).                 |
| **Módulo GSM**                          | **SIM800L / SIM8xx** con antena                       | Envío de **SMS** y **llamadas** de emergencia             | Comunicación sin necesidad de internet.                                             |
| **Módulo de relé (5 V, optoacoplado)**  | Placa de **1–4–8–16** canales                         | Activación de actuadores: **luces**, **vibradores**       | Aislamiento para proteger el microcontrolador; fácil cableado de potencia.         |
| **Motor de vibración DC**               | 12 V, encapsulado                                     | **Alerta física** (p. ej. bajo la almohada)               | Robusto y fácil de accionar mediante relé a 12 V.                                   |
| **Botón iluminado Ø60 mm**              | Pulsador con LED integrado                            | **Atender / prueba / acción** manual                      | Alta visibilidad y fácil pulsación.                                                 |
| **Bloques de terminales**               | **KF301** (2P–4P)                                     | Conexión de alimentación y actuadores                     | Conector de tornillo fiable y fácil de montar/desmontar.                            |
| **Kit pines 2.54 mm**                   | Macho/hembra, tiras rompibles                         | Interconexión y soldadura en placas                       | Compatible con perfboard/prototipado.                                               |
| **Conectores JST**                      | JST‑XH/JST‑PH (2–5 pines, según uso)                  | Conexiones limpias entre módulos                          | Útiles para alimentación y sensores; evitan falsos contactos.                       |
| **Conectores GX12/GX16/GX20**           | Circulares metálicos con bloqueo (≥4 pines)           | Conexiones externas resistentes                           | Cuerpo metálico con rosca; conexión segura.                                         |
| **Fuente de alimentación**              | 12 V para motores; **USB‑C PD** / 5 V para electrónica| Proveer energía a cada raíl                               | Dimensionar según consumo total; separa potencia (12 V) de lógica (5 V).            |
| **Regulador buck DC‑DC**                | **LM2596** (hasta 3 A)                                | Adaptar tensiones de entrada a 5 V / 4.3 V (GSM)          | Eficiente; protege frente a sobre/infra‑tensión si se ajusta correctamente.         |
| **Tornillería M3**                      | Tornillos **DIN912** + tuercas de **inserción**       | Ensamblaje en carcasa impresa                            | Compatibles con diseño mecánico (Fusion 360); montaje sólido.                       |
| **Imanes de neodimio**                  | Circulares, alta potencia                             | Anclaje de funda al cuerpo principal                      | Opcional según diseño físico.                                                       |
| **Conectores pogo magnéticos**          | Pines de resorte + base magnética                     | Pruebas/módulos fácilmente desconectables                 | Conexión rápida y segura en piezas móviles.                                         |
| **Impresora 3D**                        | **Artillery Genius Pro**                               | Fabricación de carcasa y soportes                         | Impresión precisa; cama caliente; extrusor directo.                                 |
| **Filamento PLA**                       | **Winkle PLA HD 1.75 mm (Malva, 1 kg)**               | Material de la carcasa                                    | Buena tolerancia dimensional y acabado; fácil de imprimir.                          |

---

## 2) Materiales recomendados adicionales

- **Termorretráctil** (varios diámetros): aislamiento y alivio de tensiones en uniones.
- **Cableado para red eléctrica 230 V**: **1.5 mm²** (≈ **AWG 15/16**) con manguera de 2/3 conductores según norma local; **conductor de tierra** si procede.
- **Cableado de señal**: 22–24 AWG multihilo para UART/SPI/SEÑALES de baja corriente.
- **Standoffs / separadores**: **plástico** (nylon) preferible a bronce para evitar cortos.
- **Bridas y anclajes adhesivos**: guiado de mazos y descarga de tracción.
- **Cinta Kapton / Doble cara**: fijación térmica y sujeción ligera.
- **Malla desoldadora (solder wick)** y **bomba de desoldar**: retrabajos limpios.
- **Estaño con flux** (Sn60Pb40 o SAC lead‑free) y **flux** adicional (no‑clean).
- **Multímetro** y **comprobador de continuidad**; opcional **analizador lógico** para SPI/UART.

> **Seguridad:** Toda la parte a **230 V** debe respetar normativa local (distancias, aislamiento, puesta a tierra, envolvente). Si hay dudas, que lo revise un técnico cualificado.

---

## 3) Notas de integración

- **Raíles de alimentación** recomendados: **12 V** (motores), **5 V** (lógica/relés), **4.3 V** (GSM). Masas unidas cerca del ESP32‑C6.
- **SD a 5 V** con regulador/adaptación de niveles habituales; **SPI** con cableado corto y, si hace falta, bajar frecuencia (4–10 MHz).
- **UART GSM** (SIM800L): TX/RX cruzados; **condensador ≥1000 µF** cerca de VBAT; PWRKEY a GND 1–2 s en arranque.
- **Relés**: si son **LOW‑trigger**, verificar lógica en firmware; considerar **snubber RC/TVS** en motores a 12 V.

---

## 4) Referencias de impresión 3D (resumen)

- **Impresora:** Artillery Genius Pro  
- **Material:** Winkle PLA HD 1.75 mm (Malva) — [enlace del fabricante](https://winkle.shop/producto/filamento-pla-hd-winkle-175-mm-1-kg-malva/)  
- **Cura:** capa **0.20 mm**, infill **gyroid 15%**, soportes **orgánicos** y **autogenerados**.

---

> *Tabla 4. Hardware usado.*  
> Proyecto: **Glauco Alert — Sistema de Asistencia para Lectores de Azúcar en Personas con Discapacidad Sensorial (TFG)**.  
> Licencia: **CC BY‑NC‑ND 4.0**.
