#include <EEPROM.h>

#include <ESP8266mDNS.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>

/*
 * Dynamometer Van WiFi Web Server
 *
 * Based on ESP8266 using an ESP-01 board
 *
 * Measure the speed and distance travelled by a 4mm scale
 * vehicle by means of an interrupter sensor on one of the axles
 * on the van.
 *
 * GPIO2 is the feedback from the sensor, 2 pulses are received
 * per rotation of the axle
 * GPIO0 is used to enable the sensor. Set low to enable the sensor
 * This allows the ESP8266 to boot normally regardless of the sensor
 * state.
 *
 * Implements a trip counter, where a trip is a single journey that
 * contains no stationary period of more than 60 seconds
 *
 * If using AP mode connect to http://192.168.4.1/ to see the data.
 * http://192.168.4.1/status will show network status information
 * http://192.168.4.1/connect?SSID=...&PWD=... to connect to a network
 */
 
 #define DMAGIC1  0x34                // Magic numbers used to determine
 #define DMAGIC2  0xA1                // if the EEPROM can be trusted
 
// multicast DNS responder
MDNSResponder mdns;

ESP8266WebServer server(80);
 
char apssid[]   = "Dynamometer";	// Access point SSID
char ssid[]     = "MySSID";	        // your network SSID (name)
char password[] = "WiFiPasswd";	        // your network password

static const int led    = 0;		// GPIO0 enables sensor LED
static const int sensor = 2;		// GPIO2 is sensor feedback

Ticker avgTick;			        // Timer used to crete averages

int refresh         = 15;               // Default refresh time
int triplength      = 60;              // Defalt trip length

int count = 0;			// Count of every sense pulse received
unsigned long last = 0;		// Last time a pulse was received
unsigned long interval = 0;	// Previous duration between pulses
double mph = 0;			// Current scale speed
double maxspeed = 0;		// Max speed seen since startup
double distance = 0;		// Distance in feet covered since startup
int newtrip = 0;		// Flag to indicate start of new trip
double trip_distance = 0;	// Distance covered in trip
double trip_aver = 0;		// Average speed during trip
double trip_max = 0;		// Maximum speed during trip
int tripsamples = 0;		// Number of speed samples during trip
int tripstart = 0;		// Millisecond on which trip started
int tripduration = 0;		// Length of trip in milliseconds

double avg = 0;			// Average speed since startup
unsigned long avs = 0;		// Number of samples in avergae speed

unsigned long zeroms = 0;       // Time at which we stopped (zero speed)

extern void sense();
extern void aver();
extern void handle_root();
extern void handle_norefresh();
extern void handle_status();
extern void handle_help();
extern void handle_reset();
extern void handle_ap();
extern void handle_both();
extern void handle_json();
extern void handle_connect();
extern void handle_config();
extern void handle_configure();
extern void handle_wifi_connect();

void setup()
{
  
  Serial.begin(115200);
  
  EEPROM.begin(512);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apssid);
  
  // Connect to WiFi network
  WiFi.begin(ssid, password);

  delay(1000);

  
  if (!mdns.begin("dyna", WiFi.localIP()))
  {
    Serial.println("Failed to register hostname");
  }
  
  server.on("/", handle_root);
  server.on("/norefresh", handle_norefresh);
  server.on("/status", handle_status);
  server.on("/help", handle_help);
  server.on("/reset", handle_reset);
  server.on("/mode/ap", handle_ap);
  server.on("/mode/both", handle_both);
  server.on("/data", handle_json);
  server.on("/connect", handle_connect);
  server.on("/config", handle_config);
  server.on("/configure", handle_configure);
  server.on("/networks", handle_wifi_connect);
  
  server.begin();
  Serial.println("HTTP server started");
  
    
  pinMode(led, OUTPUT);		// Enable sensor
  digitalWrite(led, 0);
  pinMode(sensor, INPUT);	// Set sensor pin as input and enable interrupt
  attachInterrupt(sensor, sense, FALLING);
  avgTick.attach(1, aver);	// Setup a 1 second recurring timer routine
  
  if (EEPROM.read(0) == DMAGIC1 && EEPROM.read(1) == DMAGIC2)
  {
    refresh = EEPROM.read(3);
    triplength = (EEPROM.read(4) << 8) | EEPROM.read(5);
  }
}

