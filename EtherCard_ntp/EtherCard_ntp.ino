/*
 * Arduino ENC28J60 EtherCard NTP client demo
 * With extra bits for nanode
 */

#define DEBUG

#include <Time.h>
#include <Timezone.h>
#include <avr/pgmspace.h>

/* Ethernet driver */
#include <EtherCard.h>

/* Display driver */
#include <TM1637Display.h>

#define CLK 3//pins definitions for TM1637 and can be changed to other ports
#define DIO 4
TM1637Display tm1637(CLK,DIO);

#define ETH_CS 10 // CS pin for ethernet module

// Jan 1 
#define SECS_YR_1900_2000  (3155673600UL)

static uint8_t mymac[6] = { 0x54,0x55,0x58,0x10,0x00,0x25};

// IP and netmask allocated by DHCP
static uint8_t myip[4] = { 0,0,0,0 };
static uint8_t mynetmask[4] = { 0,0,0,0 };
static uint8_t gwip[4] = { 0,0,0,0 };
static uint8_t dnsip[4] = { 0,0,0,0 };
static uint8_t dhcpsvrip[4] = { 0,0,0,0 };

static int currentTimeserver = 0;

// Find list of servers at http://support.ntp.org/bin/view/Servers/StratumTwoTimeServers
// Please observe server restrictions with regard to access to these servers.
// This number should match how many ntp time server strings we have
#define NUM_TIMESERVERS 4

// Create an entry for each timeserver to use
const prog_char ntp0[] PROGMEM = "0.us.pool.ntp.org";
const prog_char ntp1[] PROGMEM = "1.us.pool.ntp.org";
const prog_char ntp2[] PROGMEM = "2.us.pool.ntp.org";
const prog_char ntp3[] PROGMEM = "3.us.pool.ntp.org";

// Now define another array in PROGMEM for the above strings
const prog_char *ntpList[] = { ntp0, ntp1, ntp2, ntp3 };
  
// Packet buffer, must be big enough to packet and payload
#define BUFFER_SIZE 550
byte Ethernet::buffer[BUFFER_SIZE];
uint8_t clientPort = 123;

bool colonOn = false;    // last colon status (on/off)
uint32_t lastUpdate = 0; // last request sent to NTP (millis)
time_t prevDisplay = 0;  // last time display was updated (UTC)
time_t timeLong;         // NTP time variable (Seconds since 1/1/1900)
time_t localTime;        // Local time (unix time)

TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  //UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   //UTC - 5 hours
Timezone usEastern(usEDT, usEST);

// Number of seconds between 1-Jan-1900 and 1-Jan-1970, unix time starts 1970
// and ntp time starts 1900.
#define GETTIMEOFDAY_TO_NTP_OFFSET 2208988800UL

void displayTime(){
  uint8_t colon = 0b10000000;
  int tm_hour;
  int tm_min;

  tm_hour = hourFormat12(localTime);
  tm_min = minute(localTime);

  //clear display
  uint8_t data[] = {0x0, 0x0, 0x0, 0x0};
  if (tm_hour > 9){
      data[0] = tm1637.encodeDigit(tm_hour/10);
  }
  data[1] = tm1637.encodeDigit(tm_hour%10);
  data[2] = tm1637.encodeDigit(tm_min/10);
  data[3] = tm1637.encodeDigit(tm_min%10);

  //add colon
  if (colonOn){
      data[1] = data[1] | colon;
  }
  colonOn = !colonOn;

  tm1637.setSegments(data);
}

void setup(){
  tm1637.setBrightness(0x08); //0x08 seems to be the minimum I can get away with

  Serial.begin(19200);
  Serial.println( F("EtherCard/Nanode NTP Client" ) );

  currentTimeserver = 0;

  uint8_t rev = ether.begin(sizeof Ethernet::buffer, mymac, ETH_CS);
  Serial.print( F("\nENC28J60 Revision ") );
  Serial.println( rev, DEC );
  if ( rev == 0) 
    Serial.println( F( "Failed to access Ethernet controller" ) );

  Serial.println( F( "Setting up DHCP" ));
  if (!ether.dhcpSetup())
    Serial.println( F( "DHCP failed" ));

  ether.printIp("My IP: ", ether.myip);
  ether.printIp("Netmask: ", ether.netmask);
  ether.printIp("GW IP: ", ether.gwip);
  ether.printIp("DNS IP: ", ether.dnsip);

  setSyncProvider(getNtpTime);
}

/*
   Send an NTP request.

   Always return 0.

   Response will be parsed in the main loop when it comes.
 */
time_t getNtpTime(){
  // time to send request
  lastUpdate = millis();
  Serial.print( F("TimeSvr: " ) );
  Serial.println( currentTimeserver, DEC );

  if (!ether.dnsLookup( ntpList[currentTimeserver] )) {
    Serial.println( F("DNS failed" ));
  } else {
    ether.printIp("SRV: ", ether.hisip);

    Serial.print( F("Send NTP request " ));
    Serial.println( currentTimeserver, DEC );

    ether.ntpRequest(ether.hisip, ++clientPort);
    Serial.print( F("clientPort: "));
    Serial.println(clientPort, DEC );
  }
  if( ++currentTimeserver >= NUM_TIMESERVERS )
    currentTimeserver = 0;

  return 0;
}

void printDigits(int digits){
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

void serialPrintTime(){
  Serial.print(hourFormat12());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year());
  Serial.println(" UTC");


  Serial.print(hourFormat12(localTime));
  printDigits(minute(localTime));
  printDigits(second(localTime));
  Serial.print(" ");
  Serial.print(day(localTime));
  Serial.print(" ");
  Serial.print(month(localTime));
  Serial.print(" ");
  Serial.print(year(localTime));
  if (usEastern.locIsDST(localTime)) {
    Serial.println(" EDT");
  }else{
    Serial.println(" EST");
  }
}

void loop(){
  uint16_t dat_p;
  char day[22];
  char clock[22];
  int plen = 0;

  // Main processing loop now we have our addresses
  // handle ping and wait for a tcp packet
  plen = ether.packetReceive();
  dat_p=ether.packetLoop(plen);

  // Has unprocessed packet response
  if (plen > 0) {
    timeLong = 0L;

    if (ether.ntpProcessAnswer(&timeLong,clientPort)) {
      Serial.print( F( "Time:" ));
      Serial.println(timeLong); // secs since year 1900

      if (timeLong) {
        if (millis() - lastUpdate > 1500){
          //If the response took too long to come, then skip it
          Serial.print( "Stale NTP response");
        }else{
          timeLong -= GETTIMEOFDAY_TO_NTP_OFFSET;
          setTime(timeLong);
        }
      }
    }
  }

  if (timeStatus() != timeNotSet) {
      if (now() != prevDisplay) { //Update only if time has changed
          prevDisplay = now();
          localTime = usEastern.toLocal(now());
          displayTime();
          serialPrintTime();
      }
  }
}

// End


