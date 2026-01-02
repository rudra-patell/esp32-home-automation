# ESP32 Home Automation with SinricPro

A cost-effective and reliable home automation solution built with ESP32 and SinricPro integration, featuring intelligent fallback mode for local control when cloud connectivity is unavailable.

## Features

- **Cloud Control**: Full integration with SinricPro for remote control via smartphone app or voice assistants (Alexa, Google Home)
- **Local Fallback Mode**: Automatic Access Point (AP) mode with local web UI when cloud connection fails
- **Multi-Switch Control**: Control multiple relays/switches for various home appliances
- **WiFi Configuration**: Easy WiFi setup through captive portal web interface
- **Smart Status Indicators**: Visual LED feedback for system status
- **PlatformIO Based**: Modern development workflow with dependency management

## LED Status Indicators

The built-in blue LED provides real-time system status feedback:

| LED Pattern | Status | Description |
|------------|--------|-------------|
| **Fast Blinking** | Connecting | Attempting to connect to WiFi network and SinricPro server |
| **Solid On** | Connected | Successfully connected to WiFi and cloud services |
| **Slow Blinking** | Fallback Mode | Failed to connect to network; running in local AP mode |

## Operating Modes

### 1. Normal Mode (Cloud Connected)
When successfully connected to your WiFi network and SinricPro:
- Control devices remotely through SinricPro app
- Voice control via Alexa or Google Home
- Blue LED stays solid on
- Full cloud features available

### 2. Fallback Mode (Local Control)
When WiFi connection fails or is unavailable:
- ESP32 automatically creates a WiFi Access Point (AP)
- Local web server starts for device control
- Blue LED blinks slowly
- Connect to the ESP32's WiFi network
- Access web UI to:
  - Control switches/relays directly
  - Configure WiFi credentials
  - Monitor device status

## Hardware Requirements

- ESP32 Development Board (ESP32-WROOM-32 or similar)
- Relay modules (depends on number of switches needed)
- Blue LED (with appropriate resistor, if not built-in)
- Power supply (5V recommended)
- Home appliances/devices to control

## Pin Configuration

Update these pins in your main code according to your hardware setup. Example configuration:

```cpp
// Example pin definitions (adjust according to your wiring)
#define LED_PIN 2              // Blue status LED (built-in on most ESP32 boards)
#define RELAY_PIN_1 16          // First relay switch
#define RELAY_PIN_2 17          // Second relay switch
#define RELAY_PIN_3 18         // Third relay switch
#define RELAY_PIN_4 19         // Fourth relay switch
#define IR_SENSOR_PIN 34       // IR proximity sensor pin
// Add more relay pins as needed
```

## Software Requirements

- [PlatformIO](https://platformio.org/) IDE or VS Code with PlatformIO extension
- SinricPro account (free tier available at [sinric.pro](https://sinric.pro))
- Arduino framework for ESP32

## Installation & Setup

### 1. Clone the Repository
```bash
git clone https://github.com/rudra-patell/esp32-home-automation.git
cd esp32-home-automation
```

### 2. Install PlatformIO
If you haven't installed PlatformIO:
- **VS Code**: Install PlatformIO IDE extension
- **CLI**: `pip install platformio`

### 3. Configure SinricPro Credentials
Create or edit the configuration file with your SinricPro credentials:
- App Key
- App Secret
- Device IDs for each switch

### 4. Configure WiFi Credentials (Optional)
You can hardcode WiFi credentials or set them up through the fallback web UI on first boot.

### 5. Build and Upload
```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output (optional)
pio device monitor
```

## First Time Setup

1. **Power on the ESP32** - Blue LED will start fast blinking
2. **If WiFi credentials not configured**:
   - Device enters fallback mode (slow blinking LED)
   - Connect to the ESP32's WiFi AP (check serial monitor for AP name)
   - Open browser and navigate to default gateway (usually `192.168.4.1`)
   - Enter your WiFi credentials in the web interface
   - Device will restart and connect to your network
3. **Once connected** - Blue LED becomes solid, device registers with SinricPro

## Usage

### Remote Control (Normal Mode)
1. Open SinricPro app on your smartphone
2. Control your devices from anywhere with internet connection
3. Use voice commands with Alexa or Google Home

### Local Control (Fallback Mode)
1. Connect to ESP32's WiFi network
2. Open browser and go to `192.168.4.1` (or shown IP)
3. Use web interface to toggle switches
4. Configure network settings if needed

## Troubleshooting

### Blue LED keeps fast blinking
- Check WiFi credentials are correct
- Verify WiFi network is available
- Check router settings (2.4GHz network required for ESP32)

### Blue LED slow blinking immediately after boot
- Device can't connect to configured WiFi
- Either network is down or credentials are incorrect
- Connect to fallback AP to reconfigure

### Can't access web UI in fallback mode
- Ensure you're connected to the ESP32's WiFi network
- Try `192.168.4.1` in browser
- Check serial monitor for actual IP address

### SinricPro not responding
- Verify internet connection
- Check SinricPro credentials in code
- Ensure device IDs match your SinricPro account
- Check SinricPro service status

## Project Structure

```
esp32-home-automation/
├── src/                    # Source code files
│   └── main.cpp           # Main application code
├── include/               # Header files
├── lib/                   # Project libraries
├── platformio.ini         # PlatformIO configuration
└── README.md             # This file
```

## Dependencies

Main libraries used (managed by PlatformIO). Add these to your `platformio.ini`:

```ini
lib_deps = 
    sinricpro/SinricPro @ ^2.10.0
    me-no-dev/ESPAsyncWebServer @ ^1.2.3
    bblanchon/ArduinoJson @ ^6.21.0
```

Core ESP32 libraries (included by default):
- WiFi (built-in)
- WebServer (built-in)
- DNSServer (for captive portal)

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

## License

This project is open source. Please check the LICENSE file for more details.

## Acknowledgments

- [SinricPro](https://sinric.pro) for excellent IoT platform
- [PlatformIO](https://platformio.org) for development environment
- ESP32 community for resources and support

## Support

For issues and questions:
- Open an issue on GitHub
- Check SinricPro documentation
- Review PlatformIO forums

## Safety Warning

⚠️ **IMPORTANT**: This project involves controlling AC powered devices. Please ensure:
- Proper electrical safety measures are followed
- Qualified electrician handles AC wiring
- Appropriate relays rated for your load are used
- All connections are properly insulated
- Local electrical codes are followed

## Future Enhancements

Potential features for future releases:
- MQTT support for additional integration options
- Scheduling and automation rules
- Energy monitoring
- OTA (Over-The-Air) firmware updates
- Multi-language web interface
- Mobile app for local control

---

**Made with ❤️ for the maker community**