void loop()
{
  // No pulses for more than a second set speed to 0
  if (micros() - last > 1000000 && mph != 0)
  {
    mph = 0;    // No sense for more than 1 second
    zeroms = millis();
  }
 
  // Stationary for more than defined triplength seconds,
  // set flag to start new trip 
  if (mph == 0 && millis() - zeroms > (triplength * 1000))
  {
    newtrip = 1;
  }
  
  mdns.update();
  
  server.handleClient();
}


/**
 * Interrupt routine called on every falling edge of the sensor.
 * This occurs twice per revolution of the wheel on the leading edge of
 * the interruptor pulse.
 */
void sense()
{
    unsigned long now = micros();
    if (last > 0)
    {
	// If this is not the first pulse work out an interval
        interval = now - last;
	// Work out the speed based on a 12mm diameter wheel
        mph = (double)(12 * 27186.0576) / (double)interval;
	// "Debounce" if sensor triggers too soon
        if (mph > 0 && mph < 200)
        {
		if (mph > maxspeed)
                {
                  maxspeed = mph;
                }
                if (mph >trip_max)
                {
                  trip_max = mph;
                }
		tripduration = millis() - tripstart;
        }
        else
        {
		mph = 0;
        }
        if (newtrip)
        {
		// We just started to move ina new trip
		tripstart = millis();
		tripsamples = 0;
		trip_distance = 0;
		trip_aver = 0;
                trip_max = 0;
        }
    }
    last = now;
    count = count + 1;
    distance = distance + 4.712;
    trip_distance += 4.712;
}


/**
 * Timer routine called every second to work out average speed of the vehicle
 * Computes an rolling average whenever the vehicle is moving for both
 * the overall average and the average during this trip.
 */
void aver()
{
    if (mph > 0)
    {
        avg = ((avg * avs) + mph) / (avs + 1);
        avs = avs + 1;
        trip_aver = ((trip_aver * tripsamples) + mph) / (tripsamples + 1);
        tripsamples = tripsamples + 1;
    }
}

String style = "<style>\n"
  "table, td {  border: 1px solid black; border-collapse: collapse; }\n"
  "th { font-size: 1.1em; text-align: left; padding: 5px; \n"
  "     background-color: #A7C942; color: #ffffff; text-align: center; \n"
  "border: 1px solid black; border-collapse: collapse; vertical-align: middle; }"
  "</style>";


/**
 * Send the CSS styles used on the page
 */
String http_style()
{
  return style;
}

/*
 * Some static HTTP content
 */
static char homelink[] = "<P><A HREF=\"/\">Home</A>";
static char prelude[] = "</BODY></HTML>";

static char htmltype[] = "text/html";

extern void handle_with_refresh(int);

/**
 * Handle a / URL request
 *
 * Print table for speeds and distances
 */
void handle_root()
{
  handle_with_refresh(refresh);
}

void handle_norefresh()
{
  handle_with_refresh(0);
}

