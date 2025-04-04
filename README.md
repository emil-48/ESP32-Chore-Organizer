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
Adafruit Feather ESP32
I2C LCD1602
Joystick Module
### Pin Assignments
**Joystick**
`VRX` -> `A2` 
`VRY` -> `A3`
`SW` -> `15`

**LCD Display**
`SCA` -> `SCA`
`SCL` -> `SCL`