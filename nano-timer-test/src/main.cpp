#include <Arduino.h>
#include <LowPower.h>
#include <ezButton.h>

const int wakeUpPin = 2;
volatile boolean interupted = false;

#define DEBOUNCE_DELAY        50

// There's a pull-up resistor in the circuit, otherwise INPUT_PULLUP would be needed below
ezButton btnMagnet(wakeUpPin, INPUT);

void ISRwakeUp()
{
  interupted = true;
}


void setup()
{
  // Set up debounce time for magnet switch & parallel test button
  btnMagnet.setDebounceTime(DEBOUNCE_DELAY);

  Serial.begin(9600);

  // // There's a pull-up resistor in the circuit, otherwise INPUT_PULLUP would be needed below
  // pinMode(wakeUpPin, INPUT);

  Serial.println("Starting up");

}


void loop()
{

  // MUST call the .loop() method for every ezButton each time round the loop
  btnMagnet.loop();

  if (btnMagnet.isPressed()) {
    Serial.println("Button pressed");
  }

  Serial.println("About to sleep for 8 seconds");
  delay(50);

  // Allow wake up pin to trigger interrupt on low.
  attachInterrupt(digitalPinToInterrupt(wakeUpPin), ISRwakeUp, LOW);

  // Enter power down state with ADC and BOD module disabled.
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);


  // <<< return from sleep here >>>


  // Disable external interrupt.
  detachInterrupt(0);


  // Check why we woke up.
  if (interupted)
  {
    interupted = false;
    Serial.println("Woken by interrupt");
  }
  else
  {
    Serial.println("Woken after 8 seconds");

    // Check if 24 hours passed, otherwise go back to sleep.  To do.
  }



  // Do something here
  Serial.println("Doing stuff");
  delay(3000);

}