/*  Description  
 *   This project runs the wire snipper tool <tool_id> that feeds wire to a certain
 *   length, strips one or both ends, and, optionally, solders the first stripped end.
*/

//------------------------------- libraries ----------------------------------

//#include <LiquidCrystal.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

//------------------------------- lcd ----------------------------------
//#define LCD_RS_PIN 12
//#define LCD_ENABLE_PIN 11
// Omitting data lines 0-3, using only 4-7
//#define LCD_DATA_LN_4 2
//#define LCD_DATA_LN_5 3
//#define LCD_DATA_LN_6 4
//#define LCD_DATA_LN_7 5

// Construct a LiquidCrystal object called lcd, using the pins assigned.
//LiquidCrystal lcd(LCD_RS_PIN, LCD_ENABLE_PIN, LCD_DATA_LN_4, LCD_DATA_LN_5, LCD_DATA_LN_6, LCD_DATA_LN_7);
LiquidCrystal_I2C lcd(0x3F,16,2);  // set the LCD address to 0x3F for a 16 chars and 2 line display

//------------------------------- stepper ----------------------------------
#define STEP_PIN 7
#define STEP_DIR_PIN 8
#define STEP_ENABLE_PIN 13
#define FORWARD HIGH
#define REVERSE LOW

//------------------------------- servo ----------------------------------
#define SNIP_PIN 10
#define SNIP_OPEN_ANGLE 160 //60
#define SNIP_CLOSE_ANGLE 60 // 155
// Construct a Servo object called snippers.
Servo snippers;

// Construct a Servo object called solder.
#define SOLDER_PIN 99                 //TBD define correct pin
#define SOLDER_FLUX_ANGLE 60          // define for 30 degrees under fixture
#define SOLDER_FLUX_STIR_ANGLE 61     // stir the strip in the flux
#define SOLDER_VERTICAL_ANGLE 0       // define for vertical position
#define SOLDER_DIP_ANGLE 115          // define for 30 degrees away from fixture (so heat doesn't affect it)
#define SOLDER_TUBE_LEN_IN 3          // solder tube length in inches
#define SOLDER_LEVEL_TENTH_IN 4       // level of solder below tube, to calculate distance to dip stripped wire, tenths of inches
#define SOLDER_DIP_TIME_MS 1500       // how long the strip should be dipped into the solder, in milliseconds
Servo solder;

// Construct a Servo object called guide.
#define GUIDE_PIN 11
#define GUIDE_STRIP_ANGLE 60
#define GUIDE_CUT_ANGLE 50
Servo guide;

//------------------------------- input ----------------------------------

#define LEFT_BUTTON 14
#define RIGHT_BUTTON 9
#define UP_BUTTON 15
#define DOWN_BUTTON 6
#define NO_BUTTON 0

//------------------------------- states ---------------------------------
#define CHOOSE_GAGE 0
#define CHOOSE_WIRE_LENGTH 1
#define CHOOSE_STRIP1_LENGTH 2
#define CHOOSE_SOLDER 3
#define CHOOSE_STRIP2_LENGTH 4
#define CHOOSE_QUANTITY 5
#define CONFIRM_SELECTIONS 6
#define CUTTING 7

//------------------------------- forward declarations ---------------------------------
void setState(int state);

//-------------------- user settings, prefixed with g_ for global variables --------------
// When adding a new gage, update MAX_GAGES and add the appropriate values to the arrays
#define MAX_GAGES 3
unsigned int g_gageSizes[MAX_GAGES]       = { 10,  10,  10}; 
unsigned int g_gageStripAngles[MAX_GAGES] = {55, 55, 55};//{105, 105, 103};
unsigned int g_gageStripHolds[MAX_GAGES]  = { 75,  75,  75};//{ 87,  92,  97};
//#define MAX_GAGES 7
//unsigned int g_gageSizes[MAX_GAGES]       = { 12,  14,  16,   18,  20,  22,  24}; 
//unsigned int g_gageStripAngles[MAX_GAGES] = {103, 103, 105 , 105, 103, 103, 103};
//unsigned int g_gageStripHolds[MAX_GAGES]  = { 97,  97,  87,   92,  97,  97,  97};

