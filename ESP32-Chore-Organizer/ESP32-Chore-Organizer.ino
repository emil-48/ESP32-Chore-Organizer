/*************
Chore Organizer Dashboard and Web Interface
Run on Adafruit Feather ESP32 V2
https://github.com/emil-48/ESP32-Chore-Organizer
*************/
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// WiFi credentials - replace with your network details
const char* ssid = "WIFI";
const char* password = "PASSWORD";

// Pin definitions
//SDA_PIN = SDA;
//SCL_PIN = SCL;
const int JOYSTICK_X_PIN = A2;
const int JOYSTICK_Y_PIN = A3;
const int JOYSTICK_SW_PIN = 15;

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16 columns, 2 rows

// Web server
WebServer server(80);

// NTP for accurate time
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
};

// Global variables
std::vector<Chore> chores;
int currentChoreIndex = 0;
bool joystickPressed = false;
unsigned long lastJoystickDebounce = 0;
const int debounceDelay = 300;  // ms

// Variables for name scrolling
int scrollPosition = 0;
unsigned long lastScrollTime = 0;
const int scrollDelay = 150;  // ms

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C for LCD
  //  Wire.begin(SDA_PIN, SCL_PIN);
  
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
  
  if (WiFi.status() != WL_CONNECTED) {
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
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
  
  // Load chores from file
  loadChores();
  
  // Initial LCD update
  updateLCD();
}

void loop() {
  // Handle web server client requests
  server.handleClient();
  
  // Read and process joystick input
  handleJoystick();

  // Handle name scrolling if needed
  handleNameScrolling();

  // Small delay to prevent CPU hogging
  delay(10);
  
  // Update all chores status periodically (every minute)
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime > 60000) {
    updateChoreStatus();
    lastCheckTime = millis();
  }
  
  // Update NTP time occasionally
  static unsigned long lastNtpUpdate = 0;
  if (millis() - lastNtpUpdate > 3600000) {  // Every hour
    timeClient.update();
    setTime(timeClient.getEpochTime());
    lastNtpUpdate = millis();
  }
}

