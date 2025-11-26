int crossCount = 0; const int maxCount = 5;
#define LEDPIN 13

int sensorState = 0, lastState = 0;       // variable for reading the pushbutton status
//Circular Kinematics Variables
unsigned long startTime = 0;
unsigned long deltaT;
float theta = 3.14159 * 2;
float omega = 0.0;
float rps = 0.0;



#define SENSORPIN A0//10

void IRSetup()
{
  pinMode(LEDPIN, OUTPUT);
  pinMode(SENSORPIN, INPUT_PULLUP); // this approach works for ESP32 (especially) and also for 32u4
}

void IRProcessCrossing()
{


  sensorState = digitalRead(SENSORPIN);

  if (sensorState == LOW) {
    // turn LED on:
    digitalWrite(LEDPIN, HIGH);
  }
  else {
    // turn LED off:
    digitalWrite(LEDPIN, LOW);
  }

  if (sensorState && !lastState) {
    Serial.print("Met: "); Serial.println(crossCount);
    canvas.setCursor(200, 20);
    canvas.setTextColor(ST77XX_BLACK);
    canvas.println((String)(crossCount));
    canvas.setCursor(200, 20);
    canvas.setTextColor(ST77XX_MAGENTA);
    crossCount++;
    canvas.println((String)(crossCount));
    canvas.setTextColor(ST77XX_WHITE);
  }
  if (!sensorState && lastState) {
    //    Serial.println("Broken: ");

  }
  lastState = sensorState;
}

void calcRPS() {
  deltaT = millis() - startTime;
  startTime = millis();
  rps = crossCount * 1000.0 / deltaT;
  omega = theta * rps;
  Serial.print("RPS: "); Serial.print(rps); Serial.print(" Omg: "); Serial.println(omega);
  crossCount = 0;
  updateOmg = true;
}

void printOmegaPeriodically()
{
  if (crossCount == maxCount)
  {
    calcRPS();
  }
}
String getStringOmgRPS() {
  return "Rad/s: " + (String)omega;
}
