/*
   
heinrich.diesinger@cnrs.fr, F4HYZ
Keyfob that publishes a command on mqtt server and then turns itself off using the autopowerhold mechanism involving S09 converter with enable input.
EEPROM version that has no hardcoded settings, but is user configurable from the USB-UART, 9k6, 8N1
 

*/


#include <EEPROM.h>           // eeprom

#include <ESP8266WiFi.h>      // libraries for wifi 
#include <PubSubClient.h>     // and MQTT client

// WiFi stuff:
char wifi_mode[4] = "sta"; //ap, sta, off
char ssid[30] = "myssid";
char password[30] = "mypasswd";
boolean connectstate = false;


// powerlatch and payload deployment
#define POWERHOLD D1
#define LED D4


// eeprom stuff
//#define nvlength 128
const long nvlength = 512;
char shadoweeprom[nvlength];
char shadowshadow[nvlength]; // backup copy unaltered by strtoktoken
char checker[5] = "";
char *strtokIndx;


//mqtt stuff
char chartargetip[16] = "192.168.1.1"; // 192.168.4.1 for the local machine
char shadowchartargetip[16];
byte targetip[4] = {192, 168, 1, 1}; // target ip for both mqtt and telnet clients
char chartargetport[6] = "1883"; //23 for telnet, 1883 for mqtt
long targetport = 1883; // or 7777 ? for telnet only ??? or also mqtt
//#define MQTT_SERVER     "192.168.4.1"
//#define MQTT_PORT       1883
#define MQTT_USERNAME   ""
#define MQTT_KEY        ""
//#define MQTT_INTOPIC    "lora/rfout"
//#define MQTT_OUTTOPIC   "lora/rfin"
char mqttouttopic[128] =  "misc";
char mqttmessage[128] =  "toggle";


// battery management
int battlevel;
float voltf;
float warnthreshold = 3.3;
float protectthreshold = 2.9;
unsigned long batmeasperiod = 30000; //check battery all 30 seconds
unsigned long lastmeasured = 0;


// variables for the serial interrupt handler:
const byte numchars = 128;
//char inputcharray[numchars];
//char *inputccptr = inputcharray; // LoRa input
char message[numchars];  // all input be it LoRa or local
char *messptr = message;
char outmessage[numchars]; // another array
char *outmessptr = outmessage;
boolean newdata = false;  // whether the reception is complete
int ndx; // this is used both in LoRa and serial reception, seems to work
//char receivedchars[numchars]; //serial data input
//char *rccptr = receivedchars;
unsigned long lastchar;
char rc;


// and for data to charray conversion etc
char interimcharray[10]; // and in battery measurement
size_t cur_len;


// declare mqtt client
WiFiClient espClient;
PubSubClient client(espClient);



// send by mqtt
void mqttpublish() {
if (client.connected()) { // if not, no need to check the rest...
client.publish(mqttouttopic, mqttmessage);
Serial.println("publishing on mqtt: ");
Serial.println(mqttmessage);
}
}
    
   


void helpscreen() {
 Serial.println();
 Serial.println("MQTT keyfob with EEPROM storage, user configurable by UART serial");
 Serial.println("By heinrich.diesinger@cnrs.fr, F4HYZ");
 Serial.println("Battery < 3.3 V - 2 LED flashes, < 2.9 V - 6 flashes and cancel");
 Serial.println("Synatx: command [argument]\\n , no argument queries present value");
 Serial.println("ssid: your router or access point <myssid>");  
 Serial.println("password: password for above ssid <mypassword>");
 Serial.println("targetip:  MQTT server ip <192.168.1.1>");
 Serial.println("targetport: MQTT server port number <1883>");
 Serial.println("outtopic: MQTT topic to publish to <misc>");
 Serial.println("message: MQTT message to publish <toggle>");
 Serial.println("eepromstore: saves settings to EEPROM (must be called manually)");
 Serial.println("help: displays this info again");
 delay(1000);
 Serial.println("Service commands, expert mode: ");
 Serial.println("eepromretrieve: reads settings from EEPROM");
 Serial.println("eepromdelete: clears EEPROM manually");
 Serial.println("batlevel: shows the supply voltage");
 Serial.println("setupwifi: restarts wifi initialization");
 Serial.println("mqttconnect: initialize MQTT connection");
 Serial.println("mqttpublish: manually trigger mqtt publishing");
 Serial.println("reboot: restarts the MCU");
 Serial.println();
}