// Handle name scrolling based on joystick X position
void handleNameScrolling() {
  if (chores.empty()) {
    return;
  }
  
  // Get current chore
  Chore& chore = chores[currentChoreIndex];
  
  // Only scroll if name is longer than available space (11 chars - 4 for checkbox)
  if (chore.name.length() <= 11) {
    return;
  }
  
  // Read X position and determine if scrolling is needed
  int joyX = analogRead(JOYSTICK_X_PIN);
  
  // Define joystick thresholds based on center position of ~1950
  const int JOYSTICK_CENTER_LOW = 1700;
  const int JOYSTICK_CENTER_HIGH = 2200;
  
  if (millis() - lastScrollTime > scrollDelay) {
    // Scroll left (right on joystick)
    if (joyX > JOYSTICK_CENTER_HIGH && scrollPosition < chore.name.length() - 11) {
      scrollPosition++;
      lastScrollTime = millis();
      updateLCD();
    }
    // Scroll right (left on joystick)
    else if (joyX < JOYSTICK_CENTER_LOW && scrollPosition > 0) {
      scrollPosition--;
      lastScrollTime = millis();
      updateLCD();
    }
  }
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
          
          // Calculate next due date
          calculateNextDueDate(chore);
          
          chores.push_back(chore);
        }
      }
      file.close();
    }
  }
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
  }
  
  File file = SPIFFS.open("/chores.json", "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
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

// Update all chores status
void updateChoreStatus() {
  time_t current = now();
  bool changed = false;
  
  for (Chore& chore : chores) {
    if (chore.completed && current >= chore.nextDue) {
      // Reset chore if it's past due
      chore.completed = false;
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
  
  // Handle scrolling for long names
  String displayName;
  if (chore.name.length() > 11) {
    // If name is too long, show a portion based on scroll position
    displayName = chore.name.substring(scrollPosition, scrollPosition + 11);
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
  if (person.length() > 10) {
    person = person.substring(0, 9) + "~";
  }
  lcd.print(person);
  
  // Days until due - show "tomorrow" for completed daily chores with 0 days
  int daysUntil = getDaysUntilDue(chore);
  
  if (daysUntil == 0 && !chore.completed) {
    lcd.setCursor(11, 1);
    lcd.print("DUE!");
  } else if (daysUntil == 0 && chore.completed && chore.frequency == "daily") {
    // For completed daily tasks, show "tomorrow" instead of "0d"
    lcd.setCursor(11, 1);
    lcd.print("tmrw");
  } else {
    lcd.setCursor(11, 1);
    lcd.print(daysUntil);
    lcd.print("d");
  }
}

// Handle joystick input
void handleJoystick() {
  // Read joystick values
  int joyY = analogRead(JOYSTICK_Y_PIN);
  bool buttonState = digitalRead(JOYSTICK_SW_PIN) == LOW;
  
  // Debounce joystick button
  if (buttonState && !joystickPressed && millis() - lastJoystickDebounce > debounceDelay) {
    joystickPressed = true;
    lastJoystickDebounce = millis();
    
    Serial.println("Joystick button pressed");
    
    // Toggle completion of current chore
    if (!chores.empty()) {
      chores[currentChoreIndex].completed = !chores[currentChoreIndex].completed;
      
      // Update last completed time if marked as completed
      if (chores[currentChoreIndex].completed) {
        chores[currentChoreIndex].lastCompleted = now();
        calculateNextDueDate(chores[currentChoreIndex]);
      }
      
      saveChores();
      // Reset scroll position when toggling a chore
      scrollPosition = 0;
      updateLCD();
    }
  } else if (!buttonState) {
    joystickPressed = false;
  }
  
  // Simplified joystick navigation with clear thresholds
  if (!chores.empty()) {
    static unsigned long lastNavigationDebounce = 0;
    const int navigationDebounceDelay = 300;  // Reduced for better responsiveness
    
    // Define joystick thresholds based on center position of ~1950
    const int JOYSTICK_CENTER_LOW = 1000;
    const int JOYSTICK_CENTER_HIGH = 3000;
    
    static bool inCenterPosition = true;
    
    // Check if joystick is in center position
    bool isCenter = (joyY >= JOYSTICK_CENTER_LOW && joyY <= JOYSTICK_CENTER_HIGH);
    
    // If we're in center position, allow for next movement
    if (isCenter) {
      inCenterPosition = true;
    } 
    // If we've returned to center and now moved, and debounce time passed
    else if (inCenterPosition && millis() - lastNavigationDebounce > navigationDebounceDelay) {
      lastNavigationDebounce = millis();
      inCenterPosition = false;
      
      if (joyY < JOYSTICK_CENTER_LOW) {  // Up movement
        currentChoreIndex = (currentChoreIndex + 1) % chores.size();
        // Reset scroll position when changing chores
        scrollPosition = 0;
        updateLCD();
      } else if (joyY > JOYSTICK_CENTER_HIGH) {  // Down movement
        currentChoreIndex = (currentChoreIndex - 1 + chores.size()) % chores.size();
        // Reset scroll position when changing chores
        scrollPosition = 0;
        updateLCD();
      }
    }
  }
}

// Web server handlers for chore operations
void handleRootPage() {
  String html = "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Chore Taskboard</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 20px; }"
    ".chore-item { padding: 8px; border-bottom: 1px solid #ddd; }"
    ".completed { text-decoration: line-through; color: #888; }"
    ".form-group { margin-bottom: 10px; }"
    "input, button, select { padding: 5px; }"
    ".add-form { border: 1px solid #ddd; padding: 10px; margin: 10px 0; display: none; }"
    ".actions { margin-top: 5px; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>Chore Taskboard</h1>"
    "<div id=\"chore-list\">"
    "<!-- Chores will be populated here -->"
    "</div>"
    "<button onclick=\"toggleAddForm()\">Add New Chore</button>"
    "<div id=\"add-form\" class=\"add-form\">"
    "<div class=\"form-group\">"
    "<label>Chore Name:</label>"
    "<input type=\"text\" id=\"chore-name\">"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Person:</label>"
    "<input type=\"text\" id=\"chore-person\">"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Frequency:</label>"
    "<select id=\"chore-frequency\">"
    "<option value=\"daily\">Daily</option>"
    "<option value=\"weekly\">Weekly</option>"
    "<option value=\"monthly\">Monthly</option>"
    "</select>"
    "</div>"
    "<button onclick=\"addChore()\">Save</button>"
    "<button onclick=\"toggleAddForm()\">Cancel</button>"
    "</div>"
    "<div id=\"edit-form\" class=\"add-form\">"
    "<input type=\"hidden\" id=\"edit-index\">"
    "<div class=\"form-group\">"
    "<label>Chore Name:</label>"
    "<input type=\"text\" id=\"edit-name\">"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Person:</label>"
    "<input type=\"text\" id=\"edit-person\">"
    "</div>"
    "<div class=\"form-group\">"
    "<label>Frequency:</label>"
    "<select id=\"edit-frequency\">"
    "<option value=\"daily\">Daily</option>"
    "<option value=\"weekly\">Weekly</option>"
    "<option value=\"monthly\">Monthly</option>"
    "</select>"
    "</div>"
    "<button onclick=\"updateChore()\">Update</button>"
    "<button onclick=\"toggleEditForm()\">Cancel</button>"
    "</div>"
    "<script>"
    "window.onload = loadChores;"
    "function loadChores() {"
    "  fetch(\"/toggle?action=list\")"
    "  .then(response => response.json())"
    "  .then(data => {"
    "    const choreList = document.getElementById(\"chore-list\");"
    "    choreList.innerHTML = \"\";"
    "    data.forEach((chore, index) => {"
    "      const choreDiv = document.createElement(\"div\");"
    "      choreDiv.className = \"chore-item\" + (chore.completed ? \" completed\" : \"\");"
    "      const checkbox = document.createElement(\"input\");"
    "      checkbox.type = \"checkbox\";"
    "      checkbox.checked = chore.completed;"
    "      checkbox.onclick = function() { toggleChore(index); };"
    "      const nameSpan = document.createElement(\"span\");"
    "      nameSpan.textContent = \" \" + chore.name + \" (\" + chore.person + \") - \" + chore.frequency;"
    "      const daysSpan = document.createElement(\"span\");"
    "      if (chore.completed) {"
    "        if (chore.frequency === \"daily\" && chore.daysUntil === 0) {"
    "          daysSpan.textContent = \" - Due tomorrow\";"
    "        } else {"
    "          daysSpan.textContent = \" - Due in \" + chore.daysUntil + \" days\";"
    "        }"
    "      }"
    "      const actions = document.createElement(\"div\");"
    "      actions.className = \"actions\";"
    "      const editButton = document.createElement(\"button\");"
    "      editButton.textContent = \"Edit\";"
    "      editButton.onclick = function() { editChore(index); };"
    "      const deleteButton = document.createElement(\"button\");"
    "      deleteButton.textContent = \"Delete\";"
    "      deleteButton.onclick = function() { deleteChore(index); };"
    "      actions.appendChild(editButton);"
    "      actions.appendChild(document.createTextNode(\" \"));"
    "      actions.appendChild(deleteButton);"
    "      choreDiv.appendChild(checkbox);"
    "      choreDiv.appendChild(nameSpan);"
    "      choreDiv.appendChild(daysSpan);"
    "      choreDiv.appendChild(actions);"
    "      choreList.appendChild(choreDiv);"
    "    });"
    "  });"
    "}"
    "function toggleChore(index) {"
    "  fetch(\"/toggle?index=\" + index)"
    "  .then(response => response.json())"
    "  .then(data => {"
    "    loadChores();"
    "  });"
    "}"
    "function toggleAddForm() {"
    "  const form = document.getElementById(\"add-form\");"
    "  form.style.display = form.style.display === \"none\" || form.style.display === \"\" ? \"block\" : \"none\";"
    "}"
    "function toggleEditForm() {"
    "  const form = document.getElementById(\"edit-form\");"
    "  form.style.display = \"none\";"
    "}"
    "function addChore() {"
    "  const name = document.getElementById(\"chore-name\").value;"
    "  const person = document.getElementById(\"chore-person\").value;"
    "  const frequency = document.getElementById(\"chore-frequency\").value;"
    "  if (!name || !person) {"
    "    alert(\"Name and person are required\");"
    "    return;"
    "  }"
    "  fetch(\"/add?name=\" + encodeURIComponent(name) + \"&person=\" + encodeURIComponent(person) + \"&frequency=\" + frequency)"
    "  .then(response => response.json())"
    "  .then(data => {"
    "    document.getElementById(\"chore-name\").value = \"\";"
    "    document.getElementById(\"chore-person\").value = \"\";"
    "    toggleAddForm();"
    "    loadChores();"
    "  });"
    "}"
    "function editChore(index) {"
    "  fetch(\"/edit?index=\" + index)"
    "  .then(response => response.json())"
    "  .then(chore => {"
    "    document.getElementById(\"edit-index\").value = index;"
    "    document.getElementById(\"edit-name\").value = chore.name;"
    "    document.getElementById(\"edit-person\").value = chore.person;"
    "    document.getElementById(\"edit-frequency\").value = chore.frequency;"
    "    document.getElementById(\"edit-form\").style.display = \"block\";"
    "  });"
    "}"
    "function updateChore() {"
    "  const index = document.getElementById(\"edit-index\").value;"
    "  const name = document.getElementById(\"edit-name\").value;"
    "  const person = document.getElementById(\"edit-person\").value;"
    "  const frequency = document.getElementById(\"edit-frequency\").value;"
    "  if (!name || !person) {"
    "    alert(\"Name and person are required\");"
    "    return;"
    "  }"
    "  fetch(\"/update?index=\" + index + \"&name=\" + encodeURIComponent(name) + \"&person=\" + encodeURIComponent(person) + \"&frequency=\" + frequency)"
    "  .then(response => response.json())"
    "  .then(data => {"
    "    toggleEditForm();"
    "    loadChores();"
    "  });"
    "}"
    "function deleteChore(index) {"
    "  if (confirm(\"Are you sure you want to delete this chore?\")) {"
    "    fetch(\"/delete?index=\" + index)"
    "    .then(response => response.json())"
    "    .then(data => {"
    "      loadChores();"
    "    });"
    "  }"
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
      choreObj["daysUntil"] = getDaysUntilDue(chore);
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
      chore.completed = !chore.completed;
      
      if (chore.completed) {
        chore.lastCompleted = now();
        calculateNextDueDate(chore);
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

// Handle add chore request
void handleAddChore() {
  if (server.hasArg("name") && server.hasArg("person") && server.hasArg("frequency")) {
    Chore chore;
    chore.name = server.arg("name");
    chore.person = server.arg("person");
    chore.frequency = server.arg("frequency");
    chore.completed = false;
    chore.lastCompleted = 0;
    
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
      // Reset scroll position since chore name might have changed
      scrollPosition = 0;
      updateLCD();
      
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
  }
}
