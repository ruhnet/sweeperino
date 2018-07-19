
#include <Wire.h>
#include <SPI.h>
#include <Si570.h>
#include <si5351.h>
#include <LiquidCrystal.h>
#include <Rotary.h>

/* 
//Use these 2 lines if you want to use a little OLED display.
#include <ssd1306_tiny.h> 
ssd1306_tiny display; 
*/


Rotary r = Rotary(3,2); // sets the pins the rotary encoder uses.  Must be interrupt pins.

LiquidCrystal lcd(12,11, 7,8,9,10);

long stepInterval = 1000;
uint8_t buttonPressed = 1;

Si5351 si5351;
Si570 *si570=NULL;
#define SI570_I2C_ADDRESS 0x55
char printBuff[20];

#define LOG_AMP A3
//#define WB_POWER_CALIBERATION (-112) //Set your power calibration here
#define WB_POWER_CALIBERATION (-92)
#define BUTTON 4
#define BUTTON2 5 //future use maybe

int  dbm_reading = 100;
int  mw_reading = 0;
int power_caliberation = WB_POWER_CALIBERATION;


char serial_in[32], c[30], b[30];
unsigned char serial_in_count = 0;

long frequency, fromFrequency=14150000, toFrequency=30000000, stepSize=100000;

#define TUNING  A2
int tune, previous = 500;
int count = 0;
int  i, pulse;
unsigned long baseTune = 14100000;
boolean sweepBusy = false;



void setup()
{  
  PCICR |= (1 << PCIE2); //Setup Interrupt Handling
  PCMSK2 |= (1 << PCINT18) | (1 << PCINT19);
  sei();

  pinMode(BUTTON,INPUT_PULLUP); //set button input to pullup mode

  lcd.begin(16, 2);
  printBuff[0] = 0;
  printLine1("[RuhNet RF Labs]"); //Startup message
  printLine2("Sweeperino v0.03");  
  
  delay(2000);

  
  // Start serial and initialize the Si5351
  Serial.begin(9600);
  analogReference(DEFAULT);

  Serial.println("*Sweeperino v0.03\n");
  Serial.println("*Testing for Si570\n");

  si570 = new Si570(SI570_I2C_ADDRESS, 56320000);
  if (si570->status == SI570_ERROR) {
    printLine1("Si570 not found. ");
    Serial.println("*Si570 Not found\n");   
    si570 = NULL;
    
    Serial.println("*Si5350 ON");       
    printLine2("Si5351 Enabled! ");   
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
    delay(1000);
    lcd.clear();
  }
  else {
    Serial.println("*Si570 ON");
     printLine2("Si570 ON");    
  }

  setFrequency(14100000); //14.1MHz initial frequency
  previous  = analogRead(TUNING)/2;
}




/* display routines */
void printLine1(char *c){
  if (strcmp(c, printBuff)){
    lcd.setCursor(0, 0);
    lcd.print(c);
    strcpy(printBuff, c);
    count++;
  }
}

void printLine2(char *c){
  lcd.setCursor(0, 1);
  lcd.print(c);
}

char *readNumber(char *p, long *number){
  *number = 0;

  sprintf(c, "#%s", p);
  while (*p){
    char c = *p;
    if ('0' <= c && c <= '9')
      *number = (*number * 10) + c - '0';
    else 
      break;
     p++;
  }
  return p;
}

char *skipWhitespace(char *p){
  while (*p && (*p == ' ' || *p == ','))
    p++;
  return p;
} 

/* command 'h' */
void sendStatus(){
  Serial.write("helo v1\n");
  sprintf(c, "from %ld\n", fromFrequency);
  Serial.write(c);
   
  sprintf(c, "to %ld\n", toFrequency);
  Serial.write(c);

  sprintf(c, "step %ld\n", stepSize);
  Serial.write(c);
}

void setFrequency(unsigned long f){
   if (si570 != NULL)
     si570->setFrequency(f);
   else
     si5351.set_freq((f * 100), SI5351_CLK0); 
   frequency = f;
}

/* command 'g' to begin sweep 
  each response begins with an 'r' followed by the frequency and the raw reading from ad8703 via the adc */