void handle_with_refresh(int val)
{
  String buffer = "";
  
  buffer += "<html><head>";
  buffer += http_style();
  buffer += "<TITLE>Dynamometer Van</TITLE>";
  if (val)
  {
    buffer += "<meta http-equiv=\"refresh\" content=\"";
    buffer.concat(String(val));
    buffer += "\"/>";
  }
  buffer += "<META HTTP-EQUIV=\"CACHE-CONTROL\" CONTENT=\"NO-CACHE\">";
  buffer += "</head><body>";
  buffer += "<H1>Dynamometer</H1><P>";
  buffer += "Scale speed and distance<P>";
  buffer += "<TABLE><TR><TH COLSPAN=\"3\">Speed</TH><TH ROWSPAN=\"2\">Distance</TH></TR>";
  buffer += "<TR><TH>Current</TH><TH>Average</TH><TH>Maximum</TH></TR>";
  buffer += "<TR><TD>";
  buffer.concat(String(mph));
  buffer += " mph</TD><TD>";
  buffer.concat(String(avg));
  buffer += " mph</TD><TD>";
  buffer.concat(String(maxspeed));
  buffer += " mph</TD><TD>";
  buffer.concat(String(distance / 5280));
  buffer += " miles</TD></TR></TABLE><P>Current trip:<BR>";
  buffer += "<TABLE><TR><TH>Average Speed</TH><TH>Maximum Speed</TH><TH>Distance</TH><TH>Duration</TH></TR>";
  buffer += "<TR><TD>";
  buffer.concat(String(trip_aver));
  buffer += " mph</TD><TD>";
  buffer.concat(String(trip_max));
  buffer += " mph</TD><TD>";
  buffer.concat(String(trip_distance / 5280));
  buffer += " miles</TD><TD>";
  buffer.concat(String(tripduration / 1000));
  buffer += " s</TD></TR></TABLE>";
  if (val)
    buffer += "<P><A HREF=\"/\">Reload</A>";
  else
    buffer += "<P><A HREF=\"/norefresh\">Reload</A>";
  buffer += "&nbsp;&nbsp;&nbsp;&nbsp;<A HREF=\"/help\">Help</A>";
  buffer += prelude;
  server.send(200, htmltype, buffer);
}

/**
 * Handle /status URL
 * Return information on the status of the network
 */
void handle_status()
{
  String buffer = "<html>";

  buffer += "<TITLE>Status</TITLE>";
  buffer += "</head>";
  buffer += http_style();
  buffer += "<body>";
  buffer += "<H1>Dynamometer server</H1>";
  buffer += "<P>Network settings";
  buffer += "<P>";
  buffer += "<TABLE><TR><TH>&nbsp</TH><TH>Station</TH><TH>Access Point</TH></TR>";
  buffer += "<TR><TH>";
  buffer += "SSID";
  buffer += "</TH><TD>";
  buffer.concat(String(WiFi.SSID()));
  buffer += "</TD><TD>";
  buffer += apssid;
  buffer += "</TD></TR>";
  buffer += "<TR><TH>";
  buffer += "IP Address";
  buffer += "</TH><TD>";
  IPAddress addr = WiFi.localIP();
  buffer.concat(String(addr[0]));
  buffer += ".";
  buffer.concat(String(addr[1]));
  buffer += ".";
  buffer.concat(String(addr[2]));
  buffer += ".";
  buffer.concat(String(addr[3]));
  buffer += "</TD><TD>";
  addr = WiFi.softAPIP();
  buffer.concat(String(addr[0]));
  buffer += ".";
  buffer.concat(String(addr[1]));
  buffer += ".";
  buffer.concat(String(addr[2]));
  buffer += ".";
  buffer.concat(String(addr[3]));
  buffer += "</TD></TR></TABLE>";
  buffer += "<P>Configuration Parameters";
  buffer += "<P>";
  buffer += "<TABLE><TR><TH>Parameter</TH><TH>Value</TH></TR>";
  buffer += "<TR><TD>Auto refresh</TD><TD>";
  buffer.concat(String(refresh));
  buffer += " seconds</TD></TR><TR><TD>Trip ends after</TD><TD>";
  buffer.concat(String(triplength));
  buffer += " seconds stationary";
  buffer += "</TD></TR></TABLE>";
  buffer += homelink;
  buffer += prelude;
  server.send(200, htmltype, buffer);
}

/**
 * Handle /help URL
 * Return information on the status of the network
 */
