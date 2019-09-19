/* Author: Nathan Seidle
  Created: Septempter 19th, 2019
  License: MIT. See SparkFun Arduino Apollo3 Project for more information

  This example demonstrates the following:
    Activate 1.8V vreg
    Ping Accel
    Ping mics
    Twinkle LEDs
    Drive Qwiic pins
*/

#include <Wire.h>

#include "SparkFun_LIS2DH12.h"

SPARKFUN_LIS2DH12 accel; //Create instance of object

#define ACCEL_ADDRESS 0x19 //On the Edge and Edge2 the accelerometer is address 0x19

unsigned long lastTimeCheck = 0;

#define QWIIC_SCL 39
#define QWIIC_SDA 30

#define VDD_CAMERA 32
#define VDD_PDM 0 //Only available on v2.2 PCB and above
#define VDD_ACCEL 41 //Only available on v2.2 PCB and above

//-=-=-= Vars for PDM -=-=-=-

//Global variables needed for PDM library
#define pdmDataBufferSize 4096 //Default is array of 4096 * 32bit
uint32_t pdmDataBuffer[pdmDataBufferSize];

//Global variables needed for the FFT in this sketch
float g_fPDMTimeDomain[pdmDataBufferSize * 2];
float g_fPDMFrequencyDomain[pdmDataBufferSize * 2];
float g_fPDMMagnitudes[pdmDataBufferSize * 2];
uint32_t sampleFreq;

//Enable these defines for additional debug printing
#define PRINT_PDM_DATA 0
#define PRINT_FFT_DATA 0

#include <PDM.h> //Include PDM library included with the Aruino_Apollo3 core
AP3_PDM myPDM; //Create instance of PDM class

//Math library needed for FFT
#define ARM_MATH_CM4
#include <arm_math.h>

//-=-=-=- End Vars for PDM -=-=-=-

bool accelGood = false;
bool micGood = false;

void setup()
{
  Serial.begin(9600);
  Serial.println("SparkFun Accel Example");

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  pinMode(VDD_CAMERA, OUTPUT);
  digitalWrite(VDD_CAMERA, HIGH); //Power up camera 1.8V regulator.

  //Normally we would not drive the SDA/SCL lines directly on the Qwiic connector (Wire)
  //but the test jig is just looking for continuity
  pinMode(QWIIC_SCL, OUTPUT);
  digitalWrite(QWIIC_SCL, LOW);
  pinMode(QWIIC_SDA, OUTPUT);
  digitalWrite(QWIIC_SDA, HIGH);

  pinMode(VDD_ACCEL, OUTPUT);
  digitalWrite(VDD_ACCEL, HIGH); //Power up accel. Only available on v2.2 PCB and above.

  Wire1.begin(); //Accel is on IOM3 and defined in the variant file as Wire1.

  //By default the SparkFun library uses Wire. We need to begin
  //with Wire1 on the Edge/Edge2.
  if (accel.begin(ACCEL_ADDRESS, Wire1) == false)
  {
    Serial.println("Accelerometer not detected. Are you sure you did a Wire1.begin()? Freezing...");
    while (1);
  }

  pinMode(VDD_PDM, OUTPUT);
  digitalWrite(VDD_PDM, HIGH); //Power up mics. Only available on v2.2 PCB and above.

  if (myPDM.begin() == false) // Turn on PDM with default settings
  {
    Serial.println("PDM Init failed. Are you sure these pins are PDM capable?");
    while (1);
  }
  Serial.println("PDM Initialized");

  printPDMConfig();

  myPDM.getData(pdmDataBuffer, pdmDataBufferSize); //This clears the current PDM FIFO and starts DMA
}

void loop()
{
  //Toggle LEDs and Qwiic pins
  if (millis() - lastTimeCheck > 1000)
  {
    lastTimeCheck = millis();

    if (digitalRead(LED_GREEN) == LOW)
    {
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_RED, HIGH);
      digitalWrite(LED_YELLOW, LOW);
      digitalWrite(LED_BLUE, LOW);

      digitalWrite(QWIIC_SCL, HIGH);
      digitalWrite(QWIIC_SDA, LOW);
    }
    else
    {
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_BLUE, HIGH);

      digitalWrite(QWIIC_SCL, LOW);
      digitalWrite(QWIIC_SDA, HIGH);
    }
  }

  //Wait for new accel data
  while(accel.available() == false) ;

    float accelX = accel.getX();
    float accelY = accel.getY();
    float accelZ = accel.getZ();
    if (accelZ > 500.0) accelGood = true;
    
    float tempC = accel.getTemperature();

    Serial.print("Acc [mg]: ");
    Serial.print(accelX, 1);
    Serial.print(" x, ");
    Serial.print(accelY, 1);
    Serial.print(" y, ");
    Serial.print(accelZ, 1);
    Serial.print(" z, ");
    Serial.print(tempC, 1);
    Serial.print("C");

  //Wait for new PDM data
  while(myPDM.available() == false) ;

    noInterrupts();
    printLoudest();

    while (PRINT_PDM_DATA || PRINT_FFT_DATA);

    // Start converting the next set of PCM samples.
    myPDM.getData(pdmDataBuffer, pdmDataBufferSize);
    interrupts();

  if(accelGood && micGood) Serial.println(" Accel/Mic Good");
}

