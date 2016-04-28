unsigned int i = 0;

void setup() {
  Serial.begin(9600);
}

void loop() {
  char buffer[50];
  sprintf(buffer, "the current value is %d", i++);
  Serial.println(buffer);
}