void handle_help()
{
  String buffer = "";

  buffer += "<TITLE>Help</TITLE>";
  buffer += "</head>";
  buffer += http_style();
  buffer += "<body>";
  buffer += "<H1>Dynamometer server help</H1>";
  buffer += "<P>";
  buffer += "<TABLE><TR><TH>URL</TH><TH>Description</TH></TR>";
  buffer += "<TR><TD><a href=\"/\">/</a></TD><TD>Display speed and distance data</TD></TR>";
  buffer += "<TR><TD><a href=\"/data\">/data</a></TD><TD>Return the speed and distance data as a JSON document</TD></TR>";
  buffer += "<TR><TD><a href=\"/norefresh\">/nofresh</a></TD><TD>Display speed and distance data without auto-refresh</TD></TR>";
  buffer += "<TR><TD><a href=\"/status\">/status</a></TD><TD>Show the status of the network connections</TD></TR>";
  buffer += "<TR><TD><a href=\"/reset\">/reset</a></TD><TD>Reset averages and maximums</TD></TR>";
  buffer += "<TR><TD><a href=\"/help\">/help</a></TD><TD>Display this help information</TD></TR>";
  buffer += "<TR><TD><a href=\"configure\">/configure</a></TD><TD>Set configuration via a form</TD></TR>";
  buffer += "<TR><TD><a href=\"networks\">/networks</a></TD><TD>Scan and connect to WiFi networks</TD></TR>";
  buffer += "<TR><TD><a href=\"/mode/ap\">/mode/ap</a></TD><TD>Set WiFi mode to access point only</TD></TR>";
  buffer += "<TR><TD><a href=\"/mode/both\">/mode/both</a></TD><TD>Set WiFi mode to access point and station</TD></TR>";
  buffer += "</TD></TR></TABLE>";
  buffer += homelink;
  buffer += prelude;
  server.send(200, htmltype, buffer);
}

/*
 * Handle a reset URL request
 */
void handle_reset()
{
  maxspeed = 0;
  distance = 0;
  avg = 0;
  avs = 0;
  newtrip = 1;
  handle_root();
}

/*
 * Handle a request to set as access point only
 */
void handle_ap()
{
  WiFi.mode(WIFI_AP);
  handle_status();
}

/*
 * Handle a request to be in both station and access point mode
 */
void handle_both()
{
  WiFi.mode(WIFI_AP_STA);
  handle_status();
}

/*
 * Handle a request for speed and distance as JSON document
 */
void handle_json()
{
  String data = "{ \"data\" : [ \"speed\" : \"";
  data = String(data + String(mph));
  data = String(data + "\", \"distance\":\"" + String(distance) + "\"] }");
  server.send(200, "application/json", data);
}

/**
 * Handle the /connect=... URL
 */
void handle_connect()
{
String buffer = "";
char  ssidbuf[40], pwdbuf[40];

  String ssid = server.arg("SSID");
  String pwd = server.arg("PWD");

  if (ssid.length() && pwd.length())
  {
    ssid.toCharArray(ssidbuf, 40);
    pwd.toCharArray(pwdbuf, 40);
    WiFi.begin(ssidbuf, pwdbuf);
  }
  else if (ssid.length())
  {
    ssid.toCharArray(ssidbuf, 40);
    WiFi.begin(ssidbuf);
  }
  if (ssid.length() == 0 && pwd.length() == 0)
  {
    buffer = "<TITLE>Error</TITLE>";
    buffer += "</head>";
    buffer += "<body>";
    buffer += "<H1>Parameter error in URL</H1>";
    buffer += "<P>";
    buffer += "Expected parameters SSID and optionally PWD.";
    buffer += homelink;
    buffer += prelude;
  }
  else
  {
    buffer = "<TITLE>Connecting...</TITLE>";
    buffer += "</head>";
    buffer += "<body>";
    buffer += "<H1>Connecting</H1>";
    buffer += "<P>";
    buffer += "Connecting to SSID ";
    buffer += ssid;
    buffer += prelude;
  }
  server.send(401, htmltype, buffer);
}

/**
 * Handle the /config=... URL
 */