void eepromstore() //concatenates all setup data in a csv to the shadoweeprom, writes 1 by 1 into eeprom, and commit()
{
  strcpy(shadoweeprom, "99,");
  strcat(shadoweeprom, ssid);
  strcat(shadoweeprom, ",");
  strcat(shadoweeprom, password);
  strcat(shadoweeprom, ",");
   strcat(shadoweeprom, chartargetip);
  strcat(shadoweeprom, ",");
  strcat(shadoweeprom, chartargetport);
  strcat(shadoweeprom, ",");
  strcat(shadoweeprom, mqttouttopic);
  strcat(shadoweeprom, ",");
  strcat(shadoweeprom, mqttmessage);
  strcat(shadoweeprom, ",");
   
  strcpy(outmessage, "shadoweeprom is: "); strcat(outmessage, shadoweeprom);
  Serial.println(outmessage);

  for (int i = 0; i < nvlength; ++i)
  {
    EEPROM.write(i, shadoweeprom[i]);
    if (shadoweeprom[i] = '\0') break; // dont write more than necesary :)))
  }

  if (EEPROM.commit()) strcpy(outmessage, "settings successfully stored to EEPROM");
    else strcpy(outmessage, "writing to EEPROM failed");
    
 Serial.println(outmessage);

}





void eepromretrieve() // reads all eeprom into shadoweeprom 1 by 1, split into token by strtok to charfreq etc, converts by atoi
{
  // write eeprom into shadow eeprom buffer
  char eeprchar;
  for (int i = 0; i < nvlength; i++) {
    eeprchar = char(EEPROM.read(i));
    shadoweeprom[i] = eeprchar;
    if (eeprchar = '\0') break;
  }

  // since strtok destroys the charray, make a copy of it
  strcpy(shadowshadow, shadoweeprom);

  // decompose it into the char versions of the parameters
  strtokIndx = strtok(shadoweeprom, ",");
  if (strtokIndx != NULL) strncpy(checker, strtokIndx, 2); // add checking mechanism if eeprom has ever been written,
  // and if not, return from the fct to use hardcoded defaults
  else strcpy(checker, "");

  if (strstr(checker, "99")) {  // only if checking is ok, continue decomposing
    strtokIndx = strtok(NULL, ",");
    if (strtokIndx != NULL) strcpy(ssid, strtokIndx);
    else strcpy(ssid, ""); // this to clear the argument from the last message, if there is not a new one
    strtokIndx = strtok(NULL, ",");
    if (strtokIndx != NULL) strcpy(password, strtokIndx);
    else strcpy(password, "");
    strtokIndx = strtok(NULL, ",");
    if (strtokIndx != NULL) strcpy(chartargetip, strtokIndx);
    else strcpy(chartargetip, "");
    strtokIndx = strtok(NULL, ",");
    if (strtokIndx != NULL) strcpy(chartargetport, strtokIndx);
    else strcpy(chartargetport, "");
    strtokIndx = strtok(NULL, ",");
    if (strtokIndx != NULL) strcpy(mqttouttopic, strtokIndx);
    else strcpy(mqttouttopic, "");
    strtokIndx = strtok(NULL, ",");
    if (strtokIndx != NULL) strcpy(mqttmessage, strtokIndx);
    else strcpy(mqttouttopic, "");
    
    // and further convert it into non-char format with atoi() functions and the like:

    // chartargetip: further decompose into byte array; the separator is now a dot !
    strcpy(shadowchartargetip, chartargetip); // it will be destroyed
    strtokIndx = strtok(shadowchartargetip, ".");
    for (int i = 0; i <= 3; i++) {
      if (strtokIndx != NULL) {
        targetip[i] = atoi(strtokIndx);
      }
      //if (strtokIndx != NULL) strcpy(targetip[i], atoi(strtokIndx)); // add checking mechanism if eeprom has ever been written,
      // and if not, return from the fct to use hardcoded defaults
      //else strcpy(targetip[i], ""); //doesnt work, empty is not a byte
      strtokIndx = strtok(NULL, ".");
    }

    // port number (easy)
    targetport = atoi(chartargetport);


    // reset the checksum
    strcpy(checker, ""); //resetting the checking mechanism

    strcpy(outmessage, "EEPROM content: \r\n");
    strcat(outmessage, shadowshadow);
   }
  else // if checker not == 99
    strcpy(outmessage, "EEPROM empty, using factory defaults");
    
    Serial.println(outmessage);
}  // end eepromretrieve



