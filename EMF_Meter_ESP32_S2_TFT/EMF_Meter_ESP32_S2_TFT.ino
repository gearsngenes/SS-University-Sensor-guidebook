#include <Adafruit_ADS1X15.h>
/**
 * Gain and bit-multiplier table for the ADS1x15
 * 
 * Gain | Bit-mul 1115 | Bit-mul 1015 | Range (V)
 * -------------------------------------------
 * 2/3  | 0.1875 mV    | 3 mV         | +-6.144
 * 1    | 0.125 mV     | 2 mV         | +-4.096
 * 2    | 0.0625 mV    | 1 mV         | +-2.048
 * 4    | 0.03125 mV   | 0.5 mV       | +-1.024
 * 8    | 0.015625 mV  | 0.25 mV      | +-0.512
 * 16   | 0.0078125 mV | 0.125 mV     | +-0.256 
 * 
 * If 9.0V battery (full) connected to the motor, at max speed, approx +/- 220-250mV is induced 
 * in the coil.
 * 
 */

const float MUL_TWO_THIRDS_ADS1115 = 0.1875;
const float MUL_TWO_THIRDS_ADS1015 = 3;
const float MUL_FOUR_ADS1115 = 0.03125;
const float MUL_FOUR_ADS1015 = 0.5;
float multiplier;

int t_delay = 10;
float max_volt_mag = 300;


/*
Depending on the specific ADS chip you are using, comment
out the one you are not. ADS 1115 is 16-bit, 1015 is 12-bit.
*/
Adafruit_ADS1115 ads1115;
//Adafruit_ADS1015 ads1015;


void setup() {
  Serial.begin(115200);
  /*set up the TFT*/
  TFT_Setup();
  Serial.println("TFT Set up Complete.");
  /*set up the ADS*/
  ads1115.setGain(GAIN_TWOTHIRDS);  // you can use any gain as long as the right multiplier is used
  multiplier = MUL_TWO_THIRDS_ADS1115;// use appropriate multiplier
  ads1115.begin();
  Serial.println("ADS Set up Complete.");
}

/**
 * The functions and variables related to updating the minimum
 * and maximum value readings of the EMF. The variables are:
 * 
 * min_voltage = minimum reading voltage
 * max_voltage = maximum reading voltage
 * update_count = number of readings the EMF Meter makes before
 *                reseting the minimum and maximum voltage vals
 *                to zero
 * counter = current number of points read
 */
float min_voltage = 0;
float max_voltage = 0;
const int update_count = 80; //240 pixels / 3 pixels per datapoint
int counter = 0;

void update_min_max(float v){
  if (v < min_voltage) { /*update minimum if necessary*/
    min_voltage = v;
  }
  if (v > max_voltage) { /*update maximum if necessary*/
    max_voltage = v;
  }
  counter ++; /*increment counter*/
  if (counter > update_count) { /*if we counted more than 100 points, reset*/
    max_voltage = 0;
    min_voltage = 0;
    counter = 0;
  }
}


void loop() {
  /*retrieve EMF in mV*/
  int16_t diff = ads1115.readADC_Differential_0_1();
  float voltage = diff * multiplier;
  /*update min-maxes*/
  update_min_max(voltage);
  /*plot the data*/
  plotData_chan1(voltage, min_voltage, max_voltage);
  /*wait 10 ms*/
  delay(t_delay);
}
