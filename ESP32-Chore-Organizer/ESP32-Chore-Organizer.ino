/**************************************************
Chore Organizer Dashboard and Web Interface
Run on Adafruit Feather ESP32 V2
https://github.com/emil-48/ESP32-Chore-Organizer
**************************************************/
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <algorithm> // For std::sort

// Wifi settings
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";

// Pin definitions
const int JOYSTICK_X_PIN = A2;
const int JOYSTICK_Y_PIN = A3;
const int JOYSTICK_SW_PIN = 15;
const int RED_LED_PIN = 27;      // WiFi failure indicator and overdue indicator
const int GREEN_LED_PIN = 33;    // All chores completed indicator
const int YELLOW_LED_PIN = 12;   // Uncompleted chores indicator

// Points for completing chores
const int DAILY_POINTS = 1;
const int WEEKLY_POINTS = 5;
const int MONTHLY_POINTS = 20;

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16 columns, 2 rows

// Web server
WebServer server(80);

// NTP for time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Chore data structure
struct Chore {
  String name;
  String person;
  String frequency;  // "daily", "weekly" or "monthly"
  bool completed;
  time_t lastCompleted;
  time_t nextDue;
  bool pointsAwarded;  // Track if points have been awarded for the current cycle
};

// User data structure
struct User {
  String username;
  int points;  // Added points field to track user points
};

// Global variables
std::vector<Chore> chores;
std::vector<User> users;
int currentChoreIndex = 0;

// Variables for joystick
bool joystickPressed = false;
unsigned long lastJoystickDebounce = 0;
const int debounceDelay = 300;  // ms
unsigned long buttonPressStartTime = 0;
const int LONG_PRESS_DURATION = 1000; // 1 seconds for long press

// Variables for confirmation dialog
bool inConfirmationDialog = false;
bool confirmationSelection = true;  // true for "Y", false for "N"

// Variables for auto name scrolling
int scrollPosition = 0;
unsigned long lastScrollTime = 0;
const int scrollDelay = 400;  // ms - slower for auto scrolling
bool pauseScrolling = false;           // Flag to indicate if we're in pause state
unsigned long pauseStartTime = 0;      // When the pause started
const int SCROLL_PAUSE_DURATION = 2000; // 2 seconds pause at beginning

void setup() {
  Serial.begin(115200);
  
  // Initialize LED pins
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  
  // Set initial LED states
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Chore Taskboard");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  
  // Initialize joystick button with pullup
  pinMode(JOYSTICK_SW_PIN, INPUT_PULLUP);
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SPIFFS failed!");
    return;
  }
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 1);
    lcd.print("WiFi connecting");
    wifiAttempts++;
  }
  
  // Wifi error message and turn on RED LED if connection failed
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(RED_LED_PIN, HIGH);  // Turn on RED LED
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi failed!");
    Serial.println("WiFi connection failed");
    return;
  }
  
  // Print IP address
  Serial.print("Connected to WiFi. IP: ");
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connected!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000);
  
  // Initialize NTP time client
  timeClient.begin();
  timeClient.setTimeOffset(0);  // Adjust to your timezone in seconds
  timeClient.update();
  
  // Set system time from NTP
  setTime(timeClient.getEpochTime());
  
  // Setup web server routes
  server.on("/", HTTP_GET, handleRootPage);
  server.on("/toggle", HTTP_GET, handleToggleChore);
  server.on("/add", HTTP_GET, handleAddChore);
  server.on("/delete", HTTP_GET, handleDeleteChore);
  server.on("/edit", HTTP_GET, handleGetChore);
  server.on("/update", HTTP_GET, handleUpdateChore);
  
  // User management routes
  server.on("/users", HTTP_GET, handleListUsers);
  server.on("/addUser", HTTP_GET, handleAddUser);
  server.on("/deleteUser", HTTP_GET, handleDeleteUser);
  server.on("/editUser", HTTP_GET, handleGetUser);
  server.on("/updateUser", HTTP_GET, handleUpdateUser);
  
  // Reset points route
  server.on("/resetPoints", HTTP_GET, handleResetPoints);
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
  
  // Load chores from file
  loadChores();
  
  // Load users from file
  loadUsers();
  
  // Initial LCD update
  updateLCD();
  
  // Initial LED update
  updateLEDStatus();
}

void loop() {
  // Handle web server client requests
  server.handleClient();
  
  // Read and process joystick input
  handleJoystick();
  
  // Handle automatic name scrolling
  handleAutoScrolling();

  // Small delay to prevent CPU hogging
  delay(10);
  
  // Update all chores status periodically (every minute)
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime > 60000) {
    updateChoreStatus();
    updateLEDStatus();
    lastCheckTime = millis();
  }
  
  // Update NTP time every hour
  static unsigned long lastNtpUpdate = 0;
  if (millis() - lastNtpUpdate > 3600000) {  // Every hour
    timeClient.update();
    setTime(timeClient.getEpochTime());
    lastNtpUpdate = millis();
  }
}

// Load users from file
void loadUsers() {
  users.clear();
  
  if (SPIFFS.exists("/users.json")) {
    File file = SPIFFS.open("/users.json", "r");
    if (file) {
      StaticJsonDocument<4096> doc;
      DeserializationError error = deserializeJson(doc, file);
      
      if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject userObj : array) {
          User user;
          user.username = userObj["username"].as<String>();
          user.points = userObj["points"] | 0;  // Default to 0 if not present
          users.push_back(user);
        }
      }
      file.close();
    }
  }
  
  // Add default user if no users exist
  if (users.empty()) {
    User defaultUser;
    defaultUser.username = "Default";
    defaultUser.points = 0;
    users.push_back(defaultUser);
    saveUsers();
  }
}

