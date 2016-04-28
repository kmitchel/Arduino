
void setup() {
  // initialize serial communications at 9600 bps:
  Serial.begin(115200);
}

void loop() {
//  Serial.print("0,330,");
  Serial.println(analogRead(A0)/1024.0*500);
  delay(2);
}