void eepromdelete()
{
  for (int i = 0; i < nvlength; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  delay(500);
  strcpy(outmessage, "EEPROM successfully deleted");
  Serial.println(outmessage);
}



void reboot()
{
  ESP.restart();
  // ESP.reset(); // less than restart ! can leave some of the registers in the old state
}


// functions to set parameters from the console input:

void setssid() {
  if (strstr(outmessage, "ssid ")) {
    strtokIndx = outmessptr + 5;
    if (strlen(strtokIndx) <= 30) {
      strcpy(ssid, strtokIndx);
      strcpy(outmessage, "ssid set to: ");
      strcat(outmessage, ssid);
    } else {
      strcpy(outmessage, "error: argument too long");
    }
  } else {
    strcpy(outmessage, ssid);
  }
  Serial.println(outmessage);
}

void setpassword() {
  if (strstr(outmessage, "password ")) {
    strtokIndx = outmessptr + 9;
    if (strlen(strtokIndx) <= 30) {
      strcpy(password, strtokIndx);
      strcpy(outmessage, "password set to: ");
      strcat(outmessage, password);
    } else {
      strcpy(outmessage, "error: argument too long");
    }
  } else {
    strcpy(outmessage, password);
  }
  Serial.println(outmessage);
}


void settargetip() {
  if (strstr(outmessage, "targetip ")) {
    strtokIndx = outmessptr + 9;
    if (strlen(strtokIndx) <= 16) {
      strcpy(chartargetip, strtokIndx);
      strcpy(outmessage, "target ip set to: ");
      strcat(outmessage, chartargetip);
      
    // targetip from char version: further decompose into byte array; the separator is now a dot !
    strcpy(shadowchartargetip, chartargetip); // it will be destroyed
    strtokIndx = strtok(shadowchartargetip, ".");
    for (int i = 0; i <= 3; i++) {
      if (strtokIndx != NULL) {
        targetip[i] = atoi(strtokIndx);
      }
      //if (strtokIndx != NULL) strcpy(targetip[i], atoi(strtokIndx)); // add checking mechanism if eeprom has ever been written,
      // and if not, return from the fct to use hardcoded defaults
      //else strcpy(targetip[i], ""); //doesnt work, empty is not a byte
      strtokIndx = strtok(NULL, ".");
    }
    } else {
      strcpy(outmessage, "error: argument too long");
    }
  } else {
    strcpy(outmessage, chartargetip);
  }
  Serial.println(outmessage);
}



void settargetport() {
if (strstr(outmessage, "targetport ")) {
    strtokIndx = outmessptr + 11;
    if (strlen(strtokIndx) <= 6) {
      strcpy(chartargetport, strtokIndx);
      strcpy(outmessage, "target port set to: ");
      strcat(outmessage, chartargetport);
      // port number from char version (easy)
      targetport = atoi(chartargetport);
    } else {
      strcpy(outmessage, "error: argument too long");
    }
  } else {
    strcpy(outmessage, chartargetport);
  }
  Serial.println(outmessage);
}



void setouttopic() {
  if (strstr(outmessage, "outtopic ")) {
    strtokIndx = outmessptr + 9;
    if (strlen(strtokIndx) <= 128) {
      strcpy(mqttouttopic, strtokIndx);
      strcpy(outmessage, "MQTT outtopic set to: ");
      strcat(outmessage, mqttouttopic);
    
    } else {
      strcpy(outmessage, "error: argument too long");
    }
  } else {
    strcpy(outmessage, mqttouttopic);
  }
  Serial.println(outmessage);
}


void setmessage() {
  if (strstr(outmessage, "message ")) {
    strtokIndx = outmessptr + 8;
    if (strlen(strtokIndx) <= 128) {
      strcpy(mqttmessage, strtokIndx);
      strcpy(outmessage, "MQTT message set to: ");
      strcat(outmessage, mqttmessage);
      
    } else {
      strcpy(outmessage, "error: argument too long");
    }
  } else {
    strcpy(outmessage, mqttmessage);
  }
  Serial.println(outmessage);
}





void batmeasure()
{
  battlevel = analogRead(A0);
  voltf = battlevel * (6.2 / 1024.0); // (300k + 220k + 100k)/100k
}


void batlevel()
{
  batmeasure();
  dtostrf(voltf, 0, 2, interimcharray);
  strcpy(outmessage, "The voltage is ");
  strcat(outmessage, interimcharray);
  strcat(outmessage, " V");
  Serial.println(outmessage);
}



void suspend()
{
  strcpy(outmessage, "Suspending system power - only works if hardware supported e.g. by keyfob shield");
  Serial.println(outmessage);
  delay(1000); // wait a while that the LoRa message goes out
  digitalWrite(POWERHOLD, LOW);
}



void batperiodically()
{
  batmeasure();
  if (voltf < warnthreshold)
  {
    // issue a battery warning
    dtostrf(voltf, 0, 2, interimcharray);
    strcpy(outmessage, "Battery critically low, ");
    strcat(outmessage, interimcharray);
    strcat(outmessage, " V");
    Serial.println(outmessage); // makes sense for the keyfob because if usb powered and no batt there it is always low
    digitalWrite(LED, HIGH); //off
    delay(500);
    digitalWrite(LED, LOW); //on
    delay(500);
    digitalWrite(LED, HIGH); //off
    delay(500);
    digitalWrite(LED, LOW); //on
    // flash LED twice and continue...
    if (voltf < protectthreshold)
    {
    // flash LED 4x more...
    delay(500);
    digitalWrite(LED, HIGH); //off
    delay(500);
    digitalWrite(LED, LOW); //on
    delay(500);
    digitalWrite(LED, HIGH); //off
    delay(500);
    digitalWrite(LED, LOW); //on
    delay(500);
    digitalWrite(LED, HIGH); //off
    delay(500);
    digitalWrite(LED, LOW); //on
    delay(500);
    digitalWrite(LED, HIGH); //off
    delay(500);
    digitalWrite(LED, LOW); //on
    suspend();
    }
  }
}



boolean setup_wifi() {
  delay(100);
  // here can use Serial.print() always instead publish() because during wifi setup, neither LoRa nor telnet or mqtt is initialized !!
  Serial.print("Connecting, SSID "); Serial.print(ssid); Serial.print(" as "); Serial.println(wifi_mode);
       
    // network cient - inital connect
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);  // to keep the WiFi connection alive always, needed for telnet client
    WiFi.begin(ssid, password);
    unsigned long prevtime = millis();  // comment this to force wifi, preventing prog to go in stealth
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
      unsigned long currentime = millis();  // comment the 6 lines to force wifi connection, preventing prog to go in stealth
      if (currentime - prevtime >= 30000)
      {
        Serial.println("WiFi unavailable");
        return false;
      }
    }
    randomSeed(micros());

    Serial.print("IP address: "); Serial.println(WiFi.localIP());
    return true;
 } // end setup_wifi()



