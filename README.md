# ESP32-Chore-Organizer
 ECSE 395 Design Project
 
Arduino Sketches for ESP32-based chore organizer. There are two main components:
## Web-Server Interface
- Displays all chores, their frequency/user
- Place to perform more complex tasks, other than marking complete or incomplete
- Editing, creating new chores
## Physical Screen and Input
- Displays chores without needing to open a site
- Can mark tasks complete	

### Parts:
1. Adafruit Feather ESP32
2. I2C LCD1602
3. Joystick Module
### Pin Assignments
**Joystick:** 
- `VRX` -> `A2`
- `VRY` -> `A3` 
- `SW` -> `15`

**LCD Display:** 
- `SCA` -> `SCA` 
- `SCL` -> `SCL`

### Setup
1. Change network SSID and password in [ESP32-Chore-Organizer.ino](/ESP32-Chore-Organizer/ESP32-Chore-Organizer.ino) 
2. Get website IP through boot sequence, or check serial monitor
3. Enter IP into browser to access website
### Controls  
Joystick up/down: cycle through chores  
Joystick left/right: Scroll through chore name  
Press Joystick: Mark chore complete/incomplete
