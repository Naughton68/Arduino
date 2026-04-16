//*********************************************************
//  Assumptions:
//     - Stepper Motor Driver configured for 16x microsteps
//  Changes from original soucee:
//     - Reassign IO Pins  
//     - Convert I2C OLED to SPI
//     - Refactored names of variables and constants
//     - Reordered/regrouped variables and constants
//*********************************************************

#define ENCODER_DO_NOT_USE_INTERRUPTS
#include <Encoder.h>
#include <Stepper.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define motorInterfaceType 

// ***********************
// *** Pin Assignments ***
// ***********************
const int CUTTER_STEP_PIN = 21;  // LINMOT: Linear motion
const int CUTTER_DIR_PIN = 19;

const int EXTRUDER_STEP_PIN = 17;  // The stepper that moves the wire in the extruder.
const int EXTRUDER_DIR_PIN = 16;

const int ENCODER_DATA_PIN = 26;
const int ENCODER_CLOCK_PIN = 25;
const int ENCODER_BUTTON_PIN = 27;

// For calibration only. The two buttons are used to move the top blade up and down manually.
// Once calibrated, you can remove the buttons from the circuit.
//  const int BUTTON_CALIBRATION_UP_PIN = __;
//  const int BUTTON_CALIBRATION_DN_PIN = __;

// ************************
// *** Screen Constants ***
// ************************
const int SCREEN_RS_PIN = 0;      // SPI Reset
const int SCREEN_DC_PIN = 2;      // SPI Register Select (Data/Command)
const int SCREEN_CS_PIN = 5;      // SPI Chip Select
const int SCREEN_CLK_PIN = 18;    // SPI Clock
const int SCREEN_MOSI_PIN = 23;   // SPI Data to device
//const int SCREEN_MISO_PIN = 19; // SPI Data from device (Not Needed)

const int SCREEN_WIDTH = 128;  // OLED display width, in pixels
const int SCREEN_HEIGHT = 64;  // OLED display height, in pixels
const int TEXT_SIZE = 2;
const int TEXT_OFFSET = 3;

// Values for drawing the wire at the top of the OLED screen.
const int WIRE_DISPLAY_LENGTH = 30;
const int WIRE_DISPLAY_POSITION_Y = 7;
const int INSULATION_DISPLAY_WIDTH = SCREEN_WIDTH - (WIRE_DISPLAY_LENGTH * 2);
const int INSULATION_DISPLAY_HEIGHT = 14;

// These are just references to the corresponding component index in the comps array;
const int STRIP_LENGTH_1_INDEX = 0;
const int STRIP_LENGTH_2_INDEX = 2;
const int STRIP_DEPTH_INDEX = 4;
const int WIRE_LENGTH_INDEX = 1;
const int QUANTITY_INDEX = 3;
const int BEGIN_CUTTING_INDEX = 5;

// *******************************
// *** Stepper Motor Constants ***
// *******************************
const int CUTTER_STEPS = 1;  // Steppers step(s) movement at a time.
const int CUTTER_SPEED = 2000;

const int EXTRUDER_STEPS = 1;
const int EXTRUDER_SPEED = 2000;

const int CUTTING_STEPS = 17750;  // Steps to move blade to fully cut the wire.
const int STRIPPING_MULTIPLIER = 300;  // The chosen stripping depth value on the screen is multiplied by this value.
const int EXTRUDE_WIRE_MULTIPLIER = 408;  // How much to move wire per unit on OLED, turn on CALIBRATION_MODE to find this value.
const int DELAY_BETWEEN_CUTS = 100;  // Delay in ms between each cut in the quantity.

// To calibrate the wire movement distance. Use the first cell to enter the distance in mm to move the wire.
// Then adjust WIRE_MOVEMENT_MULTI to get the correct wire length.
const boolean CALIBRATION_MODE = false;

// **********************************
// *** Datastructure Declarations ***
// **********************************