unsigned int g_selectedAwgIdx = 1; // index into arrays above, range is 0 to MAX_GAGES-1
String g_selectedAwgText(g_gageSizes[g_selectedAwgIdx]);
unsigned int g_selectedAwgStripAngle = g_gageStripAngles[g_selectedAwgIdx];
unsigned int g_selectedAwgHoldAngle = g_gageStripHolds[g_selectedAwgIdx];
unsigned int g_wireLength = 1;
unsigned int g_wireStripLength1 = 2;
unsigned int g_wireQuantity = 1;
unsigned int g_wireStripLength2 = 2;
bool g_solderSelected = false;

String g_displayValue = "";
String g_selection;
String g_units;
String g_navigation("<BACK      NEXT>");

//------------------------------- system settings ----------------------------------
int g_state;
int g_lastState = CONFIRM_SELECTIONS;

const float tenthinPerStep = 0.0129;////0.0129 cuts a little long for 10awg. was 0.00645 for small gauge cutter

void setup() {
    Serial.begin(9600);

    lcd.init();
    lcd.clear();         
    lcd.backlight();      // Make sure backlight is on
    //lcd.begin(16, 2); //LCD columns and rows
  
    pinMode(UP_BUTTON, INPUT_PULLUP);
    pinMode(DOWN_BUTTON, INPUT_PULLUP);
    pinMode(LEFT_BUTTON, INPUT_PULLUP);
    pinMode(RIGHT_BUTTON, INPUT_PULLUP);
  
    pinMode(STEP_PIN,OUTPUT);
    pinMode(STEP_DIR_PIN,OUTPUT);
    pinMode(STEP_ENABLE_PIN,OUTPUT);
  
    snippers.attach(SNIP_PIN);
    snippers.write(SNIP_OPEN_ANGLE);
    
    guide.attach(GUIDE_PIN);
    guide.write(GUIDE_STRIP_ANGLE);
  
    solder.attach(SOLDER_PIN);
  
    delay(1000);
    lcd.clear();
    setState(CHOOSE_GAGE);
}

// Get the button pressed by the user without waiting. Return "no button" if none was pressed.
int getButton()
{
  int retValue = NO_BUTTON;
  if (0 == digitalRead(RIGHT_BUTTON))
  {
    retValue = RIGHT_BUTTON;
  }
  if (0 == digitalRead(LEFT_BUTTON))
  {
    retValue = LEFT_BUTTON;
  }
  if (0 == digitalRead(UP_BUTTON))
  {
    retValue = UP_BUTTON;
  }
  if (0 == digitalRead(DOWN_BUTTON))
  {
    retValue = DOWN_BUTTON;
  }
  return(retValue);
}

// Set the current gage based on the index passed in. Index is used to access the
// arrays containing valid gage sizes and strip angles: 
//   g_gageSizes, g_gageStripAngles, g_gageStripHolds
void setupForGage(unsigned int awgIdx)
{
  Serial.print("setupForGage ");
  Serial.println(awgIdx);

  if (awgIdx < MAX_GAGES)
  {
    g_selectedAwgIdx = awgIdx;
    g_selectedAwgText = String(g_gageSizes[awgIdx]);
    g_selectedAwgStripAngle = g_gageStripAngles[awgIdx];
    g_selectedAwgHoldAngle = g_gageStripHolds[awgIdx];
    Serial.println(g_selectedAwgText);
  }
}

// Update the display to show the user the current state and g_selection.
void updateDisplay(void)
{
  // debug output to serial port
  Serial.print(g_selection);
  Serial.print(g_displayValue);
  Serial.print(" ");
  Serial.println(g_units);
  Serial.println(g_navigation);

  // same output to hardware LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(g_selection + g_displayValue + " " + g_units);
  lcd.setCursor(0, 1);
  lcd.print(g_navigation);
  delay(250);
}