// Save users to file
void saveUsers() {
  StaticJsonDocument<4096> doc;
  JsonArray array = doc.to<JsonArray>();
  
  for (User& user : users) {
    JsonObject userObj = array.add<JsonObject>();
    userObj["username"] = user.username;
    userObj["points"] = user.points;
  }
  
  File file = SPIFFS.open("/users.json", "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

// Function to check if a chore is overdue
bool isChoreOverdue(const Chore& chore) {
  // A chore is overdue if:
  // 1. It's not completed
  // 2. The current time is past the due date
  return (!chore.completed && now() > chore.nextDue);
}

// Load chores from file
void loadChores() {
  chores.clear();
  
  if (SPIFFS.exists("/chores.json")) {
    File file = SPIFFS.open("/chores.json", "r");
    if (file) {
      StaticJsonDocument<4096> doc;
      DeserializationError error = deserializeJson(doc, file);
      
      if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject choreObj : array) {
          Chore chore;
          chore.name = choreObj["name"].as<String>();
          chore.person = choreObj["person"].as<String>();
          chore.frequency = choreObj["frequency"].as<String>();
          chore.completed = choreObj["completed"];
          chore.lastCompleted = choreObj["lastCompleted"];
          chore.pointsAwarded = choreObj["pointsAwarded"] | false;  // Default to false if not present
          
          calculateNextDueDate(chore);
          
          chores.push_back(chore);
        }
      }
      file.close();
    }
  }
  
  updateLEDStatus();
}