void doSweep(){
  unsigned long x;
  int a;
  
  sweepBusy = 1;
  Serial.write("begin\n");
  printLine1("Sweeping...     ");
  for (x = fromFrequency; x < toFrequency; x = x + stepSize){
    setFrequency(x);
    delay(10);
    a = analogRead(LOG_AMP) * 2 + (power_caliberation * 10);
    sprintf(c, "r%ld:%d\n", x, a);
    Serial.write(c);
  }
  Serial.write("end\n");
//  si5351.set_freq(fromFrequency,  0, SI5351_CLK0);
  sweepBusy = 0;
}

/* command 'e' to end sweep */
void endSweep(){
  //to be done
}

void readDetector(){
  int i = analogRead(3);
  sprintf(c, "d%d\n", i);
  Serial.write(c);
}


void parseCommand(char *line){
  char *p = line;
  char command;

  while (*p){
    p = skipWhitespace(p);
    command = *p++;
    
    switch (command){
      case 'f' : //from - start frequency
        p = readNumber(p, &fromFrequency);
        setFrequency(fromFrequency);
        break;
      case 't':
        p = readNumber(p, &toFrequency);
        break;
      case 's':
        p = readNumber(p, &stepSize);     
        break;
      case 'v':
        sendStatus();
        break;
      case 'g':
         sendStatus();
         doSweep();
         break;
      case 'r':
         readDetector();
         break;   
      case 'o':     
      case 'w':
      case 'n':
          break;
      case 'i': /* identifies itself */
        Serial.write("*iSweeperino 2.0\n");
        break;
    }
  } /* end of the while loop */
}

void acceptCommand(){
  int inbyte = 0;
  inbyte = Serial.read();
  
  if (inbyte == '\n'){
    parseCommand(serial_in);    
    serial_in_count = 0;    
    return;
  }
  
  if (serial_in_count < sizeof(serial_in)){
    serial_in[serial_in_count] = inbyte;
    serial_in_count++;
    serial_in[serial_in_count] = 0;
  }
}

void updateDisplay(){
  int j;

  sprintf(b, "%9ld", frequency);
  sprintf(c, "%.3s.%.3s.%3s ",  b, b+3, b+6);
  printLine1(c);  

  //sprintf(c,  "%d.%d dBm ", dbm_reading/10, abs(dbm_reading % 10));
  sprintf(c,  "%d.%d dBm %d.%dmW", dbm_reading/10, abs(dbm_reading % 10), mw_reading/10, abs(mw_reading % 10) );
  printLine2(c);
}

void doReading(){
  int new_reading = analogRead(LOG_AMP) * 2 + (power_caliberation * 10);
  if (abs(new_reading - dbm_reading) > 4){
    dbm_reading = new_reading;
	mw_reading = pow( 10.0, (dbm_reading) / 10.0);

    updateDisplay();
  }
}

void doTuning(){
  setFrequency(baseTune);
  updateDisplay();
  
  
/*
  
  count = 0;
  if (previous != tune){
    setFrequency(baseTune + (10L * (unsigned long)(tune-20)));
    updateDisplay();
    previous = tune;
  }

  */
  
}


//Main program loop:
void loop(){
  if (Serial.available()>0)
    acceptCommand();   
  
  if (!sweepBusy){
    doReading();
    doTuning();
  }
  
  buttonPressed = digitalRead(BUTTON);
  if (buttonPressed) {
    lcd.clear();
    lcd.setCursor(0,0);
    printLine1("Step Size:");
    lcd.setCursor(0,1);
   
    lcd.print(stepInterval, DEC);
    lcd.print(" Hz");
  }

  delay(100);
}


//Interrupt processing.
//This is probably kindof messy, but it works...
ISR(PCINT2_vect) {  
  unsigned char result = r.process();
  if (result == DIR_NONE) {
    // do nothing
  } else if (buttonPressed) {
    if (result == DIR_CW) {
      if (stepInterval < 1000000) {
        stepInterval = stepInterval*10;
      } else stepInterval = 1;
    } else if (result == DIR_CCW) {
      if (stepInterval > 1) {
        stepInterval = stepInterval/10;  
      } else stepInterval = 1000000;
    }
  } 
  else if (result == DIR_CW) {
    baseTune=baseTune+stepInterval;
  }
  else if (result == DIR_CCW) {
    baseTune=baseTune-stepInterval;
  }
}

