#include <Arduino.h>

int out, gaugeArray[25] = {60,105,130,145,165,171,176,182,188,193,198,202,207,211,215,218,221,224,227,230,232,235,238,240,242};
float sensor;

void setup(){
  
}

void loop(){
  sensor = analogRead(2) * 5 / 1023.0 * 35 - 16.5;
  out = floor(sensor/5);
  analogWrite(0, gaugeArray[out]);
}
