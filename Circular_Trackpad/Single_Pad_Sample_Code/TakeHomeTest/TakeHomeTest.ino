// Copyright (c) 2018 Cirque Corp. Restrictions apply. See: www.cirque.com/sw-license

#include <SPI.h>

// ___ Using a Cirque TM0XX0XX w/ Curved Overlay and Arduino ___
// This demonstration application is built to work with a Teensy 3.1/3.2 but it can easily be adapted to
// work with Arduino-based systems.
// When using with DK000013 development kit, connect sensor to the FFC connector
// labeled 'Sensor0'.
// This application connects to a TM0XX0XX circular touch pad via SPI. To verify that your touch pad is configured
// for SPI-mode, make sure that R1 is populated with a 470k resistor (or whichever resistor connects pins 24 & 25 of the 1CA027 IC).
// The pad is configured for Absolute mode tracking.  Touch data is sent in text format over USB CDC to
// the host PC.  You can open a terminal window on the PC to the USB CDC port and see X, Y, and Z data
// fill the window when you touch the sensor. Tools->Serial Monitor can be used to view touch data.
// NOTE: all config values applied in this sample are meant for a module using REXT = 976kOhm

//  Pinnacle TM0XX0XX with Arduino
//  Hardware Interface
//  GND
//  +3.3V
//  SCK = Pin 13
//  MISO = Pin 12
//  MOSI = Pin 11
//  SS = Pin 8
//  DR = Pin 7

// Hardware pin-number labels
#define SCK_PIN 13
#define DIN_PIN 12
#define DOUT_PIN 11
#define CS_PIN 10
#define DR_PIN 9

#define SDA_PIN 18
#define SCL_PIN 19

#define LED_0 21
#define LED_1 20

// Masks for Cirque Register Access Protocol (RAP)
#define WRITE_MASK 0x80
#define READ_MASK 0xA0

// Register config values for this demo
#define SYSCONFIG_1 0x00
#define FEEDCONFIG_1 0x03
#define FEEDCONFIG_2 0x1F
#define Z_IDLE_COUNT 0x01

// Coordinate scaling values
#define PINNACLE_XMAX 2047    // max value Pinnacle can report for X
#define PINNACLE_YMAX 1535    // max value Pinnacle can report for Y
#define PINNACLE_X_LOWER 127  // min "reachable" X value
#define PINNACLE_X_UPPER 1919 // max "reachable" X value
#define PINNACLE_Y_LOWER 63   // min "reachable" Y value
#define PINNACLE_Y_UPPER 1471 // max "reachable" Y value
#define PINNACLE_X_RANGE (PINNACLE_X_UPPER - PINNACLE_X_LOWER)
#define PINNACLE_Y_RANGE (PINNACLE_Y_UPPER - PINNACLE_Y_LOWER)
#define PINNACLE_X_CENTER ((PINNACLE_X_LOWER + PINNACLE_X_UPPER) / 2)
#define PINNACLE_Y_CENTER ((PINNACLE_Y_LOWER + PINNACLE_Y_UPPER) / 2)
#define ZONESCALE 256 // divisor for reducing x,y values to an array index for the LUT
#define ROWS_Y ((PINNACLE_YMAX + 1) / ZONESCALE)
#define COLS_X ((PINNACLE_XMAX + 1) / ZONESCALE)

// ADC-attenuation settings (held in BIT_7 and BIT_6)
// 1X = most sensitive, 4X = least sensitive
#define ADC_ATTENUATE_1X 0x00
#define ADC_ATTENUATE_2X 0x40
#define ADC_ATTENUATE_3X 0x80
#define ADC_ATTENUATE_4X 0xC0

#define TAP_TIMEOUT_MSEC 300

#define DEBUG_SERIAL 0  // Print debug info to serial port
#define TAP_THRESH 300  // Threshold for tap duration in ms

// Convenient way to store and access measurements
typedef struct _absData
{
    uint16_t xValue;
    uint16_t yValue;
    uint16_t zValue;
    uint8_t buttonFlags;
    bool touchDown;
    bool hovering;
} absData_t;