// Set the state to the requested new state.
void setState(int newState)
{
  String build("");
  char s1 = (g_wireStripLength1 > 0) ? 'Y' : 'N';
  char sol = (g_solderSelected) ? 'Y' : 'N';
  char s2 = (g_wireStripLength2 > 0) ? 'Y' : 'N';
 
  Serial.print("setState ");
  Serial.println(String(newState));
  g_units = "";
  g_lastState = g_state;
  switch (newState){
    case CUTTING:
      g_selection = "CUTTING: ";
      g_displayValue = String(g_wireQuantity);
      g_units = "pcs";
      updateDisplay();
      currentlyCutting();
      g_selection = "Done w/ ";
      break;
    case CHOOSE_GAGE:
      g_selection = "GAGE: ";
      g_units = "AWG";
      g_displayValue = g_selectedAwgText;
      break;
    case CHOOSE_WIRE_LENGTH:
      g_selection = "LENGTH: ";
      g_units = "INCH";
      g_displayValue = String(g_wireLength);
      break;
    case CHOOSE_STRIP1_LENGTH:
      g_selection = "STRIP1: .";
      g_units = "INCH";
      g_displayValue = String(g_wireStripLength1);
      break;
    case CHOOSE_STRIP2_LENGTH:
      g_selection = "STRIP2: .";
      g_units = "INCH";
      g_displayValue = String(g_wireStripLength2);
      break;
    case CHOOSE_SOLDER:
      g_selection = "SOLDER ";
      g_units = "";
      g_displayValue = "YES";
      if (!g_solderSelected) g_displayValue = "NO";
      break;
    case CHOOSE_QUANTITY:
      g_selection = "QUANTITY: ";
      g_units = "";
      g_displayValue = String(g_wireQuantity);
      break;
    case CONFIRM_SELECTIONS:
      build.concat(g_wireLength);
      build.concat("in*");
      build.concat(g_wireQuantity);
      build.concat(":");
      build.concat(s1);
      build.concat(sol);
      build.concat(s2);
      g_selection = "Go? ";
      g_displayValue = build;
      g_units = "";
      break;
    default:
      break;
  }
  Serial.println(g_selection);
  g_state = newState;
  updateDisplay();
}

// Change the quantity associated with the state appropriately - 
// if the button is up, then increment; if the button is down then
// decrement but stop at the minimum available. For values that have 
// a max, cycle from max to min and vice versa.
void changeSelection(int state, int newButton)
{
  int awgIdx = g_selectedAwgIdx;
  Serial.print("changeg_selection ");
  Serial.println((newButton == DOWN_BUTTON) ? "Down" : "Up");
  switch (state)
  {
    case CHOOSE_GAGE:
      if (newButton == DOWN_BUTTON) 
      {
        awgIdx = (g_selectedAwgIdx > 0) ? g_selectedAwgIdx - 1 : MAX_GAGES - 1;
      }
      if (newButton == UP_BUTTON) 
      {
        awgIdx = (g_selectedAwgIdx < MAX_GAGES - 1) ? g_selectedAwgIdx + 1 : 0;
      }
      setupForGage(awgIdx);
      g_displayValue = g_selectedAwgText;
      updateDisplay();
      break;
    case CHOOSE_WIRE_LENGTH:
      if ((newButton == DOWN_BUTTON) && (g_wireLength > 0)) 
      {
        g_wireLength--;
      }
      else if (newButton == UP_BUTTON)
      {
        g_wireLength++;
      }
      g_displayValue = String(g_wireLength);
      updateDisplay();
      break;
    case CHOOSE_STRIP1_LENGTH:
      if ((newButton == DOWN_BUTTON) && (g_wireStripLength1 > 0)) 
      {
        g_wireStripLength1--;
      }
      else if (newButton == UP_BUTTON)
      {
        g_wireStripLength1++;
      }
      g_displayValue = String(g_wireStripLength1);
      updateDisplay();
      break;
    case CHOOSE_STRIP2_LENGTH:
      if ((newButton == DOWN_BUTTON) && (g_wireStripLength2 > 0)) 
      {
        g_wireStripLength2--;
      }
      else if (newButton == UP_BUTTON)
      {
        g_wireStripLength2++;
      }
      g_displayValue = String(g_wireStripLength2);
      updateDisplay();
      break;
    case CHOOSE_SOLDER:
      g_solderSelected = !g_solderSelected; 
      g_displayValue = "YES";
      if (!g_solderSelected)
      {
        g_displayValue = "NO";
      }
      updateDisplay();
      break;
    case CHOOSE_QUANTITY:
      if ((newButton == DOWN_BUTTON) && (g_wireQuantity > 0)) 
      {
        g_wireQuantity--;
      }
      else if (newButton == UP_BUTTON)
      {
        g_wireQuantity++;
      }
      g_displayValue = String(g_wireQuantity);
      updateDisplay();
      break;
    case CONFIRM_SELECTIONS:
      break;
    case CUTTING:
      break;
    default:
      break;
  }
}