void handle_config()
{
String buffer = "";
char  buf[40];

  String refreshstr = server.arg("REFRESH");
  String triplenstr = server.arg("TRIPLEN");

  if (refreshstr.length() > 0)
  {
    refreshstr.toCharArray(buf, 40);
    refresh = atoi(buf);
  }
  
  if (triplenstr.length() > 0)
  {
    triplenstr.toCharArray(buf, 40);
    triplength = atoi(buf);
  }
  
  if (refreshstr.length() == 0 && triplenstr.length() == 0)
  {
    buffer = "<TITLE>Error</TITLE>";
    buffer += "</head>";
    buffer += "<body>";
    buffer += "<H1>Parameter error in URL</H1>";
    buffer += "<P>";
    buffer += "Expected REFRESH or TRIPLEN.";
    buffer += homelink;
    buffer += prelude;
    server.send(401, htmltype, buffer);
  }
  else
  {
    /*
     * Update the EEPROM data
     */
    EEPROM.write(0, DMAGIC1);
    EEPROM.write(1, DMAGIC2);
    EEPROM.write(3, refresh);
    EEPROM.write(4, (triplength >> 8) & 0xff);
    EEPROM.write(5, triplength & 0xff);
    EEPROM.commit();

    handle_root();
  }
}

/*
 * Handle a request for the configuration form
 */
void handle_configure()
{
  String buffer = "";
  
    buffer = "<TITLE>Configuration</TITLE>";
    buffer += "</head>";
    buffer += "<body>";
    buffer += "<H1>Configuration</H1>";
    buffer += "<P>";
    buffer += "<form action=\"config\" method=\"GET\">";
    buffer += "<TABLE><TR><TH>Parameter</TH><TH>Value</TH><TR>";
    buffer += "<TR><TD>Refresh interval</TD><TD><INPUT LENGTH=3 NAME=\"REFRESH\" VALUE=\"";
    buffer.concat(String(refresh));
    buffer += "\"/> Seconds</TD></TR>";
    buffer += "<TR><TD>Trip stationary limit</TD><TD><INPUT LENGTH=3 NAME=\"TRIPLEN\" VALUE=\"";
    buffer.concat(String(triplength));
    buffer += "\"/> Seconds</TD></TR>";
    buffer += "</TABLE>";
    buffer += "<BR/><INPUT TYPE=\"Submit\"/>";
    buffer += "</FORM>";
    buffer += homelink;
    buffer += prelude;
    server.send(200, htmltype, buffer);
}

/*
 * Report the encryption type as a printable string
 */
String encryption_type(int type)
{
  switch (type)
  {
  case 2: return "TKIP";
  case 4: return "CCMP";
  case 5: return "WEP";
  case 7: return "None";
  case 8: return "Auto";
  }
  return "???";
}

/*
 * WiFi Setup form
 */
void handle_wifi_connect()
{
  String buffer = "";
  int i, n_networks;
 
 n_networks = WiFi.scanNetworks();
 
 buffer = "<TITLE>WiFI Connection</TITLE>";
 buffer += "</head>";
 buffer += "<body>";
 buffer += "<H1>Setup</H1>";
 buffer += "<P>";
 if (n_networks == 0)
 {
   buffer += "No WiFi networks found.";
 }
 else
 {
    buffer += "<form action=\"connect\" method=\"GET\">";
    buffer += "Network: <select name=\"SSID\">";
    for (i = 0; i < n_networks; i++)
    {
      buffer += "<OPTION value=\"";
      buffer += WiFi.SSID(i);
      buffer += "\">";
      buffer += WiFi.SSID(i);
      buffer += "&nbsp;&nbsp;";
      buffer.concat(encryption_type(WiFi.encryptionType(i)));
      buffer += "&nbsp;&nbsp;";
      buffer.concat(String(WiFi.RSSI(i)));
      buffer += "dB</OPTION>";
    }
    buffer += "</SELECT>";
    buffer += "<P>Password: <INPUT LENGTH=3 NAME=\"PWD\" VALUE=\"\">";
    buffer += "<BR/><INPUT TYPE=\"Submit\"/>";
    buffer += "</FORM>";
 }
 buffer += homelink;
 buffer += prelude;
 server.send(200, htmltype, buffer);
}