// Global Declarations
absData_t touchData;
uint8_t initial_quadrant;
unsigned long touch_down, lift_off;
bool touch_recorded, maintained_quadrant;

//const uint16_t ZONESCALE = 256;
//const uint16_t ROWS_Y = 6;
//const uint16_t COLS_X = 8;

// These values require tuning for optimal touch-response
// Each element represents the Z-value below which is considered "hovering" in that XY region of the sensor.
// The values present are not guaranteed to work for all HW configurations.
const uint8_t ZVALUE_MAP[ROWS_Y][COLS_X] =
{
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 2, 3, 5, 5, 3, 2, 0},
    {0, 3, 5, 15, 15, 5, 2, 0},
    {0, 3, 5, 15, 15, 5, 3, 0},
    {0, 2, 3, 5, 5, 3, 2, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
};

// setup() gets called once at power-up, sets up serial debug output and Cirque's Pinnacle ASIC.
void setup()
{
    Serial.begin(115200);
    while (!Serial); // Wait for USB serial port to enumerate

    /*******************************************************************************
        YOUR STARTUP CODE HERE
     ******************************************************************************/
    touch_recorded = false;

    pinMode(LED_0, OUTPUT);

    Pinnacle_Init();

    // These functions are required for use with thick overlays (curved)
    //setAdcAttenuation(ADC_ATTENUATE_2X);
    //tuneEdgeSensitivity();

    // if the touchpad reports touches with no fingers near the pad then it may need
    // to be calibrated. Generally it doesn't.
    // Pinnacle_forceCalibration();

    Pinnacle_EnableFeed(true);

    Serial.println("** INITIALIZATION COMPLETE **");
}

// loop() continuously checks to see if data-ready (DR) is high. If so, reads and reports touch data to terminal.
void loop()
{
    /*******************************************************************************
        YOUR RUNNING CODE HERE
     ******************************************************************************/
    if (DR_Asserted())
    {
        unsigned long timer = millis();   // Update timer        
        Pinnacle_GetAbsolute(touchData);  // Read Pinnacle data into absData_t struct
        
        if (touchData.zValue == 0)
        {   // As long as Z-idle count is configured to >0, the first empty packet can be used as lift-off detection
            lift_off = timer;
            unsigned long touch_duration = (lift_off - touch_down);
            if (touch_duration <= TAP_THRESH && maintained_quadrant)
            {   // If the contact was under 300ms and never left its initial quadrant,
                //   acknowledge the tap by printing the quadrant to serial port
                Serial.print('Q');
                Serial.println(initial_quadrant);
            }
            touch_recorded = false;
            if (DEBUG_SERIAL)
            {
                Serial.print("Duration: ");
                Serial.print(touch_duration);
                Serial.println(" ms");
                Serial.println("**  LIFT-OFF  **");
            }
        }
        else
        {
            // Offset x & y positions with centerpoint
            int16_t x_pos = touchData.xValue - PINNACLE_X_CENTER;
            int16_t y_pos = touchData.yValue - PINNACLE_Y_CENTER;
            // Determine the current quadrant
            uint8_t quad = getQuadrant(x_pos, y_pos);

            if (!touch_recorded)
            {   // Save initial data when touch is first detected
                initial_quadrant = quad;
                maintained_quadrant = true;
                touch_down = timer;
                touch_recorded = true;
                if (DEBUG_SERIAL)
                {
                    Serial.println("** TOUCH DOWN **");
                }
            }

            if (quad != initial_quadrant)
            {
                // Update flag if touch leaves initial quadrant
                maintained_quadrant = false;
            }

            if (DEBUG_SERIAL)
            {
                // Print touchData to console if debug macro is enabled
                Serial.print(x_pos);
                Serial.print('\t');
                Serial.print(y_pos);
                Serial.print('\t');
                Serial.print(touchData.zValue);
                Serial.print("\tQ");
                Serial.println(quad);
            }
        }
    }
}