// Decide where to go next when back/next (left/right) buttons are pressed.
void changeState(int newButton)
{
  int newState = g_state;
  String dir = "PREV";
  g_lastState = g_state;
  if (newButton == RIGHT_BUTTON)
  {
    dir = "NEXT";
  }
  Serial.println(dir);
  switch (g_lastState)
  {
    case CHOOSE_GAGE:
      newState = (newButton == RIGHT_BUTTON) ? CHOOSE_WIRE_LENGTH : CHOOSE_QUANTITY;
      break;
    case CHOOSE_WIRE_LENGTH:
      newState = (newButton == RIGHT_BUTTON) ? CHOOSE_STRIP1_LENGTH : CHOOSE_GAGE;
      break;
    case CHOOSE_STRIP1_LENGTH:
      newState = (newButton == RIGHT_BUTTON) ? CHOOSE_STRIP2_LENGTH : CHOOSE_WIRE_LENGTH;
      break;
    case CHOOSE_STRIP2_LENGTH:
      newState = (newButton == RIGHT_BUTTON) ? CHOOSE_SOLDER : CHOOSE_STRIP1_LENGTH;
      break;
    case CHOOSE_SOLDER:
      newState = (newButton == RIGHT_BUTTON) ? CHOOSE_QUANTITY : CHOOSE_STRIP2_LENGTH;
      break;
    case CHOOSE_QUANTITY:
      newState = (newButton == RIGHT_BUTTON) ? CONFIRM_SELECTIONS : CHOOSE_SOLDER;
      break;
    case CONFIRM_SELECTIONS:
      newState = (newButton == RIGHT_BUTTON) ? CUTTING : CHOOSE_QUANTITY;
      break;
    case CUTTING:
      newState = (newButton == RIGHT_BUTTON) ? CHOOSE_GAGE : CONFIRM_SELECTIONS;
      break;
    default:
      newState = g_lastState; 
      break;
  }
  
  if (g_lastState != newState)
  {
    setState(newState);
  }
}

// This is the main loop. It continually checks for state changes caused by button presses
// or completion of cutting.
void loop() 
{
  int selectedButton = getButton();
  
  if (selectedButton != NO_BUTTON)
  {
    if (selectedButton == RIGHT_BUTTON)
    {
      changeState(selectedButton);
    }
    if (selectedButton == LEFT_BUTTON)
    {
      changeState(selectedButton);
    }
    if (selectedButton == UP_BUTTON)
    {
      changeSelection(g_state, selectedButton);
    }
    if (selectedButton == DOWN_BUTTON)
    {
      changeSelection(g_state, selectedButton);
    }
    
    selectedButton = NO_BUTTON;
    delay(250);
  }
  
}

