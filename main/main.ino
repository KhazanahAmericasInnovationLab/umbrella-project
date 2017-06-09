#include <SevenSeg.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include "config.h"

RF24 radio(7, 8); // uno

void setup() {

  Serial.begin(9600);
  ring.begin(); //init ring
  ring.show(); //show nothing
  last_pulse = millis(); //set to now
  last_id_send = millis(); //set to now

  //RF24 STUFF

  radio.begin(); //init the radio
  //radio.setDataRate(RF24_250KBPS);
  //radio.setPALevel(RF24_PA_MIN);
  radio.setRetries(5, 5); //try to send 100 times with 0 delay.  This doesn't seem to affect things too much.
  radio.setChannel(channel); //set the channel
  printf_begin();

  for (int i = (int)FIRST_ID; i < MAX_UMBRELLAS; i++) {
    rgb_data[i].ID = (char) i;
    rgb_data[i].RGB.R = 0;
    rgb_data[i].RGB.G = 0;
    rgb_data[i].RGB.B = 0;
  }

  for (int i = 0; i < RING_LEDS; i++) {
    ring_colors[i] = mypacket.RGB;
  }
}

void setReadMode() { //start listening and open our reading pipe
  radio.openReadingPipe(1, pipes[1]);
  radio.startListening();
}

void setWriteMode() { //stop listening so we can talk.  Open our writing pipe
  radio.stopListening();
  radio.openWritingPipe(pipes[1]);
}


char captureID() {
  Serial.println("TRYING");
  if (DEBUG_MODE) radio.printDetails();
  int totals[MAX_UMBRELLAS]; //Array to count instances of each ID
  int past_test[MAX_UMBRELLAS]; //Past test to compare our current one to
  int current_test[MAX_UMBRELLAS]; //The current test

  for (int i = (int)FIRST_ID; i < MAX_UMBRELLAS; i++) {
    doBackgroundStuff(); //Running this as much as possible.  see definition.
    totals[i] = 0; //Fill the array with 0s
    past_test[i] = 0;
    current_test[i] = 0;
  }

  for (int i = 0; i < NUMBER_OF_TESTS; i++) { //How many "tests" to run.  Average is taken of all the tests
    String sample = ""; //empty string
    float t = millis(); //keep track of when we started the test

    while (millis() - t < 1000.0 * TEST_LENGTH) { //while the time doesn't exceed our defined test length
      doBackgroundStuff(); //see definition
      packet p;
      setReadMode();
      radio.read(&p, sizeof(packet)); //Write to the address of our character
      sample.concat(p.ID); //Add on the character to our string
      String rgb_string = "";
      rgb_data[(int)p.ID] = p;
    }
    for (int j = 0; j < sample.length(); j++) { //Now we "sort" our string of IDS
      doBackgroundStuff(); //see definition
      int value = (int) sample.charAt(j); //get binary value of the ID.  We will use this as the index.  Basically a hash table where casting is our hash function
      if (value <= MAX_UMBRELLAS) {
        current_test[value]++; //increment the corresponding slot.
      }
    }
    for (int j = (int)FIRST_ID; j < MAX_UMBRELLAS; j++) {
      doBackgroundStuff();
      float difference = current_test[j] - past_test[j];
      if (fabs(difference) > OUTLIER_CONSTANT * NUMBER_OF_TESTS) {
        //DEAL WITH THE OUTLIER
        if (DEBUG_MODE) {
          Serial.print("CHANGING FROM ");
          Serial.print(current_test[j]);
        }
        current_test[j] -= difference / 2; //fix the outlier by subtracting the outlier
        if (DEBUG_MODE) {
          Serial.print(" TO ");
          Serial.print(current_test[j]);
          Serial.print(" WHICH IS CLOSER TO ");
          Serial.println(past_test[j]);
        }
      }
      totals[j] += current_test[j];
      past_test[j] = current_test[j];
      current_test[j] = 0;
    }

  }

  //we now know approx how far away everything is (RELATIVELY: AMOUNT OF DATA RECEIVED MIGHT CHANGE WHEN WE ADD MORE NODES)
  //we can do whatever we want with this data, but for now we'll just find the closest umbrella

  float highest = 0; //get the highest element in this array we built
  int indexOfHighest = 0;
  for (int i = (int)FIRST_ID; i < MAX_UMBRELLAS; i++) {
    doBackgroundStuff();
    if (totals[i] > highest) {
      highest = totals[i];
      indexOfHighest = i;
    }
  }
  highest /= NUMBER_OF_TESTS; //average out
  Serial.println(highest); //for debugging
  Serial.print("COLOR IS ");
  Serial.print(mypacket.RGB.R);
  Serial.print(" ");
  Serial.print(mypacket.RGB.G);
  Serial.print(" ");
  Serial.println(mypacket.RGB.B);

  //gets sent to setLight()
  //this is hard coded in.  Would be nicer to have as global variables
  //Or even callibration depending on the total amount of signals we receive

  if (highest < MEDIUM_SIGNAL_AMOUNT) return 'F';
  if (highest < CLOSE_SIGNAL_AMOUNT) return 'M';
  addColor(indexOfHighest);
  return 'C';
}

