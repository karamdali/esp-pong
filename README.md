# ESP-Pong: Multiplayer Pong Game over ESP-NOW

![esp-pong](https://github.com/user-attachments/assets/03bf9a76-d613-428c-ba06-00d88e60a8d8)


## Overview
ESP-Pong is a multiplayer Pong game that uses two ESP32 microcontrollers communicating via ESP-NOW protocol. One ESP32 acts as the game host (handling game logic), while the other acts as a slave (displaying the game state). Both players control paddles using analog joysticks and see the game on SSD1306 OLED displays.

## Hardware Requirements

### Components Needed:
- 2 × ESP32 development boards
- 2 × SSD1306 OLED displays (128×64 pixels)
- 2 × Analog joysticks (or potentiometers)
- Breadboards and jumper wires
- Micro USB cables for programming

### Connections:

#### For both Host and Slave:

### SSD1306 OLED Display:
- **GND** → ESP32 GND  
- **VCC** → ESP32 3.3V  
- **SCL** → GPIO 22 *(default, configurable in code)*  
- **SDA** → GPIO 21 *(default, configurable in code)*  

### Analog Joystick:
- **GND** → ESP32 GND  
- **VCC** → ESP32 3.3V  
- **VRx** → GPIO 32 *(ADC1_CHANNEL_4)*  

**Important Notes:**
- The I2C pins (SCL/SDA) can be changed in the code if needed
- ADC1_CHANNEL_4 specifically refers to GPIO32 on most ESP32 boards
- Ensure all ground connections are properly made
- Use 3.3V power sources only

**Note:** The joystick connection uses ADC1_CHANNEL_4 (GPIO32) by default, but you can modify this in the code.

## Software Setup

The game is programmed with freeRTOS and ESP-IDF

### Uploading the Programs:

1. **Configure the build:**
   - Open the project in your ESP-IDF environment
   - For the host device, uncomment `#define HOST 1` and comment out `#define SLAVE 1`
   - For the slave device, do the opposite (comment HOST, uncomment SLAVE)

2. **Set MAC addresses:**
   - In the host code, set the slave's MAC address in `peer_mac[]`
   - In the slave code, set the host's MAC address in `peer_mac[]`
   - You can find each ESP32's MAC address in the serial monitor output

## Host vs Slave Differences

| Feature          | Host (Game Server)                          | Slave (Game Client)                     |
|------------------|---------------------------------------------|-----------------------------------------|
| Game Logic       | Manages all game logic                      | Receives game state                     |
| Communication    | Sends ball and paddle positions             | Sends local paddle position             |
| Responsibilities | Ball movement, collisions, scoring          | Input handling and display              |
| Complexity       | More complex game state management          | Focused on display and local input      |

## Gameplay Instructions

1. Power both ESP32 devices
2. The host will initialize the game
3. Use the joysticks to move your paddles:
   - Left/right movement controls paddle position
   - The further you push the joystick, the faster the paddle moves
4. The game automatically resets when a player misses the ball

## Troubleshooting

| Issue               | Solution                                  |
|---------------------|-------------------------------------------|
| Connection issues   | Verify MAC addresses are correctly set    |
| Display problems    | Check I2C connections and contrast       |
| slave/host paddle not displayed| just move the joystick  |
| Input lag           | Ensure both devices have latest firmware  |
| Performance issues  | Reduce the `MAX_SPEED` value in the code |

## Customization Options

You can modify these constants in the code:
- `MAX_SPEED`: Changes game speed
- `paddle_WIDTH`/`paddle_HEIGHT`: Adjust paddle size
- `BALL_DIMENTION`: Change ball size
- Screen dimensions (if using different displays)

## License
This code is provided AS IS without warranty. Feel free to use and modify it for any purpose.

## Author
Karam Dali - Damascus (09/05/2025)