// A component is a cell with a value on the OLED display.
struct Component {
    int x, y;  // Position
    int w, h;  // Size
    int displayValue;  // Current value of the cell.
    boolean isHighlighted;  // Whether the cell is the currently highlighted cell.
    boolean isSelected;  // Whether the cell is currently the selected one, where its value will be controlled by the encoder.
    boolean isButton;  // If it is a button or not.
};

// *****************************
// *** Variable Declarations ***
// *****************************

Component screenComponents[] = 
{ 
    {  0,20, 40,20, 0, 0,0,0 },
    { 44,20, 40,20, 0, 0,0,0 },
    { 88,20, 40,20, 0, 0,0,0 },

    {  0,44, 40,20, 0, 0,0,0 },
    { 44,44, 40,20, 0, 0,0,0 },
    { 88,44, 40,20, 0, 0,0,1 } 
  };

int screenComponentCount = sizeof(screenComponents) / sizeof(screenComponents[0]);
int cutterStepPosition = 0;  // Current position/step of the stepper motor.

int encoder_Position;  // Current position/value of the encoder.
int encoder_LastPosition;  // For OLED drawing.
int encoder_LastPositionMain;  // For main loop.

boolean encoderButton_isPressed = false;
boolean encoderButton_wasPressed = false;  // For OLED drawing.
boolean encoderButton_wasPressedMain = false;  // For main loop.

Stepper cutterStepper(200, CUTTER_DIR_PIN, CUTTER_STEP_PIN);
Stepper extruderStepper(200, EXTRUDER_DIR_PIN, EXTRUDER_STEP_PIN);
Encoder encoder(ENCODER_DATA_PIN, ENCODER_CLOCK_PIN);

Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, SCREEN_MOSI_PIN, SCREEN_CLK_PIN, SCREEN_DC_PIN, SCREEN_RS_PIN, SCREEN_CS_PIN);


// *********************************************************

void setup() 
{
     pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
    //pinMode(CALIBRATION_UP_BUTTON_PIN, INPUT_PULLUP);
    //pinMode(BUTTON_CALIBRATION_DN_PIN, INPUT_PULLUP);     
    
    cutterStepper.setSpeed(CUTTER_SPEED);
    extruderStepper.setSpeed(EXTRUDER_SPEED);

    display.setRotation(2);
    display.begin(0, true);

}
 

//  ***** Application Logic *********************
//  Read Input PINS
//  Determine if any have changed
//    If button was pressed...
//      If the start field was pressed...
//        Start cutting wires
//      If an input value field was pressed...
//        Process changed to field value
//    If encoder position has changed...
//      Update screen with new field selected
//  *********************************************

  boolean buttonChanged = FALSE;
  boolean encoderChanged = FALSE;
  integer cellSelectionState = FALSE;

void loop() 
{

  // Read Input Pins
  encoder_Position = encoder.read() / 4;
  encoderButton_isPressed = digitalRead(ENCODER_BUTTON_PIN);

  if (encoderButton_isPressed != encoderButton_wasPressed) 
    buttonChanged = TRUE;

  if (encoder_Position != encoder_LastPosition) 
    encoderChanged = TRUE;


  if (encoderChanged)
  {
    encoder.write(encoder_LastPosition * 4);
  }

  if (buttonChanged || encoderChanged)
  {
    updateDisplay();
  }

  if (screenComponents[BEGIN_CUTTING_INDEX].isSelected) 
  {
      cutWires();
  }

  encoder_LastPosition = encoder_Position;
  encoderButton_wasPressed = encoderButton_isPressed;

  buttonChanged = FALSE;
  encoderChanged = FALSE;
}

// *********************************************************

void selectCell()
{}

void changeCellValue()
{}



void updateDisplay() 
{
    display.clearDisplay();
    updateRegionValue();
    drawWire();
    display.display();
}