// This is the workhorse. It takes all the settings selected in the g_selection states and 
// runs the system accordingly.
void currentlyCutting(){
  unsigned long timeForOneCycle = millis(); //shows time remaining
  unsigned long timeRemaining = 999;
  float scaledLenS1 = (float)g_wireStripLength1 / 10;
  float scaledLenS2 = (float)g_wireStripLength2 / 10;
  float scaledLen = (float)g_wireLength; // requested length is between stripped ends.
  
/* cuts any extra portion of the wire before stripping and cutting begins */  
  guide.write(GUIDE_CUT_ANGLE);
  delay(600);
  snippers.write(SNIP_CLOSE_ANGLE);
  delay(600);
  snippers.write(SNIP_OPEN_ANGLE);
  delay(600);
  guide.write(GUIDE_STRIP_ANGLE);
  delay(600);
  Serial.println("snipped");

  // Repeat the same actions for each wire up to g_wireQuantity selected by user
  for(int i = 0; i < g_wireQuantity; i++)
  { 
    // Send output to both the LCD and the serial port for debugging.
    // First time through we don't know the time remaining yet,
    // after than we can print the seconds left.
    lcd.clear();
    lcd.setCursor(0, 0);
    if (i == 0) {
      lcd.print("AW YIS! 1st cut");
      Serial.println("AW YIS! 1st cut");
    } else {
      lcd.print("Left: " + (String)timeRemaining + "s    ");
      Serial.print("Left: ");
      Serial.print(timeRemaining);
      Serial.println(" s");
    }
    lcd.setCursor(0, 1);
    lcd.print((String)(i+1) + "/" + (String)g_wireQuantity);
    Serial.print(i+1);
    Serial.print("/");
    Serial.println(g_wireQuantity);
    
    // When the first strip length is not 0, advance the wire, cut it,
    // reverse the wire, strip it, and finally, if soldering is selected, solder it.
    if (scaledLenS1 > 0) {
      moveStepper(FORWARD, scaledLenS1, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);
      
      snippers.write(g_selectedAwgStripAngle);
      delay(600);//must be this long or leaves insulation on wire 
      snippers.write(g_selectedAwgHoldAngle);
      delay(600);
      Serial.println("stripped 1");
      
      moveStepper(REVERSE, scaledLenS1, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);
      
      snippers.write(SNIP_OPEN_ANGLE);
      delay(1000);
      
      if (g_solderSelected) {
        solderStrip1();
      }        
    }
    
    // Move the selected length of wire forward.
    moveStepper(FORWARD, scaledLen + scaledLenS1, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);
    
    // When the second strip length is not 0, slice the insulation and then advance
    // the strip length for the final cut
    if (scaledLenS2 > 0) {
      snippers.write(g_selectedAwgStripAngle);
      delay(600);
      snippers.write(SNIP_OPEN_ANGLE);
      delay(600);
      Serial.println("stripped 2");
      
      moveStepper(FORWARD, scaledLenS2, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);
    }


    // Servo Guide move to cut angle
    guide.write(GUIDE_CUT_ANGLE);
    delay(600);
    // Make the final cut
    snippers.write(SNIP_CLOSE_ANGLE);
    delay(600);
    snippers.write(SNIP_OPEN_ANGLE);
    delay(600);
    guide.write(GUIDE_STRIP_ANGLE);
    delay(600);
    
    digitalWrite(STEP_ENABLE_PIN,LOW);
    delayMicroseconds(500);
    if (i == 0)
    {
      timeForOneCycle = millis() - timeForOneCycle;
    }
    timeRemaining = (timeForOneCycle*(g_wireQuantity - (i+1)))/1000;
  }
} // end of currentlycutting