//*****************************************************************************
//
// Analyze and print frequency data.
//
//*****************************************************************************
void printLoudest(void)
{
  float fMaxValue;
  uint32_t ui32MaxIndex;
  int16_t *pi16PDMData = (int16_t *) pdmDataBuffer;
  uint32_t ui32LoudestFrequency;

  //
  // Convert the PDM samples to floats, and arrange them in the format
  // required by the FFT function.
  //
  for (uint32_t i = 0; i < pdmDataBufferSize; i++)
  {
    if (PRINT_PDM_DATA)
    {
      Serial.printf("%d\n", pi16PDMData[i]);
    }

    g_fPDMTimeDomain[2 * i] = pi16PDMData[i] / 1.0;
    g_fPDMTimeDomain[2 * i + 1] = 0.0;
  }

  if (PRINT_PDM_DATA)
  {
    Serial.printf("END\n");
  }

  //
  // Perform the FFT.
  //
  arm_cfft_radix4_instance_f32 S;
  arm_cfft_radix4_init_f32(&S, pdmDataBufferSize, 0, 1);
  arm_cfft_radix4_f32(&S, g_fPDMTimeDomain);
  arm_cmplx_mag_f32(g_fPDMTimeDomain, g_fPDMMagnitudes, pdmDataBufferSize);

  if (PRINT_FFT_DATA)
  {
    for (uint32_t i = 0; i < pdmDataBufferSize / 2; i++)
    {
      Serial.printf("%f\n", g_fPDMMagnitudes[i]);
    }

    Serial.printf("END\n");
  }

  //
  // Find the frequency bin with the largest magnitude.
  //
  arm_max_f32(g_fPDMMagnitudes, pdmDataBufferSize / 2, &fMaxValue, &ui32MaxIndex);

  ui32LoudestFrequency = (sampleFreq * ui32MaxIndex) / pdmDataBufferSize;

  if (PRINT_FFT_DATA)
  {
    Serial.printf("Loudest frequency bin: %d\n", ui32MaxIndex);
  }

  Serial.printf(" Loudest frequency: %d", ui32LoudestFrequency);

  if(ui32LoudestFrequency > 0) micGood = true;
}

//*****************************************************************************
//
// Print PDM configuration data.
//
//*****************************************************************************
void printPDMConfig(void)
{
  uint32_t PDMClk;
  uint32_t MClkDiv;
  float frequencyUnits;

  //
  // Read the config structure to figure out what our internal clock is set
  // to.
  //
  switch (myPDM.getClockDivider())
  {
    case AM_HAL_PDM_MCLKDIV_4: MClkDiv = 4; break;
    case AM_HAL_PDM_MCLKDIV_3: MClkDiv = 3; break;
    case AM_HAL_PDM_MCLKDIV_2: MClkDiv = 2; break;
    case AM_HAL_PDM_MCLKDIV_1: MClkDiv = 1; break;

    default:
      MClkDiv = 0;
  }

  switch (myPDM.getClockSpeed())
  {
    case AM_HAL_PDM_CLK_12MHZ:  PDMClk = 12000000; break;
    case AM_HAL_PDM_CLK_6MHZ:   PDMClk =  6000000; break;
    case AM_HAL_PDM_CLK_3MHZ:   PDMClk =  3000000; break;
    case AM_HAL_PDM_CLK_1_5MHZ: PDMClk =  1500000; break;
    case AM_HAL_PDM_CLK_750KHZ: PDMClk =   750000; break;
    case AM_HAL_PDM_CLK_375KHZ: PDMClk =   375000; break;
    case AM_HAL_PDM_CLK_187KHZ: PDMClk =   187000; break;

    default:
      PDMClk = 0;
  }

  //
  // Record the effective sample frequency. We'll need it later to print the
  // loudest frequency from the sample.
  //
  sampleFreq = (PDMClk / (MClkDiv * 2 * myPDM.getDecimationRate()));

  frequencyUnits = (float) sampleFreq / (float) pdmDataBufferSize;

  Serial.printf("Settings:\n");
  Serial.printf("PDM Clock (Hz):         %12d\n", PDMClk);
  Serial.printf("Decimation Rate:        %12d\n", myPDM.getDecimationRate());
  Serial.printf("Effective Sample Freq.: %12d\n", sampleFreq);
  Serial.printf("FFT Length:             %12d\n\n", pdmDataBufferSize);
  Serial.printf("FFT Resolution: %15.3f Hz\n", frequencyUnits);
}
