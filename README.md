# ESP32 ToneHub - Controlador de Tono WiFi

![ToneHub](https://img.shields.io/badge/ESP32-C3-orange) ![License](https://img.shields.io/github/license/jorgelserve/esp32oled) ![Version](https://img.shields.io/badge/version-1.0.0--rc1-blue) ![Build](https://img.shields.io/badge/build-passing-brightgreen)

## Descripción

ESP32 ToneHub es un controlador de tono WiFi basado en ESP32-C3 que permite controlar remotamente la generación de tonos a través de una interfaz web y controles hardware. El dispositivo crea su propia red WiFi, permitiendo el control simultáneo desde múltiples dispositivos conectados.

## Características

- ✅ **Control de tono remoto** - Ajuste el tono desde cualquier dispositivo conectado
- ✅ **Controles hardware** - Botón selector y potenciómetro para control físico
- ✅ **Botón de parada** - Botón físico en pin 7 y botón web para detener el tono
- ✅ **Sincronización en tiempo real** - Todos los dispositivos muestran el estado actual
- ✅ **Filtrado de ADC** - Elimina ruido del potenciómetro con filtro de media móvil exponencial
- ✅ **Interfaz OLED** - Visualización de estado en una pantalla SSD1306 72x40
- ✅ **Comunicación WebSocket** - Comunicación bidireccional en tiempo real
- ✅ **Sistema de stop/resume** - Funcionalidad de detención y reanudación del tono

## Hardware Requerido

- **ESP32-C3 DevKitM-1** (o placa compatible con ESP32-C3)
- **OLED SSD1306 72x40** (conexión I2C)
- **Potenciómetro de 100K** (conectado al pin ADC)
- **Botón pulsador** (conectado al pin 7 con pull-up)
- **Botón selector** (conectado al pin 9 con pull-up)
- **LED indicador** (conectado al pin 8)

### Conexiones

| Componente | Pin ESP32-C3 | Notas |
|------------|--------------|--------|
| OLED SDA | GPIO 6 | Datos I2C |
| OLED SCL | GPIO 5 | Reloj I2C |
| Potenciómetro | GPIO 4 | ADC1_CH4 |
| Botón Stop | GPIO 7 | Con pull-up interno |
| Botón Selector | GPIO 9 | Con pull-up interno |
| LED Estado | GPIO 8 | Control de estado |

## Configuración

### Dependencias

- **PlatformIO** como framework de desarrollo
- **Arduino Core** para ESP32
- Librerías requeridas:
  - `olikraus/U8g2` para control de OLED
  - `Links2004/WebSockets` para comunicación WebSocket

### Configuración WiFi

El ESP32 crea una red WiFi de acceso directo (AP) con:
- **SSID**: `ToneHub-ESP32`
- **Contraseña**: `ToneHub123`
- **IP**: `192.168.4.1`

> ⚠️ **Nota Importante**: El dispositivo opera como punto de acceso, no se conecta a una red WiFi externa.

## Instalación

1. **Clonar el repositorio**
   ```bash
   git clone https://github.com/jorgelserve/esp32oled.git
   cd esp32oled
   ```

2. **Instalar dependencias**
   ```bash
   # Si usa PlatformIO CLI
   pio run
   ```

3. **Conectar el hardware**
   - Conecte todos los componentes según el diagrama de conexiones

4. **Cargar el firmware**
   ```bash
   pio run -t upload
   ```

5. **Conectar a la red WiFi**
   - Conéctese a `ToneHub-ESP32` con contraseña `ToneHub123`
   - Abra un navegador y vaya a `http://192.168.4.1`

## Uso

### Controles de Hardware

- **Potenciómetro**: Ajusta el valor normalizado del tono (0.0 - 1.0)
- **Botón Selector (Pin 9)**: Cambia entre pantallas de la interfaz OLED
- **Botón Stop (Pin 7)**: Alterna entre detener/reanudar el tono

### Interfaz Web

- **Control deslizante**: Ajusta el tono con control preciso
- **Botón "Enable Audio Output"**: Habilita la reproducción de audio
- **Botón "Stop Tone"**: Detiene el tono en todos los dispositivos
- **Controles de rango**: Ajuste los límites de frecuencia (min/max Hz)

### Indicadores OLED

- **Pantalla 0**: Información del AP (IP, clientes conectados)
- **Pantalla 1**: Control de tono (valor normalizado, frecuencia)
- **Pantalla 2**: Información de configuración (SSID, contraseña)

## Funcionalidades Especiales

### Filtro de ADC

El potenciómetro utiliza un filtro de media móvil exponencial para:
- Eliminar ruido de lectura del ADC
- Prevenir transmisiones innecesarias por fluctuaciones menores
- Mantener respuesta sensible al movimiento intencional

### Sistema de Stop/Resume

Diseñado con múltiples capas de control:

1. **Detención local**: El tono se detiene en el dispositivo web
2. **Sincronización remota**: Comando de stop enviado a todos los clientes
3. **Controles duales**: Botón físico y botón web con funcionalidad idéntica
4. **Reanudación inteligente**: El tono se reanuda al último valor conocido

### Gestión de estados

- **Estado de tono**: `Reproduciendo` o `Detenido`
- **Sincronización**: Todos los dispositivos mantienen el mismo estado
- **Recuperación**: Gestión de reconexiones y pérdida temporal de conectividad

## API WebSocket

### Comandos de entrada

| Comando | Descripción | Ejemplo |
|---------|-------------|---------|
| `ping` | Verificación de conectividad | `"ping"` |
| `stop` | Detiene el tono | `"stop"` |
| `next` | Cambia a siguiente tono predefinido | `"next"` |
| `normalized` | Ajusta valor normalizado | `"normalized:0.5"` |

### Mensajes de salida

| Tipo | Descripción | Ejemplo |
|------|-------------|---------|
| Normalizado | Valor actual (0.0000 - 1.0000) | `"0.1234"` |
| Stop | Indicación de tono detenido | `"stop"` |
| Pong | Confirmación de ping | `"pong"` |

## Desarrollo

### Estructura de proyecto

```
esp32oled/
├── src/
│   ├── main.cpp          # Firmware principal del ESP32
│   └── web_bundle.h      # Interfaz web comprimida
├── web/
│   └── index.html        # Interfaz web (fuente original)
├── scripts/
│   └── build_web_bundle.py  # Script de empaquetado
├── platformio.ini        # Configuración del proyecto
└── AGENTS.md             # Documentación del repositorio
```

### Comandos útiles

```bash
# Compilar el proyecto
pio run

# Cargar firmware al ESP32
pio run -t upload

# Abrir monitor serial
pio device monitor

# Regenerar bundle web
python scripts/build_web_bundle.py
```

### Contribuciones

Se aceptan contribuciones que mejoren:
- Estabilidad del sistema
- Nueva funcionalidad
- Documentación
- Corrección de errores

## Solución de problemas

### Problemas comunes

- **No se conecta al WiFi**: Verifique que el ESP32 se ha encendido y el LED está encendido
- **Tono no cambia**: Asegúrese de que el potenciómetro esté correctamente conectado
- **Pantalla no responde**: Verifique las conexiones I2C del OLED
- **Botones no responden**: Confirme que los pull-ups están configurados

### Depuración

- Conecte el monitor serial para ver mensajes de depuración
- El firmware imprime mensajes de estado por el puerto serial a 115200 baud

## Licencia

Este proyecto está licenciado bajo la Licencia MIT - ver el archivo [LICENSE](LICENSE) para detalles.

## Versiones

- **1.0.0-rc1** - Versión candidata con funcionalidad completa de stop/resume, filtrado de ADC y controles mejorados

---

**Proyecto desarrollado con ❤️ para la comunidad maker**