/** Determines the quadrant of a given x,y coordinate based off conventional Cartesian numbering.
 *
 *  @param  x The x-value of the coordinate
 *  @param  y the y-value of the coordinate
 *  @return The quadrant number of the coordinate (0-3)
 */
uint8_t getQuadrant(int16_t x, int16_t y)
{
    if (x >= 0 && y >= 0)
    {                             //              ^ y
        return 0;                 //              |
    }                             //              |
    else if (x < 0 && y >= 0)     //       Q1     |     Q0
    {                             //              |
        return 1;                 //              |
    }                             // -------------------------->
    else if (x < 0)               //              |            x
    {                             //              |
        return 2;                 //       Q2     |     Q3
    }                             //              |
    else                          //              |
    {                             //              |
        return 3;
    }
}

/*  Pinnacle-based TM0XX0XX Functions  */
void Pinnacle_Init()
{
    RAP_Init();
    DeAssert_CS();
    pinMode(DR_PIN, INPUT);

    // Host clears SW_CC flag
    Pinnacle_ClearFlags();

    // Host configures bits of registers 0x03 and 0x05
    RAP_Write(0x03, SYSCONFIG_1);
    RAP_Write(0x05, FEEDCONFIG_2);

    // Host enables preferred output mode (absolute)
    RAP_Write(0x04, FEEDCONFIG_1);

    // Host sets z-idle packet count (default is 30)
    RAP_Write(0x0A, Z_IDLE_COUNT);
    Serial.println("Pinnacle Initialized...");
}

// Reads XYZ data from Pinnacle registers 0x14 through 0x17
// Stores result in absData_t struct with xValue, yValue, and zValue members
void Pinnacle_GetAbsolute(absData_t &result)
{
    uint8_t data[6] = {0, 0, 0, 0, 0, 0};
    RAP_ReadBytes(0x12, data, 6);

    Pinnacle_ClearFlags();

    result.buttonFlags = data[0] & 0x3F;
    result.xValue = data[2] | ((data[4] & 0x0F) << 8);
    result.yValue = data[3] | ((data[4] & 0xF0) << 4);
    result.zValue = data[5] & 0x3F;

    result.touchDown = result.xValue != 0;
}

// Checks touch data to see if it is a z-idle packet (all zeros)
bool Pinnacle_zIdlePacket(absData_t *data)
{
    return data->xValue == 0 && data->yValue == 0 && data->zValue == 0;
}

// Clears Status1 register flags (SW_CC and SW_DR)
void Pinnacle_ClearFlags()
{
    RAP_Write(0x02, 0x00);
    delayMicroseconds(50);
}

// Enables/Disables the feed
void Pinnacle_EnableFeed(bool feedEnable)
{
    uint8_t temp;

    RAP_ReadBytes(0x04, &temp, 1); // Store contents of FeedConfig1 register

    if (feedEnable)
    {
        temp |= 0x01; // Set Feed Enable bit
        RAP_Write(0x04, temp);
    }
    else
    {
        temp &= ~0x01; // Clear Feed Enable bit
        RAP_Write(0x04, temp);
    }
}

/*  Curved Overlay Functions  */
// Adjusts the feedback in the ADC, effectively attenuating the finger signal
// By default, the the signal is maximally attenuated (ADC_ATTENUATE_4X for use with thin, flat overlays
void setAdcAttenuation(uint8_t adcGain)
{
    uint8_t temp = 0x00;

    Serial.println();
    Serial.println("Setting ADC gain...");
    ERA_ReadBytes(0x0187, &temp, 1);
    temp &= 0x3F; // clear top two bits
    temp |= adcGain;
    ERA_WriteByte(0x0187, temp);
    ERA_ReadBytes(0x0187, &temp, 1);
    Serial.print("ADC gain set to:\t");
    Serial.print(temp &= 0xC0, HEX);
    switch (temp)
    {
        case ADC_ATTENUATE_1X:
            Serial.println(" (X/1)");
            break;
        case ADC_ATTENUATE_2X:
            Serial.println(" (X/2)");
            break;
        case ADC_ATTENUATE_3X:
            Serial.println(" (X/3)");
            break;
        case ADC_ATTENUATE_4X:
            Serial.println(" (X/4)");
            break;
        default:
            break;
    }
}