// Save chores to file
void saveChores() {
  StaticJsonDocument<4096> doc;
  JsonArray array = doc.to<JsonArray>();
  
  for (Chore& chore : chores) {
    JsonObject choreObj = array.add<JsonObject>();
    choreObj["name"] = chore.name;
    choreObj["person"] = chore.person;
    choreObj["frequency"] = chore.frequency;
    choreObj["completed"] = chore.completed;
    choreObj["lastCompleted"] = chore.lastCompleted;
    choreObj["pointsAwarded"] = chore.pointsAwarded;
  }
  
  File file = SPIFFS.open("/chores.json", "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
  
  updateLEDStatus();
}

// Calculate next due date for a chore
void calculateNextDueDate(Chore& chore) {
  if (chore.lastCompleted == 0) {
    // Never completed before, due now
    chore.nextDue = now();
    return;
  }
  
  if (chore.frequency == "daily") {
    // Due next day at midnight
    time_t nextDue = chore.lastCompleted;
    
    // Get current day components
    tmElements_t tm;
    breakTime(nextDue, tm);
    
    // Set to next day, midnight
    tm.Hour = 0;
    tm.Minute = 0;
    tm.Second = 0;
    nextDue = makeTime(tm) + (24 * 3600);  // Add 24 hours for next day
    
    chore.nextDue = nextDue;
  }
  else if (chore.frequency == "weekly") {
    // Due next Monday after completion date
    time_t nextDue = chore.lastCompleted + (7 * 24 * 3600);  // Add 7 days
    
    // Adjust to next Monday
    while (weekday(nextDue) != 2) {  // 2 is Monday in TimeLib
      nextDue += 24 * 3600;  // Add one day
    }
    
    chore.nextDue = nextDue;
  }
  else if (chore.frequency == "monthly") {
    // Due first day of next month
    tmElements_t tm;
    breakTime(chore.lastCompleted, tm);
    
    // Move to first day of next month
    tm.Day = 1;
    tm.Month++;
    if (tm.Month > 12) {
      tm.Month = 1;
      tm.Year++;
    }
    
    chore.nextDue = makeTime(tm);
  }
}

// Get days until a chore is due
int getDaysUntilDue(Chore& chore) {
  if (!chore.completed) {
    return 0;  // Due now if not completed
  }
  
  time_t current = now();
  if (chore.nextDue > current) {
    return (chore.nextDue - current) / (24 * 3600);  // Convert seconds to days
  }
  
  return 0;  // Due now if past due date
}

// Get days until next scheduled reset based on frequency
int getDaysUntilReset(Chore& chore) {
  time_t current = now();
  
  if (chore.frequency == "daily") {
    // Calculate days until tomorrow
    tmElements_t tm;
    breakTime(current, tm);
    
    // Set to next day, midnight
    tm.Hour = 0;
    tm.Minute = 0;
    tm.Second = 0;
    time_t tomorrow = makeTime(tm) + (24 * 3600);  // Add 24 hours for next day
    
    return 1;  // Always 1 day for daily tasks
  }
  else if (chore.frequency == "weekly") {
    // Calculate days until next Monday
    int dayOfWeek = weekday(current);  // 1=Sunday, 2=Monday, ..., 7=Saturday
    int daysUntilMonday = (9 - dayOfWeek) % 7;  // Days until next Monday
    if (daysUntilMonday == 0) daysUntilMonday = 7;  // If today is Monday, next reset is in 7 days
    
    return daysUntilMonday;
  }
  else if (chore.frequency == "monthly") {
    // Calculate days until 1st of next month
    tmElements_t tm;
    breakTime(current, tm);
    
    // Get days in current month
    int daysInMonth;
    if (tm.Month == 4 || tm.Month == 6 || tm.Month == 9 || tm.Month == 11) {
      daysInMonth = 30;
    } else if (tm.Month == 2) {
      // Check for leap year
      int year = tm.Year + 1970;
      bool isLeapYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
      daysInMonth = isLeapYear ? 29 : 28;
    } else {
      daysInMonth = 31;
    }
    
    return daysInMonth - tm.Day + 1;  // Days left in month plus 1 for the 1st of next month
  }
  
  return 0;  // Default case
}

// Update all chores status
void updateChoreStatus() {
  time_t current = now();
  bool changed = false;
  
  for (Chore& chore : chores) {
    if (chore.completed && current >= chore.nextDue) {
      // Reset chore if it's past due
      chore.completed = false;
      chore.pointsAwarded = false; // Reset pointsAwarded flag for the new cycle
      changed = true;
    }
  }
  
  if (changed) {
    saveChores();
    updateLCD();
  }
}

// Update LCD display
void updateLCD() {
  lcd.clear();
  
  if (chores.empty()) {
    lcd.setCursor(0, 0);
    lcd.print("No chores!");
    lcd.setCursor(0, 1);
    lcd.print("Add via web app");
    return;
  }
  
  // Display current chore
  Chore& chore = chores[currentChoreIndex];
  
  // Line 1: Name and completion status
  lcd.setCursor(0, 0);
  lcd.print(chore.completed ? "[X] " : "[ ] ");
  
  // Handle long name scrolling
  String displayName;
  if (chore.name.length() > 11) {
    // If name is too long, show a portion based on scroll position
    int endPos = min(scrollPosition + 11, (int)chore.name.length());
    displayName = chore.name.substring(scrollPosition, endPos);
    
    // Pad with spaces if the substring is less than 11 chars
    while (displayName.length() < 11) {
      displayName += " ";
    }
  } else {
    displayName = chore.name;
  }
  lcd.print(displayName);
  
  // Line 2: Person and days till due
  lcd.setCursor(0, 1);
  String person = chore.person;
  // Handle empty or null person field
  if (person == "null" || person == "" || person.length() == 0) {
    person = "Unknown";
  }
  // Truncate if too long
  if (person.length() > 8) {
    person = person.substring(0, 7) + "~";
  }
  lcd.print(person);
  
  // Display due date status
  lcd.setCursor(10, 1);
  
  // Check if the chore is overdue (not completed and past due date)
  if (!chore.completed && isChoreOverdue(chore)) {
    // For overdue chores, show "OVRDUE"
    lcd.print("OVRDUE");
  } else if (!chore.completed) {
    // For incomplete (but not overdue) chores, show "!TODAY" instead of days until reset
    lcd.print("!TODAY");
  } else if (getDaysUntilDue(chore) == 0 && chore.frequency == "daily") {
    // For completed daily tasks due tomorrow
    lcd.print("tmrw");
  } else {
    // For other completed tasks, show days until due
    int daysUntilDue = getDaysUntilDue(chore);
    lcd.print("in");
    lcd.print(daysUntilDue);
    lcd.print("d");
  }
}

// Handle automatic scrolling for long chore names
void handleAutoScrolling() {
  // Do nothing if no chores
  if (chores.empty()) {
    return;
  }

  // Skip auto-scrolling if in confirmation dialog
  if (inConfirmationDialog) {
    return;  // Don't scroll while confirmation dialog is active
  }
  
  // Get current chore
  Chore& chore = chores[currentChoreIndex];
  
  // Only scroll if name is longer than available space (11 chars - with 4 chars for checkbox)
  if (chore.name.length() <= 11) {
    scrollPosition = 0;  // Reset position for short names
    pauseScrolling = false;
    return;
  }
  
  // Check if we're in pause state
  if (pauseScrolling) {
    if (millis() - pauseStartTime > SCROLL_PAUSE_DURATION) {
      // Pause time is over, resume scrolling
      pauseScrolling = false;
      lastScrollTime = millis();  // Reset timer for normal scrolling
    }
    return;  // Skip scrolling while in pause state
  }
  
  // Auto scroll based on timer
  if (millis() - lastScrollTime > scrollDelay) {
    scrollPosition++;
    
    // Reset scroll position when we reach the end (with a small offset to show the end of text)
    if (scrollPosition > chore.name.length() - 8) {
      scrollPosition = 0;
      
      // Start a pause at the beginning
      pauseScrolling = true;
      pauseStartTime = millis();
    }
    
    lastScrollTime = millis();
    updateLCD();
  }
}
// Display WiFi status and IP address
void displayNetworkStatus() {
  Serial.println("Displaying network status on LCD");
  
  // Show WiFi status
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (WiFi.status() == WL_CONNECTED) {
    String ssidName = WiFi.SSID();
    
    // Truncate SSID if too long
    if (ssidName.length() > 16) {
      ssidName = ssidName.substring(0, 13) + "...";
    }
    
    lcd.print(ssidName);
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    
    // Log to serial
    Serial.print("Connected to: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    lcd.print("WiFi: DISCONNECTED");
    lcd.setCursor(0, 1);
    lcd.print("Check settings");
    
    Serial.println("WiFi is not connected");
  }
  
  // Wait for button release
  while (digitalRead(JOYSTICK_SW_PIN) == LOW) {
    delay(10);
  }
  
  // Add a short delay after showing the info
  delay(2000);
  
  // Return to normal display
  updateLCD();
}

// Update LED Traffic Light Module
void updateLEDStatus() {
  bool allCompleted = true;
  bool anyOverdue = false;
  
  // Check all chores status
  for (const Chore& chore : chores) {
    if (!chore.completed) {
      allCompleted = false;
      
      // Check if any chore is overdue
      if (isChoreOverdue(chore)) {
        anyOverdue = true;
      }
    }
  }
  
  // Update LED states based on chore statuses
  if (WiFi.status() != WL_CONNECTED) {
    // WiFi not connected - Red LED takes priority
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
  } else if (chores.empty() || allCompleted) {
    // All chores completed or no chores - Green LED
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
  } else if (anyOverdue) {
    // At least one chore is overdue - Red LED
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
  } else {
    // Uncompleted chores but none overdue - Yellow LED
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
  }
}

// Display confirmation dialog on LCD
void displayConfirmationDialog() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mark Complete?");
  lcd.setCursor(0, 1);
  
  // Display Y/N options with the current selection highlighted
  if (confirmationSelection) {
    lcd.print("[Y]  N");  // Y is selected
  } else {
    lcd.print(" Y  [N]");  // N is selected
  }
}

// Manage joystick inputs
void handleJoystick() {
  // Read joystick values
  int joyX = analogRead(JOYSTICK_X_PIN);
  int joyY = analogRead(JOYSTICK_Y_PIN);
  bool buttonState = digitalRead(JOYSTICK_SW_PIN) == LOW;
  
  // Joystick thresholds
  const int JOYSTICK_CENTER_LOW = 1000;
  const int JOYSTICK_CENTER_HIGH = 3000;
  
  // Handle confirmation dialog if active
  if (inConfirmationDialog) {
    // Handle horizontal movement for Y/N selection
    static unsigned long lastConfirmNavTime = 0;
    const int CONFIRM_NAV_DELAY = 300;
    
    if (millis() - lastConfirmNavTime > CONFIRM_NAV_DELAY) {
      if (joyX < JOYSTICK_CENTER_LOW) {  // Left movement - select Y
        confirmationSelection = true;
        displayConfirmationDialog();
        lastConfirmNavTime = millis();
      } 
      else if (joyX > JOYSTICK_CENTER_HIGH) {  // Right movement - select N
        confirmationSelection = false;
        displayConfirmationDialog();
        lastConfirmNavTime = millis();
      }
    }
    
    // Handle button press in confirmation dialog
    if (buttonState) {
      if (buttonPressStartTime == 0) {
        buttonPressStartTime = millis();
      }
    } 
    else if (buttonPressStartTime > 0) { // Button released
      // If it was a short press
      if (millis() - buttonPressStartTime < LONG_PRESS_DURATION) {
        // Process the selection
        if (confirmationSelection && !chores.empty()) {
          // User selected Yes - mark the chore as complete
          chores[currentChoreIndex].completed = true;
          chores[currentChoreIndex].lastCompleted = now();
          calculateNextDueDate(chores[currentChoreIndex]);
          
          // Award points only if not already awarded for this cycle
          if (!chores[currentChoreIndex].pointsAwarded) {
            String personName = chores[currentChoreIndex].person;
            int pointsToAdd = 0;
            
            // Calculate points based on frequency
            if (chores[currentChoreIndex].frequency == "daily") {
              pointsToAdd = DAILY_POINTS;
            } else if (chores[currentChoreIndex].frequency == "weekly") {
              pointsToAdd = WEEKLY_POINTS;
            } else if (chores[currentChoreIndex].frequency == "monthly") {
              pointsToAdd = MONTHLY_POINTS;
            }
            
            // Find the user and add points
            for (User& user : users) {
              if (user.username == personName) {
                user.points += pointsToAdd;
                saveUsers();  // Save updated points
                break;
              }
            }
            
            // Mark that points have been awarded for this cycle
            chores[currentChoreIndex].pointsAwarded = true;
          }
          
          saveChores();
          updateLEDStatus();
        }
        
        // Exit confirmation dialog and return to normal display
        inConfirmationDialog = false;
        scrollPosition = 0;  // Reset scroll position
        updateLCD();
        lastJoystickDebounce = millis();
      }
      buttonPressStartTime = 0;
    }
    
    return; // Skip the rest of the joystick handling when in confirmation dialog
  }
  
  // Handle button state for both short and long press
  if (buttonState) {
    // Start tracking press time
    if (buttonPressStartTime == 0) {
      buttonPressStartTime = millis();
    }
    
    // Check for long press
    if (millis() - buttonPressStartTime > LONG_PRESS_DURATION && !joystickPressed) {
      joystickPressed = true; // Prevent multiple triggers
      displayNetworkStatus();
    }
  } 
  else { // Button released
    // If it was a short press and not recently debounced
    if (buttonPressStartTime > 0 && 
        millis() - buttonPressStartTime < LONG_PRESS_DURATION &&
        !joystickPressed && 
        millis() - lastJoystickDebounce > debounceDelay) {
      
      // Check if there are chores
      if (!chores.empty()) {
        // If chore is already completed, toggle directly to incomplete without confirmation
        if (chores[currentChoreIndex].completed) {
          // If points were awarded for this chore, subtract them when uncompleted
          if (chores[currentChoreIndex].pointsAwarded) {
            String personName = chores[currentChoreIndex].person;
            int pointsToSubtract = 0;
            
            // Calculate points based on frequency
            if (chores[currentChoreIndex].frequency == "daily") {
              pointsToSubtract = DAILY_POINTS;
            } else if (chores[currentChoreIndex].frequency == "weekly") {
              pointsToSubtract = WEEKLY_POINTS;
            } else if (chores[currentChoreIndex].frequency == "monthly") {
              pointsToSubtract = MONTHLY_POINTS;
            }
            
            // Find the user and subtract points
            for (User& user : users) {
              if (user.username == personName) {
                user.points -= pointsToSubtract;
                if (user.points < 0) user.points = 0; // Prevent negative points
                saveUsers();  // Save updated points
                break;
              }
            }
            
            // Reset the points awarded flag so it can be rewarded
            chores[currentChoreIndex].pointsAwarded = false;
          }
          
          // Mark as incomplete immediately
          chores[currentChoreIndex].completed = false;
          saveChores();
          scrollPosition = 0;
          updateLCD();
          updateLEDStatus();
        } else {
          // Only show confirmation dialog when marking a chore as complete
          inConfirmationDialog = true;
          confirmationSelection = true; // Default to Yes
          displayConfirmationDialog();
        }
      }
      
      lastJoystickDebounce = millis();
    }
    
    // Reset button tracking
    buttonPressStartTime = 0;
    joystickPressed = false;
  }
  
  // Handle joystick navigation - only process if not empty and not in confirmation
  if (!chores.empty() && !inConfirmationDialog) {
    static unsigned long lastNavigationTime = 0;
    const int NAVIGATION_DELAY = 300;
    
    if (millis() - lastNavigationTime > NAVIGATION_DELAY) {
      if (joyY < JOYSTICK_CENTER_LOW) {  // Up movement
        currentChoreIndex = (currentChoreIndex + 1) % chores.size();
        scrollPosition = 0;  // Reset scroll position when changing chores
        updateLCD();
        lastNavigationTime = millis();
      } 
      else if (joyY > JOYSTICK_CENTER_HIGH) {  // Down movement
        currentChoreIndex = (currentChoreIndex - 1 + chores.size()) % chores.size();
        scrollPosition = 0;  // Reset scroll position when changing chores
        updateLCD();
        lastNavigationTime = millis();
      }
    }
  }
}

// Handle reset points request
void handleResetPoints() {
  // Reset all user points to zero
  for (User& user : users) {
    user.points = 0;
  }
  
  // Save the updated users
  saveUsers();
  
  // Return success response
  server.send(200, "application/json", "{\"success\":true}");
}

// Web server handlers for chore operations
void handleRootPage() {
  // Build user selection dropdown options
  String userOptions = "";
  for (const User& user : users) {
    userOptions += "<option value=\"" + user.username + "\">" + user.username + "</option>";
  }

  // Build chore list HTML directly on the server
  String choreListHtml = "";
  for (int i = 0; i < chores.size(); i++) {
    Chore& chore = chores[i];
    String choreClass = chore.completed ? "chore-item completed" : "chore-item";
    if (isChoreOverdue(chore)) {
      choreClass += " overdue";
    }
    
    choreListHtml += "<div class=\"" + choreClass + "\">";
    choreListHtml += "<input type=\"checkbox\" " + String(chore.completed ? "checked" : "") + " onclick=\"toggleChore(" + String(i) + ")\">";
    choreListHtml += " " + chore.name + " (" + chore.person + ") - " + chore.frequency;
    
    // Days display
    if (chore.completed) {
      if (chore.frequency == "daily" && getDaysUntilDue(chore) == 0) {
        choreListHtml += " - Next due: tomorrow";
      } else {
        choreListHtml += " - Next due: " + String(getDaysUntilDue(chore)) + " days";
      }
    } else if (isChoreOverdue(chore)) {
      choreListHtml += " - Overdue!";
    } else if (getDaysUntilReset(chore) == 1) {
      choreListHtml += " - Due Today";
    } else {
      choreListHtml += " - Due in " + String(getDaysUntilReset(chore)) + " days";
    }
    
    // Actions
    choreListHtml += "<div class=\"actions\">";
    choreListHtml += "<button onclick=\"editChore(" + String(i) + ")\">Edit</button> ";
    choreListHtml += "<button onclick=\"deleteChore(" + String(i) + ")\">Delete</button>";
    choreListHtml += "</div>";
    choreListHtml += "</div>";
  }
  
  if (choreListHtml == "") {
    choreListHtml = "<p>No chores found. Add some using the button below.</p>";
  }
  
  // Build user list HTML directly on the server
  String userListHtml = "";
  for (int i = 0; i < users.size(); i++) {
    User& user = users[i];
    userListHtml += "<div class=\"user-item\">";
    userListHtml += user.username + " - " + String(user.points) + " points";
    userListHtml += "<div class=\"actions\">";
    userListHtml += "<button onclick=\"editUser(" + String(i) + ")\">Edit</button> ";
    userListHtml += "<button onclick=\"deleteUser(" + String(i) + ")\">Delete</button>";
    userListHtml += "</div>";
    userListHtml += "</div>";
  }
  
  if (userListHtml == "") {
    userListHtml = "<p>No users found. Add some using the button below.</p>";
  }
  
  // Sort users by points for leaderboard (descending order)
  std::vector<User> sortedUsers = users;
  std::sort(sortedUsers.begin(), sortedUsers.end(), [](const User& a, const User& b) {
    return a.points > b.points;  // Sort by points in descending order
  });
  
  // Build leaderboard HTML
  String leaderboardHtml = "";
  for (const User& user : sortedUsers) {
    leaderboardHtml += "<div class=\"leaderboard-item\">";
    leaderboardHtml += user.username + " - " + String(user.points) + " points";
    leaderboardHtml += "</div>";
  }
  
  if (leaderboardHtml == "") {
    leaderboardHtml = "<p>No users have earned points yet.</p>";
  }

  String html = "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Chore Taskboard</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 20px; }"
    ".chore-item { padding: 8px; border-bottom: 1px solid #ddd; }"
    ".completed { text-decoration: line-through; color: #888; }"
    ".overdue { color: red; font-weight: bold; }"
    ".form-group { margin-bottom: 10px; }"
    "input, button, select { padding: 5px; }"
    ".add-form { border: 1px solid #ddd; padding: 10px; margin: 10px 0; }"
    ".hidden { display: none; }"
    ".visible { display: block; }"
    ".actions { margin-top: 5px; }"
    ".section { margin-bottom: 20px; }"
    "h2 { border-bottom: 1px solid #ddd; padding-bottom: 5px; }"
    ".user-item { padding: 8px; border-bottom: 1px solid #ddd; }"
    ".leaderboard-item { padding: 8px; border-bottom: 1px solid #ddd; }"
    ".reset-button { margin-top: 10px; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>Chore Taskboard</h1>"
    
    "<div class=\"section\">"
    "<h2>Chores</h2>"
    "<div id=\"chore-list\">"
    + choreListHtml +
    "</div>"
    "<button onclick=\"document.getElementById('add-form').classList.remove('hidden');document.getElementById('add-form').classList.add('visible');\">Add New Chore</button>"
    "<div id=\"add-form\" class=\"add-form hidden\">"
    "<div class=\"form-group\">"
    "<label>Chore Name:</label>"
    "<input type=\"text\" id=\"chore-name\">"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Person:</label>"
    "<select id=\"chore-person\">"
    + userOptions +
    "</select>"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Frequency:</label>"
    "<select id=\"chore-frequency\">"
    "<option value=\"daily\">Daily</option>"
    "<option value=\"weekly\">Weekly</option>"
    "<option value=\"monthly\">Monthly</option>"
    "</select>"
    "</div>"
    "<button onclick=\"addChore();\">Save</button>"
    "<button onclick=\"document.getElementById('add-form').classList.add('hidden');document.getElementById('add-form').classList.remove('visible');\">Cancel</button>"
    "</div>"
    "<div id=\"edit-form\" class=\"add-form hidden\">"
    "<input type=\"hidden\" id=\"edit-index\">"
    "<div class=\"form-group\">"
    "<label>Chore Name:</label>"
    "<input type=\"text\" id=\"edit-name\">"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Person:</label>"
    "<select id=\"edit-person\">"
    + userOptions +
    "</select>"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Frequency:</label>"
    "<select id=\"edit-frequency\">"
    "<option value=\"daily\">Daily</option>"
    "<option value=\"weekly\">Weekly</option>"
    "<option value=\"monthly\">Monthly</option>"
    "</select>"
    "</div>"
    "<button onclick=\"updateChore();\">Update</button>"
    "<button onclick=\"document.getElementById('edit-form').classList.add('hidden');document.getElementById('edit-form').classList.remove('visible');\">Cancel</button>"
    "</div>"
    "</div>"
    
    "<div class=\"section\">"
    "<h2>Leaderboard</h2>"
    "<div id=\"leaderboard\">"
    + leaderboardHtml +
    "</div>"
    "<div class=\"reset-button\">"
    "<button onclick=\"resetAllPoints();\">Reset All Points</button>"
    "</div>"
    "</div>"
    
    "<div class=\"section\">"
    "<h2>Users</h2>"
    "<div id=\"user-list\">"
    + userListHtml +
    "</div>"
    "<button onclick=\"document.getElementById('add-user-form').classList.remove('hidden');document.getElementById('add-user-form').classList.add('visible');\">Add New User</button>"
    "<div id=\"add-user-form\" class=\"add-form hidden\">"
    "<div class=\"form-group\">"
    "<label>Username:</label>"
    "<input type=\"text\" id=\"user-username\">"
    "</div>"
    "<button onclick=\"addUser();\">Save</button>"
    "<button onclick=\"document.getElementById('add-user-form').classList.add('hidden');document.getElementById('add-user-form').classList.remove('visible');\">Cancel</button>"
    "</div>"
    "<div id=\"edit-user-form\" class=\"add-form hidden\">"
    "<input type=\"hidden\" id=\"edit-user-index\">"
    "<div class=\"form-group\">"
    "<label>Username:</label>"
    "<input type=\"text\" id=\"edit-user-username\">"
    "</div>"
    "<button onclick=\"updateUser();\">Update</button>"
    "<button onclick=\"document.getElementById('edit-user-form').classList.add('hidden');document.getElementById('edit-user-form').classList.remove('visible');\">Cancel</button>"
    "</div>"
    "</div>"
    
    "<script>"
    "function toggleChore(index) {"
    "  fetch('/toggle?index=' + index)"
    "    .then(function(response) { return response.json(); })"
    "    .then(function(data) { window.location.reload(); });"
    "}"
    
    "function addChore() {"
    "  var name = document.getElementById('chore-name').value;"
    "  var person = document.getElementById('chore-person').value;"
    "  var frequency = document.getElementById('chore-frequency').value;"
    "  if (!name) {"
    "    alert('Chore name is required');"
    "    return;"
    "  }"
    "  fetch('/add?name=' + encodeURIComponent(name) + '&person=' + encodeURIComponent(person) + '&frequency=' + frequency)"
    "    .then(function(response) { return response.json(); })"
    "    .then(function(data) { window.location.reload(); });"
    "}"
    
    "function addUser() {"
    "  var username = document.getElementById('user-username').value;"
    "  if (!username) {"
    "    alert('Username is required');"
    "    return;"
    "  }"
    "  fetch('/addUser?username=' + encodeURIComponent(username))"
    "    .then(function(response) { return response.json(); })"
    "    .then(function(data) { window.location.reload(); });"
    "}"
    
    "function editChore(index) {"
    "  fetch('/edit?index=' + index)"
    "    .then(function(response) { return response.json(); })"
    "    .then(function(chore) {"
    "      document.getElementById('edit-index').value = index;"
    "      document.getElementById('edit-name').value = chore.name;"
    "      var personSelect = document.getElementById('edit-person');"
    "      for (var i = 0; i < personSelect.options.length; i++) {"
    "        if (personSelect.options[i].value === chore.person) {"
    "          personSelect.selectedIndex = i;"
    "          break;"
    "        }"
    "      }"
    "      document.getElementById('edit-frequency').value = chore.frequency;"
    "      document.getElementById('edit-form').classList.remove('hidden');"
    "      document.getElementById('edit-form').classList.add('visible');"
    "    });"
    "}"
    
    "function updateChore() {"
    "  var index = document.getElementById('edit-index').value;"
    "  var name = document.getElementById('edit-name').value;"
    "  var person = document.getElementById('edit-person').value;"
    "  var frequency = document.getElementById('edit-frequency').value;"
    "  if (!name) {"
    "    alert('Chore name is required');"
    "    return;"
    "  }"
    "  fetch('/update?index=' + index + '&name=' + encodeURIComponent(name) + '&person=' + encodeURIComponent(person) + '&frequency=' + frequency)"
    "    .then(function(response) { return response.json(); })"
    "    .then(function(data) { window.location.reload(); });"
    "}"
    
    "function deleteChore(index) {"
    "  if (confirm('Are you sure you want to delete this chore?')) {"
    "    fetch('/delete?index=' + index)"
    "      .then(function(response) { return response.json(); })"
    "      .then(function(data) { window.location.reload(); });"
    "  }"
    "}"
    
    "function deleteUser(index) {"
    "  if (confirm('Are you sure you want to delete this user?')) {"
    "    fetch('/deleteUser?index=' + index)"
    "      .then(function(response) { return response.json(); })"
    "      .then(function(data) { window.location.reload(); });"
    "  }"
    "}"
    
    "function resetAllPoints() {"
    "  if (confirm('Are you sure you want to reset all user points to zero? This cannot be undone.')) {"
    "    if (confirm('FINAL WARNING: All point progress will be lost. Continue?')) {"
    "      fetch('/resetPoints')"
    "        .then(function(response) { return response.json(); })"
    "        .then(function(data) { window.location.reload(); });"
    "    }"
    "  }"
    "}"
    
    "function editUser(index) {"
    "  fetch('/editUser?index=' + index)"
    "    .then(function(response) { return response.json(); })"
    "    .then(function(user) {"
    "      document.getElementById('edit-user-index').value = index;"
    "      document.getElementById('edit-user-username').value = user.username;"
    "      document.getElementById('edit-user-form').classList.remove('hidden');"
    "      document.getElementById('edit-user-form').classList.add('visible');"
    "    });"
    "}"
    
    "function updateUser() {"
    "  var index = document.getElementById('edit-user-index').value;"
    "  var username = document.getElementById('edit-user-username').value;"
    "  if (!username) {"
    "    alert('Username is required');"
    "    return;"
    "  }"
    "  fetch('/updateUser?index=' + index + '&username=' + encodeURIComponent(username))"
    "    .then(function(response) { return response.json(); })"
    "    .then(function(data) { window.location.reload(); });"
    "}"
    "</script>"
    "</body>"
    "</html>";
  
  server.send(200, "text/html", html);
}

// Handle toggle chore request
void handleToggleChore() {
  // Check if this is a list request
  if (server.hasArg("action") && server.arg("action") == "list") {
    StaticJsonDocument<4096> doc;
    JsonArray array = doc.to<JsonArray>();
    
    for (int i = 0; i < chores.size(); i++) {
      Chore& chore = chores[i];
      JsonObject choreObj = array.add<JsonObject>();
      choreObj["name"] = chore.name;
      choreObj["person"] = chore.person;
      choreObj["frequency"] = chore.frequency;
      choreObj["completed"] = chore.completed;
      choreObj["daysUntilDue"] = getDaysUntilDue(chore);
      choreObj["daysUntilReset"] = getDaysUntilReset(chore);
      choreObj["overdue"] = isChoreOverdue(chore);  // Add overdue status to JSON
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
    return;
  }
  
  // Check if this is a toggle request
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    
    if (index >= 0 && index < chores.size()) {
      Chore& chore = chores[index];
      
      // Toggle chore completion status
      if (chore.completed) {
        // If we're uncompleting a chore and points were awarded, subtract them
        if (chore.pointsAwarded) {
          String personName = chore.person;
          int pointsToSubtract = 0;
          
          // Calculate points based on frequency
          if (chore.frequency == "daily") {
            pointsToSubtract = DAILY_POINTS;
          } else if (chore.frequency == "weekly") {
            pointsToSubtract = WEEKLY_POINTS;
          } else if (chore.frequency == "monthly") {
            pointsToSubtract = MONTHLY_POINTS;
          }
          
          // Find the user and subtract points
          for (User& user : users) {
            if (user.username == personName) {
              user.points -= pointsToSubtract;
              if (user.points < 0) user.points = 0; // Prevent negative points
              saveUsers();  // Save updated points
              break;
            }
          }
          
          // Reset the points awarded flag
          chore.pointsAwarded = false;
        }
        
        chore.completed = false;
      } else {
        chore.completed = true;
        
        // Award points if not already awarded
        if (!chore.pointsAwarded) {
          String personName = chore.person;
          int pointsToAdd = 0;
          
          // Calculate points based on frequency
          if (chore.frequency == "daily") {
            pointsToAdd = DAILY_POINTS;
          } else if (chore.frequency == "weekly") {
            pointsToAdd = WEEKLY_POINTS;
          } else if (chore.frequency == "monthly") {
            pointsToAdd = MONTHLY_POINTS;
          }
          
          // Find the user and add points
          for (User& user : users) {
            if (user.username == personName) {
              user.points += pointsToAdd;
              saveUsers();  // Save updated points
              break;
            }
          }
          
          // Mark that points have been awarded for this cycle
          chore.pointsAwarded = true;
        }
        
        chore.lastCompleted = now();
        calculateNextDueDate(chore);
      }
      
      saveChores();
      scrollPosition = 0;  // Reset scroll position
      updateLCD();
      
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing index\"}");
  }
}

// Handle add chore request
void handleAddChore() {
  if (server.hasArg("name") && server.hasArg("person") && server.hasArg("frequency")) {
    Chore chore;
    chore.name = server.arg("name");
    chore.person = server.arg("person");
    chore.frequency = server.arg("frequency");
    chore.completed = false;
    chore.lastCompleted = 0;
    chore.pointsAwarded = false; // Initialize as not awarded
    
    calculateNextDueDate(chore);
    chores.push_back(chore);
    saveChores();
    updateLCD();
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
  }
}

// Handle delete chore request
void handleDeleteChore() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    
    if (index >= 0 && index < chores.size()) {
      chores.erase(chores.begin() + index);
      
      if (currentChoreIndex >= chores.size()) {
        currentChoreIndex = chores.size() - 1;
        if (currentChoreIndex < 0) currentChoreIndex = 0;
      }
      
      saveChores();
      updateLCD();
      
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing index\"}");
  }
}