// moveStepper will move the stepper motor connected to the pins the number
// of requested steps in the requested direction.
// arguments:
//   dir FORWARD or REVERSSE
//   inches - any number of inches (float)
//   enablePin - the pin that enables the stepper
//   stepperPin - the pin that triggers the step
//   directPin - the pin that controls stepper direction
// example 
//   moveStepper(FORWARD, 0.2, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);
void moveStepper(int dir, float inches, int enablePin, int stepperPin, int directPin)
{
  int stepsToTake = (int)(inches/tenthinPerStep); //
  Serial.print((dir == FORWARD) ? "Forward" : "Reverse");
  Serial.print("Inches ");
  Serial.print(inches);
  Serial.print(" as steps -> ");
  Serial.println(stepsToTake);
  
  //starting at zero. count up to steps incrementing by 1 and do everything between the curly braces (everything between braces in called your block)
  for(int x = 0; x < stepsToTake; x++) { 
    
    // Enable the stepper
    digitalWrite(enablePin,HIGH);
    
    // Set the direction and delay to let the stepper complete
    if (dir == FORWARD)
    {
      digitalWrite(directPin,HIGH);
    }
    else
    {
      digitalWrite(directPin,LOW);
    }
    delayMicroseconds(500);
    
    // Toggle the stepper pin high, delay, then low and delay to let the stepper complete
    digitalWrite(stepperPin,HIGH);
    delayMicroseconds(1500);//2000 normally
    digitalWrite(stepperPin,LOW);
    delayMicroseconds(1500);//2000 normally
    
    // Disable the stepper
    digitalWrite(enablePin,LOW);
  }
}


// solderStrip1 moves the stripped wire to dip it into a prepared flux and then moves it to the heat source
// before making the final strip/cut and dropping the completed wire down the vertially aligned solder tube.
void solderStrip1()
{
  float scaledLenS1 = (float)g_wireStripLength1 / 10;
  float scaledLenS2 = (float)g_wireStripLength2 / 10;
  float scaledLen = (float)g_wireLength; // requested length is between stripped ends.
  float inchLen = 0.0;
  Serial.println("Soldering-ish");
  
  // move solder tube into flux position. Extend wire tubelength plus strip length. Move solder tube into stir position to
  // cover stripped wire with flux. Retract wire strip length. Move solder tube to solder position. Extend wire length of tube
  // plus length of strip plus solder level. Wait the desired number of milliseconds. Move the tube to vertical. 
  // Retract the wire up the tube to make the second strip and/or cut. 
  
  // sample solder code
  Serial.print ("Move solder tube to flux ");
  Serial.println(SOLDER_FLUX_ANGLE);
  // solder.write(SOLDER_FLUX_ANGLE);
  delay(1000);
 
  inchLen = SOLDER_TUBE_LEN_IN + scaledLenS1;
  moveStepper(FORWARD, inchLen, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);
  Serial.print ("Move solder tube to stir ");
  Serial.println(SOLDER_FLUX_STIR_ANGLE);
  // solder.write(SOLDER_FLUX_STIR_ANGLE);
  delay(1000);
 
  moveStepper(REVERSE, scaledLenS1, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);

  Serial.print ("Move solder tube to solder ");
  Serial.println(SOLDER_DIP_ANGLE);
  // solder.write(SOLDER_DIP_ANGLE);
  delay(600);

  inchLen = scaledLenS1 + ((float)SOLDER_LEVEL_TENTH_IN/10);
  moveStepper(FORWARD, inchLen, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);

  delay(SOLDER_DIP_TIME_MS);

  inchLen = SOLDER_TUBE_LEN_IN + scaledLenS1 + ((float)SOLDER_LEVEL_TENTH_IN/10);
  moveStepper(REVERSE, inchLen, STEP_ENABLE_PIN, STEP_PIN, STEP_DIR_PIN);
 
  Serial.print ("Move solder tube to vertical ");
  Serial.println(SOLDER_VERTICAL_ANGLE);
  // solder.write(SOLDER_VERTICAL_ANGLE);
  delay(600);

}