void addColor(int indexOfHighest) {
  ring_colors[filledLEDS] = rgb_data[indexOfHighest].RGB;
  ring_colors[filledLEDS + 1] = rgb_data[indexOfHighest].RGB;
  ring_colors[filledLEDS + 2] = rgb_data[indexOfHighest].RGB;
  ring_colors[filledLEDS + 3] = rgb_data[indexOfHighest].RGB;
  filledLEDS += 4;
  if (filledLEDS >= RING_LEDS) filledLEDS = 0;
}

void mixWithNeighbour(int index) { //mixes the unit's color with the nearest unit's color 50/50
  doBackgroundStuff();
  mypacket.RGB = mixColors(rgb_data[index].RGB, mypacket.RGB);
}

color mixColors (color c1, color c2) { //simple color mixing algo
  color temp = c1;
  int rd = c1.R - c2.R;
  int gd = c1.G - c2.G;
  int bd = c1.B - c2.B;
  temp.R -= rd / 2;
  temp.G -= gd / 2;
  temp.B -= bd / 2;
  return temp;
}

String RGBFormat(int input) { //makes sure it's 3 chars long
  doBackgroundStuff();
  if (input / 100 > 0) return String(input);
  if (input / 10 > 0) {
    String temp = "0";
    temp.concat(String(input));
    return temp;
  }
  String temp = "00";
  temp.concat(String(input));
  return temp;
}

void setLight(char level) { //pretty straightforward.  Just setting the color/intensity for close/medium/far
  switch (level) {
    case 'F':
      max_intensity = FAR_INTENSITY;
      if (LED_MODE) { //only if on LED mode
        digitalWrite(redPin, HIGH);
        digitalWrite(yellowPin, LOW);
        digitalWrite(greenPin, LOW);
      }
      break;
    case 'M':
      max_intensity = MEDIUM_INTENSITY;
      if (LED_MODE) {
        digitalWrite(redPin, LOW);
        digitalWrite(yellowPin, HIGH);
        digitalWrite(greenPin, LOW);
      }
      break;
    case 'C':
      max_intensity = CLOSE_INTENSITY;
      if (LED_MODE) {
        digitalWrite(redPin, LOW);
        digitalWrite(yellowPin, LOW);
        digitalWrite(greenPin, HIGH);
      }
      break;
    case 'R': //R for really close
      if (LED_MODE) {
        digitalWrite(redPin, HIGH);
        digitalWrite(yellowPin, HIGH);
        digitalWrite(greenPin, HIGH);
      }
      break;
  }
}

void doBackgroundStuff() {
  //This method is called as much as possible since we don't have multi-threading
  //HOWEVER, since we track the time using millis(), and since we have defined the incrememnt of how often we should send IDs and update the light,
  //the functions only actually enter the block IF the defined incrememnt of time has occured.

  //so basically, we're spamming these functions everywhere but they don't do anything unless it's time for them to.
  sendPacket();
  updateLight();
}

void sendPacket() {
  if ((millis() - last_id_send) > id_delta) { //Logic for timing things right.
    setWriteMode(); //get into write mode
    packet out = mypacket;
    color c1 = mixColors(ring_colors[0], ring_colors[4]);
    color c2 = mixColors(ring_colors[8], ring_colors[12]);
    out.RGB = mixColors(c1, c2); //mix 4 colors
    radio.write(&out, sizeof(packet)); //send out our ID
    last_id_send = millis(); //track when we sent it
  }
}
void updateLight() {
  if ((millis() - last_pulse) > ((1 / max_intensity) * 1000 * PULSE_LENGTH / 2)) { //same logic as sendID()
    //Simple logic to linearly fade in and out.
    if (up && intensity < max_intensity) {
      intensity++;
    } else if (up && intensity >= max_intensity) {
      intensity--;
      up = false;
    } else if (!up && intensity > 0) {
      intensity--;
    } else {
      intensity++;
      up = true;
    }
    last_pulse = millis(); //track this too
  }
  for (int i = 0; i < 16; i++) {
    ring.setPixelColor(i, intensity / 100 * ring_colors[i].R, intensity / 100 * ring_colors[i].G, intensity / 100 * ring_colors[i].B);
  }
  ring.show(); //update our changes
  //Serial.print("INTENSITY IS ");
  //Serial.println(intensity);
}

void loop() {
  setLight(captureID()); //run this over and over
}