// Handle get chore request for editing
void handleGetChore() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    
    if (index >= 0 && index < chores.size()) {
      Chore& chore = chores[index];
      
      StaticJsonDocument<512> doc;
      doc["name"] = chore.name;
      doc["person"] = chore.person;
      doc["frequency"] = chore.frequency;
      
      String response;
      serializeJson(doc, response);
      server.send(200, "application/json", response);
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing index\"}");
  }
}

// Handle update chore request
void handleUpdateChore() {
  if (server.hasArg("index") && server.hasArg("name") && server.hasArg("person") && server.hasArg("frequency")) {
    int index = server.arg("index").toInt();
    
    if (index >= 0 && index < chores.size()) {
      Chore& chore = chores[index];
      chore.name = server.arg("name");
      chore.person = server.arg("person");
      chore.frequency = server.arg("frequency");
      
      // Recalculate next due date if completed
      if (chore.completed) {
        calculateNextDueDate(chore);
      }
      
      saveChores();
      updateLCD();
      
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
  }
}

// Handle list users request
void handleListUsers() {
  StaticJsonDocument<4096> doc;
  JsonArray array = doc.to<JsonArray>();
  
  for (User& user : users) {
    JsonObject userObj = array.add<JsonObject>();
    userObj["username"] = user.username;
    userObj["points"] = user.points;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handle add user request
void handleAddUser() {
  if (server.hasArg("username")) {
    User user;
    user.username = server.arg("username");
    user.points = 0;  // Initialize points to 0 for new users
    
    users.push_back(user);
    saveUsers();
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing username\"}");
  }
}

// Handle delete user request
void handleDeleteUser() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    
    if (index >= 0 && index < users.size()) {
      users.erase(users.begin() + index);
      saveUsers();
      
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing index\"}");
  }
}

// Handle get user request for editing
void handleGetUser() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    
    if (index >= 0 && index < users.size()) {
      User& user = users[index];
      
      StaticJsonDocument<512> doc;
      doc["username"] = user.username;
      doc["points"] = user.points;
      
      String response;
      serializeJson(doc, response);
      server.send(200, "application/json", response);
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing index\"}");
  }
}

// Handle update user request
void handleUpdateUser() {
  if (server.hasArg("index") && server.hasArg("username")) {
    int index = server.arg("index").toInt();
    
    if (index >= 0 && index < users.size()) {
      User& user = users[index];
      user.username = server.arg("username");
      
      // Points remain unchanged
      saveUsers();
      
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
  }
}