// Changes thresholds to improve detection of fingers
void tuneEdgeSensitivity()
{
    uint8_t temp = 0x00;

    Serial.println();
    Serial.println("Setting xAxis.WideZMin...");
    ERA_ReadBytes(0x0149, &temp, 1);
    Serial.print("Current value:\t");
    Serial.println(temp, HEX);
    ERA_WriteByte(0x0149, 0x04);
    ERA_ReadBytes(0x0149, &temp, 1);
    Serial.print("New value:\t");
    Serial.println(temp, HEX);

    Serial.println();
    Serial.println("Setting yAxis.WideZMin...");
    ERA_ReadBytes(0x0168, &temp, 1);
    Serial.print("Current value:\t");
    Serial.println(temp, HEX);
    ERA_WriteByte(0x0168, 0x03);
    ERA_ReadBytes(0x0168, &temp, 1);
    Serial.print("New value:\t");
    Serial.println(temp, HEX);
}

// This function identifies when a finger is "hovering" so your system can choose to ignore them.
// Explanation: Consider the response of the sensor to be flat across it's area. The Z-sensitivity of the sensor projects this area
// a short distance upwards above the surface of the sensor. Imagine it is a solid cylinder (wider than it is tall)
// in which a finger can be detected and tracked. Adding a curved overlay will cause a user's finger to dip deeper in the middle, and higher
// on the perimeter. If the sensitivity is tuned such that the sensing area projects to the highest part of the overlay, the lowest
// point will likely have excessive sensitivity. This means the sensor can detect a finger that isn't actually contacting the overlay in the shallower area.
// ZVALUE_MAP[][] stores a lookup table in which you can define the Z-value and XY position that is considered "hovering". Experimentation/tuning is required.
// NOTE: Z-value output decreases to 0 as you move your finger away from the sensor, and it's maximum value is 0x63 (6-bits).
void Pinnacle_CheckValidTouch(absData_t *touchData)
{
    uint32_t zone_x, zone_y;
    //eliminate hovering
    zone_x = touchData->xValue / ZONESCALE;
    zone_y = touchData->yValue / ZONESCALE;
    touchData->hovering = !(touchData->zValue > ZVALUE_MAP[zone_y][zone_x]);
}

// Forces Pinnacle to re-calibrate. If the touchpad is reporting touches when
// no fingers are on the pad then calibration (compensation) is wrong.
// Calling this function will fix the problem. Warning, re-enable the feed after calling this.
void Pinnacle_forceCalibration(void)
{
    uint8_t CalConfig1Value = 0x00;

    Pinnacle_EnableFeed(false);
    RAP_ReadBytes(0x07, &CalConfig1Value, 1);
    CalConfig1Value |= 0x01;
    RAP_Write(0x07, CalConfig1Value);

    do
    {
        RAP_ReadBytes(0x07, &CalConfig1Value, 1);
    }
    while (CalConfig1Value & 0x01);

    Pinnacle_ClearFlags();
}

/*  ERA (Extended Register Access) Functions  */
// Reads <count> bytes from an extended register at <address> (16-bit address),
// stores values in <*data>
void ERA_ReadBytes(uint16_t address, uint8_t *data, uint16_t count)
{
    uint8_t ERAControlValue = 0xFF;

    Pinnacle_EnableFeed(false); // Disable feed

    RAP_Write(0x1C, (uint8_t)(address >> 8));     // Send upper byte of ERA address
    RAP_Write(0x1D, (uint8_t)(address & 0x00FF)); // Send lower byte of ERA address

    for (uint16_t i = 0; i < count; i++)
    {
        RAP_Write(0x1E, 0x05); // Signal ERA-read (auto-increment) to Pinnacle

        // Wait for status register 0x1E to clear
        do
        {
            RAP_ReadBytes(0x1E, &ERAControlValue, 1);
        } while (ERAControlValue != 0x00);

        RAP_ReadBytes(0x1B, data + i, 1);

        Pinnacle_ClearFlags();
    }
}