void setupwifi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  connectstate = setup_wifi();
  // wifi_station_connect(); from weatherstation, needed in setup after deepsleep
  //strcpy(outmessage, "done");
  Serial.println("done");
}



  void establish_mqtt() {
  //here use publish() because during mqtt setup, telnet is already initialized !!
  client.setServer(targetip, targetport);
  yield();
  if (!client.connected()) {
    strcpy(outmessage, "Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("", MQTT_USERNAME, MQTT_KEY))
    {
    strcat(outmessage, "connected");
    // client.subscribe(mqttintopic); // not for keyfob !
    }

    else {
      strcat(outmessage, "failed, rc=");
      strcat(outmessage, (char*)client.state());
      //strcat(outmessage, ", try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    Serial.println(outmessage);
  }
  // client.setCallback(callback); not for keyfob
  }




void cmdparse() {
  
  if (strstr(outmessage, "help") == outmessptr) {
    helpscreen();
  }
   else if (strstr(outmessage, "batlevel") == outmessptr) {
    batlevel();
  }
  else if (strstr(outmessage, "shutdown") == outmessptr) {
    suspend();
  }
  else if (strstr(outmessage, "eepromretrieve") == outmessptr) {
    eepromretrieve();
  }
  else if (strstr(outmessage, "eepromdelete") == outmessptr) {
    eepromdelete();
  }
  else if (strstr(outmessage, "eepromstore") == outmessptr) {
    eepromstore();
  }
  else if (strstr(outmessage, "reboot") == outmessptr) {
    reboot();
  }
   else if (strstr(outmessage, "ssid") == outmessptr) {
    setssid();
  }
  else if (strstr(outmessage, "password") == outmessptr) {
    setpassword();
  }
 else if (strstr(outmessage, "targetip") == outmessptr) {
    settargetip();
  }
  else if (strstr(outmessage, "targetport") == outmessptr) {
    settargetport();
  }
    else if (strstr(outmessage, "outtopic") == outmessptr) {
    setouttopic();
  }
     else if (strstr(outmessage, "message") == outmessptr) {
    setmessage();
  }
    else if (strstr(outmessage, "setupwifi") == outmessptr) {
    setupwifi();
  }
     else if (strstr(outmessage, "mqttconnect") == outmessptr) {
    establish_mqtt();
  }
     else if (strstr(outmessage, "mqttpublish") == outmessptr) {
    mqttpublish();
  }
 }  // end cmdparse()






void setup() {

pinMode(POWERHOLD, OUTPUT); // configure and set the POWERHOLD output
pinMode(LED, OUTPUT); // configure and set the LED output
digitalWrite(POWERHOLD, HIGH);
digitalWrite(LED, LOW); //on

EEPROM.begin(nvlength);  // eeprom

Serial.begin(9600);                   // initialize serial

// retrieve EEPROM data
eepromretrieve();

connectstate = setup_wifi();           // attempt connection to wifi:
// insert force no sleep from the telnet client example:
//while (!Serial);
delay(1000);

establish_mqtt();

// now that we have drawn a bit current, check the battery:
batperiodically();

mqttpublish(); // send the command

delay(1000); //let mqtt buffer go out although probably this is not necessary because the function blocks the loop :)))

suspend(); // if battery powered with the keyfob shield, it ends here; 
           // if usb powered, we will enter the main loop from where we can enter the settings

delay(1000); // let the capacitor discharge ...

helpscreen();  // now if we are usb powered, it shows the help and enters the main loop; 
              
} // end setup





void loop() {

/* all this can do manually with troubleshooting commands

// take care of the connections

 if (!connectstate) connectstate = setup_wifi(); // if wrong passwd it keeps retrying and impossible to enter credentials

if (connectstate){  // if conneted to wifi
// maintain connection; 
     establish_mqtt(); // not publish();
//client.loop(); subscriber only ?
  }
*/  

 
  // input routines

  // handle incoming data from serial
  while (Serial.available() && newdata == false) {
    lastchar = millis();
    rc = Serial.read();
    if (rc != '\n') {
      //if (rc != '\r')  //suppress carriage return !
      { message[ndx] = rc;
        ndx++;
      }
      if (ndx >= numchars) {
        ndx = numchars - 1;
      }
    }
    else {
      message[ndx] = '\0';
      ndx = 0;
      newdata = true;
     }
  }
  // put the 2s timeout in case the android terminal cannot terminate
  if (ndx != 0 && millis() - lastchar > 2000) {
    message[ndx] = '\0';
    ndx = 0;
    newdata = true;
   }


// if something received, launch the command parser
if (newdata) {
  strcpy(outmessage, message); // because here was no stripping off of AT+ or other prefix
  cmdparse(); // proceed to command parsing
    newdata = false;
    }

 
} //end main loop
