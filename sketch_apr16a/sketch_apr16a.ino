//
//    FILE: pcf8574_test.ino
//  AUTHOR: Rob Tillaart
//    DATE: 27-08-2013
//
// PUPROSE: demo 
//

#include "PCF8574.h"
#include <Wire.h>

// adjust addresses if needed
PCF8574 PCF_38(0x38);  // add switches to lines  (used as input)
PCF8574 PCF_39(0x39);  // add leds to lines      (used as output)

void setup()
{
  Serial.begin(115200);

}

void loop()
{
  // echos the lines
  PCF_38.write(0, LOW);
  delay(1000);
  PCF_38.write(0, HIGH);
  delay(1000);

}
//
// END OF FILE
//

