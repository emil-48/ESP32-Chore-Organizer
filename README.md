# ESP32-Chore-Organizer
 ECSE 395 Design Project
# Overview
This is a household chore tracking system with both a physical interface (LCD screen with joystick control) and a web interface. It allows users to:
- Create, view, edit, and delete chores
- Assign chores to specific people
- Set chore frequencies (daily, weekly, monthly)
- Mark chores as completed
- Track when chores are due next

## How It Works Together
1. The system boots up, connects to WiFi, and displays its IP address
2. It loads any saved chores from SPIFFS file storage
3. The user can navigate and toggle chores using the joystick
4. Anyone on the network can access the web interface to manage chores
5. Chores automatically reset based on their frequency (daily at midnight, weekly on Mondays, monthly on the 1st)
6. All changes sync between the web interface and physical display
  
## Hardware Components
- **Adafruit Feather ESP32 V2 Microcontroller**
- **LCD Display (16x2)**: Shows chore info and status
- **Joystick Module**: For navigation and toggling chore completion status
### Pin Assignments
**Joystick:**   
`VRX` -> `A2`   
`VRY` -> `A3`  
`SW` -> `15`

**LCD Display:**   
`SCA` -> `SCA`   
`SCL` -> `SCL`

**LED Module:**   
`R` -> `27`   
`Y` -> `12`  
`G` -> `33`

## Setup
1. Change network SSID and password in [ESP32-Chore-Organizer.ino](/ESP32-Chore-Organizer/ESP32-Chore-Organizer.ino) 
2. Get website IP through boot sequence, or check serial monitor
3. Enter IP into browser to access website
### Controls  
*Joystick up/down:* cycle through chores  
*Joystick left/right:* Scroll through chore name  
*Press Joystick:* Mark chore complete/incomplete

## Troubleshooting
Joystick Not Working: Try the [Joystick_Test.ino](/Joystick_Test/Joystick_Test.ino) and check the serial monitor. Can also try A0/A1 (Remember to modify pin assignments!)  
LCD Not Displaying: Try the [LCD_Test.ino](/LCD_Test/LCD_Test.ino). Also adjust the potentiometer on the back of the LCD for max contrast  
Missing Libraries: Open Library Manager on sidebar and install necessary libraries.

## Software Components

### Libraries Used
- WiFi, WebServer: For network connectivity and web interface
- SPIFFS: File storage to persist chore data
- LiquidCrystal_I2C: Controls the LCD display
- ArduinoJson: Handles JSON data formatting
- TimeLib, NTPClient: For time management and syncing

#### Data Structure
Each chore contains:
- Name
- Person assigned to
- Frequency (daily, weekly, monthly)
- Completion status
- Last completion timestamp
- Next due timestamp

### Core Functions  

#### Setup and Initialization  
- `setup()`: Initializes LCD hardware, connects to WiFi, sets up web server routes, loads saved chores
- `loop()`: Main program loop that handles user input, updates display, and refreshes chore status

#### Chore Management  
- `loadChores()` & `saveChores()`: Read/write chore data from/to file storage
- `calculateNextDueDate()`: Sets when chores become due again based on frequency
- `updateChoreStatus()`: Checks if completed chores should be reset based on their next due date

#### User Interface  
- `updateLCD()`: Refreshes the LCD display with current chore info
- `handleJoystick()`: Interprets joystick movements and button presses
- `handleNameScrolling()`: Allows scrolling through long chore names that don't fit on the LCD

#### Web Interface  
Several handler functions process web requests:
- `handleRootPage()`: Serves the main web page with HTML/CSS/JavaScript
- `handleToggleChore()`: Toggles chore completion status
- `handleAddChore()`, `handleDeleteChore()`: Create and remove chores
- `handleGetChore()`, `handleUpdateChore()`: Edit existing chores