// Writes a byte, <data>, to an extended register at <address> (16-bit address)
void ERA_WriteByte(uint16_t address, uint8_t data)
{
    uint8_t ERAControlValue = 0xFF;

    Pinnacle_EnableFeed(false); // Disable feed

    RAP_Write(0x1B, data); // Send data byte to be written

    RAP_Write(0x1C, (uint8_t)(address >> 8));     // Upper byte of ERA address
    RAP_Write(0x1D, (uint8_t)(address & 0x00FF)); // Lower byte of ERA address

    RAP_Write(0x1E, 0x02); // Signal an ERA-write to Pinnacle

    // Wait for status register 0x1E to clear
    do
    {
        RAP_ReadBytes(0x1E, &ERAControlValue, 1);
    } while (ERAControlValue != 0x00);

    Pinnacle_ClearFlags();
}

/*  RAP Functions */

void RAP_Init()
{
    pinMode(CS_PIN, OUTPUT);
    SPI.begin();
}

// Reads <count> Pinnacle registers starting at <address>
void RAP_ReadBytes(byte address, byte *data, byte count)
{
    byte cmdByte = READ_MASK | address; // Form the READ command byte

    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE1));

    Assert_CS();
    SPI.transfer(cmdByte); // Signal a RAP-read operation starting at <address>
    SPI.transfer(0xFC);    // Filler byte
    SPI.transfer(0xFC);    // Filler byte
    for (byte i = 0; i < count; i++)
    {
        data[i] = SPI.transfer(0xFC); // Each subsequent SPI transfer gets another register's contents
    }
    DeAssert_CS();

    SPI.endTransaction();
}

// Writes single-byte <data> to <address>
void RAP_Write(byte address, byte data)
{
    byte cmdByte = WRITE_MASK | address; // Form the WRITE command byte

    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE1));

    Assert_CS();
    SPI.transfer(cmdByte); // Signal a write to register at <address>
    SPI.transfer(data);    // Send <value> to be written to register
    DeAssert_CS();

    SPI.endTransaction();
}

/*  Logical Scaling Functions */
// Clips raw coordinates to "reachable" window of sensor
// NOTE: values outside this window can only appear as a result of noise
void ClipCoordinates(absData_t *coordinates)
{
    if (coordinates->xValue < PINNACLE_X_LOWER)
    {
        coordinates->xValue = PINNACLE_X_LOWER;
    }
    else if (coordinates->xValue > PINNACLE_X_UPPER)
    {
        coordinates->xValue = PINNACLE_X_UPPER;
    }
    if (coordinates->yValue < PINNACLE_Y_LOWER)
    {
        coordinates->yValue = PINNACLE_Y_LOWER;
    }
    else if (coordinates->yValue > PINNACLE_Y_UPPER)
    {
        coordinates->yValue = PINNACLE_Y_UPPER;
    }
}

// Scales data to desired X & Y resolution
void ScaleData(absData_t *coordinates, uint16_t xResolution, uint16_t yResolution)
{
    uint32_t xTemp = 0;
    uint32_t yTemp = 0;

    ClipCoordinates(coordinates);

    xTemp = coordinates->xValue;
    yTemp = coordinates->yValue;

    // translate coordinates to (0, 0) reference by subtracting edge-offset
    xTemp -= PINNACLE_X_LOWER;
    yTemp -= PINNACLE_Y_LOWER;

    // scale coordinates to (xResolution, yResolution) range
    coordinates->xValue = (uint16_t)(xTemp * xResolution / PINNACLE_X_RANGE);
    coordinates->yValue = (uint16_t)(yTemp * yResolution / PINNACLE_Y_RANGE);
}

/*  I/O Functions */
void Assert_CS()
{
    digitalWrite(CS_PIN, LOW);
}

void DeAssert_CS()
{
    digitalWrite(CS_PIN, HIGH);
}

void AssertSensorLED(bool state)
{
    digitalWrite(LED_0, !state);
}

bool DR_Asserted()
{
    return digitalRead(DR_PIN);
}