void updateRegionValue() //???????
{
    for (int regionNumber = 0; regionNumber < screenComponentCount; regionNumber++) 
    {
        Component& screenRegion = screenComponents[regionNumber];

        // If we're on the screen region button
        if (encoder_Position == regionNumber) 
        {
            screenRegion.isHighlighted = true;

            if (encoderButton_isPressed) 
            {
                if (!screenRegion.isSelected && !screenRegion.isButton) 
                {
                    encoder.write(screenRegion.displayValue * 4);
                }

                // Input should be captured up stream
                screenRegion.isSelected = true;
                screenRegion.displayValue = getEncoderPos();
            }
            else 
            {
                screenRegion.isSelected = false;
            }
        }
        else 
        {
            screenRegion.isHighlighted = false;
            screenRegion.isSelected = false;
        }

        drawScreenComponent(screenRegion);
    }
}

void drawWire() {
    display.drawLine(0, WIRE_DISPLAY_POSITION_Y, WIRE_DISPLAY_LENGTH, WIRE_DISPLAY_POSITION_Y, SH110X_WHITE);
    display.fillRect(WIRE_DISPLAY_LENGTH, 0, INSULATION_DISPLAY_WIDTH, INSULATION_DISPLAY_HEIGHT, SH110X_WHITE);
    display.drawLine(WIRE_DISPLAY_LENGTH + INSULATION_DISPLAY_WIDTH, WIRE_DISPLAY_POSITION_Y, SCREEN_WIDTH, WIRE_DISPLAY_POSITION_Y, SH110X_WHITE);
}

void drawScreenComponen
(Component comp) {
    if (comp.isHighlighted) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.fillRect(comp.x, comp.y, comp.w, comp.h, SH110X_WHITE);

        if (comp.isSelected) {
            display.drawRect(comp.x - 1, comp.y - 1, comp.w + 2, comp.h + 2, SH110X_WHITE);
        }

    }
    else {
        display.setTextColor(SH110X_WHITE, SH110X_BLACK);
        display.drawRect(comp.x, comp.y, comp.w, comp.h, SH110X_WHITE);
    }

    if (comp.isButton) {
        display.setTextSize(1);
        drawText("Start", comp.x + TEXT_OFFSET, comp.y + TEXT_OFFSET);
    }
    else {
        display.setTextSize(TEXT_SIZE);
        drawText(String(comp.displayValue), comp.x + TEXT_OFFSET, comp.y + TEXT_OFFSET);
    }
}


void drawText(String text, int x, int y) {
    display.setCursor(x, y);
    display.println(text);
}

void cutWires() {
    if (CALIBRATION_MODE) {
        moveWire(screenComponents[STRIP_LENGTH_1_INDEX].displayValue);
        cut();
    }
    else {
        cut();
        delay(DELAY_BETWEEN_CUTS);

        for (int i = 0; i < screenComponents[QUANTITY_INDEX].displayValue; i++) 
        {
            moveWire(screenComponents[STRIP_LENGTH_1_INDEX].displayValue);
            strip();

            moveWire(screenComponents[WIRE_LENGTH_INDEX].displayValue);
            strip();

            moveWire(screenComponents[STRIP_LENGTH_2_INDEX].displayValue);
            cut();

            delay(DELAY_BETWEEN_CUTS);
        }
    }

    screenComponents[BEGIN_CUTTING_INDEX].isSelected = false;
    encoderButton_isPressed = false;
}

void cut() {
    moveCutter(-CUTTING_STEPS);
    moveCutter( CUTTING_STEPS);
}


void strip() {
    moveCutter(-(screenComponents[STRIP_DEPTH_INDEX].displayValue * STRIPPING_MULTIPLIER));
    moveCutter(  screenComponents[STRIP_DEPTH_INDEX].displayValue * STRIPPING_MULTIPLIER);
}


void moveCutter(int steps) {
    cutterStepper.step(steps);
    cutterStepPosition += steps;
}


void moveWire(int steps) {
    extruderStepper.step(steps * EXTRUDE_WIRE_MULTIPLIER);
}

