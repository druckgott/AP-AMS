#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
//#include <map>
#include <LittleFS.h>
#include <Servo.h>
#include <Adafruit_NeoPixel.h>

#define MSG_BUFFER_SIZE	(4096)//Configuration of MQTT server

//-=-=-=-=-=-=-=-=↓User Configuration↓-=-=-=-=-=-=-=-=-=-=
String wifiName;//
String wifiKey;//
String bambu_mqtt_broker;//
String bambu_mqtt_password;//
String bambu_device_serial;//
String filamentID;//
String ha_mqtt_broker;
String ha_mqtt_user;
String ha_mqtt_password;
//-=-=-=-=-=-=-=-=↑User Configuration↑-=-=-=-=-=-=-=-=-=-=

//-=-=-=-=-=-↓System Configuration↓-=-=-=-=-=-=-=-=-=
bool debug = true;
String sw_version = "v2.3";
String bambu_mqtt_user = "bblp";
String bambu_mqtt_id = "ams";
String ha_mqtt_id = "ams";
String ha_topic_subscribe;
String bambu_topic_subscribe;// = "device/" + String(bambu_device_serial) + "/report";
String bambu_topic_publish;// = "device/" + String(bambu_device_serial) + "/request";
String bambu_resume = "{\"print\":{\"command\":\"resume\",\"sequence_id\":\"1\"},\"user_id\":\"1\"}"; // Retry|Continue printing
String bambu_unload = "{\"print\":{\"command\":\"ams_change_filament\",\"curr_temp\":220,\"sequence_id\":\"1\",\"tar_temp\":220,\"target\":255},\"user_id\":\"1\"}";
String bambu_load = "{\"print\":{\"command\":\"ams_change_filament\",\"curr_temp\":220,\"sequence_id\":\"1\",\"tar_temp\":220,\"target\":254},\"user_id\":\"1\"}";
String bambu_done = "{\"print\":{\"command\":\"ams_control\",\"param\":\"done\",\"sequence_id\":\"1\"},\"user_id\":\"1\"}"; // Finish
String bambu_clear = "{\"print\":{\"command\": \"clean_print_error\",\"sequence_id\":\"1\"},\"user_id\":\"1\"}";
String bambu_status = "{\"pushing\": {\"sequence_id\": \"0\", \"command\": \"pushall\"}}";
//String bambu_pause = "{\"print\": {\"command\": \"gcode_line\",\"sequence_id\": \"1\",\"param\": \"M400U1\"},\"user_id\": \"1\"}";
//String bambu_pause = "{\"print\":{\"command\":\"pause\",\"sequence_id\":\"1\"},\"user_id\":\"1\"}";
int servoPin = 13;//Servo pins
int motorPin1 = 4;//Motor pin 1
int motorPin2 = 5;//Motor pin 2
int bufferPin1 = 14;//Buffer 1
int bufferPin2 = 16;//Buffer 2
unsigned int bambuRenewTime = 1250;//Bambu Update Time
unsigned int haRenewTime = 3000;//ha push Time
int backTime = 1000;
unsigned int ledBrightness;//LED default brightness
String filamentType;
int filamentTemp;
int ledR;
int ledG;
int ledB;
#define ledPin 12//LED Pinout
#define ledPixels 3//Number of Leds
//-=-=-=-=-=-↑System Configuration↑-=-=-=-=-=-=-=-=-=

//-=-=-=-=-=-Variables required by mqtt callback logic-=-=-=-=-=-=
bool unloadMsg;
bool completeMSG;
bool reSendUnload;
String commandStr = "";//Command transmission
//-=-=-=-=-=-=end

unsigned long lastMsg = 0;
char msg[MSG_BUFFER_SIZE];
WiFiClientSecure bambuWifiClient;
PubSubClient bambuClient(bambuWifiClient);
WiFiClient haWifiClient;
PubSubClient haClient(haWifiClient);

Adafruit_NeoPixel leds(ledPixels, ledPin, NEO_GRB + NEO_KHZ800);

unsigned long bambuLastTime = millis();
unsigned long haLastTime = millis();
unsigned long bambuCheckTime = millis();//mqtt scheduled tasks
unsigned long haCheckTime = millis();
int inLed = 2;//Marquee LED variable
int waitLed = 2;
int completeLed = 2;

Servo servo;//Initialize the servo

void ledAll(unsigned int r, unsigned int g, unsigned int b) {//LED Population
    leds.fill(leds.Color(r,g,b));
    leds.show();
}

//Connect to wifi
void connectWF(String wn,String wk) {
    ledAll(0,0,0);
    int led = 2;
    int count = 1;
    WiFi.begin(wn, wk);
    Serial.print("Connect to wifi [" + String(wifiName) + "]");
    while (WiFi.status() != WL_CONNECTED) {
        count ++;
        if (led == -1){
            led = 2;
            ledAll(0,0,0);
        }else{
            leds.setPixelColor(led,leds.Color(0,255,0));
            leds.show();
            led--;
        }
        Serial.print(".");
        delay(250);
        Serial.println(count);
        if (count > 30){
            ledAll(255,0,0);
            Serial.println("WIFIcennection time out! Pleas check your wifi configuration");
            Serial.println("WIFI Name ["+String(wifiName)+"]");
            Serial.println("WIFI PASSWORD["+String(wifiKey)+"]");
            Serial.println("There is no build-in space in the output this time!");
            Serial.println("You will have two options:");
            Serial.println("1:I have confirmed that there is no problem with my wifi confuguration! Keep try again!");
            Serial.println("2:My configuration is wrong, delete the configuration and rewrite it");
            Serial.println("Please enter your selection:");
            while (Serial.available() == 0){
                Serial.print(".");
                delay(100);
            }
            String content = Serial.readString();
            if (content == "2"){if(LittleFS.remove("/config.json")){Serial.println("SUCCESS!");ESP.restart();}}
            ESP.restart();
        }
    }
    Serial.println("");
    Serial.println("WIFI connected");
    Serial.println("IP: ");
    Serial.println(WiFi.localIP());
    ledAll(50,255,50);
}

//Get persistent data
JsonDocument getPData(){
    File file = LittleFS.open("/data.json", "r");
    JsonDocument Pdata;
    deserializeJson(Pdata, file);
    return Pdata;
}
//Writing persistent data
void writePData(JsonDocument Pdata){
    // Check if Pdata contains the required parameters
    if (Pdata.containsKey("lastFilament") && Pdata.containsKey("step") && Pdata.containsKey("subStep") && Pdata.containsKey("filamentID")) {
        File file = LittleFS.open("/data.json", "w");
        serializeJson(Pdata, file);
        file.close();
    } else {
        Serial.println("Error: The data is missing required parametes and cannot be stored");
        if(LittleFS.remove("/data.json")){Serial.println("SUCCESS!");ESP.restart();}
    }
}

//Get configuration data
JsonDocument getCData(){
    File file = LittleFS.open("/config.json", "r");
    JsonDocument Cdata;
    deserializeJson(Cdata, file);
    return Cdata;
}
//Wrinting configuration data
void writeCData(JsonDocument Cdata){
    File file = LittleFS.open("/config.json", "w");
    serializeJson(Cdata, file);
    file.close();
}

//Define motor drive class and servo control class
class Machinery {
  private:
    int pin1;
    int pin2;
    bool isStop = true;
    String state = "stop";
  public:
    Machinery(int pin1, int pin2) {
      this->pin1 = pin1;
      this->pin2 = pin2;
      pinMode(pin1, OUTPUT);
      pinMode(pin2, OUTPUT);
    }

    void forward() {
      digitalWrite(pin1, HIGH);
      digitalWrite(pin2, LOW);
      isStop = false;
      state = "go ahead";
    }

    void backforward() {
      digitalWrite(pin1, LOW);
      digitalWrite(pin2, HIGH);
      isStop = false;
      state = "Back";
    }

    void stop() {
      digitalWrite(pin1, HIGH);
      digitalWrite(pin2, HIGH);
      isStop = true;
      state = "stop";
    }

    bool getStopState() {
        return isStop;
    }
    String getState(){
        return state;
    }
};
class ServoControl {
    private:
        int angle = -1;
        String state = "Custom Angle";
    public:
        ServoControl(){
        }
        void push() {
            servo.write(0);
            angle = 0;
            state = "push";
        }
        void pull() {
            servo.write(180);
            angle = 180;
            state = "pull";
        }
        void writeAngle(int angle) {
            servo.write(angle);
            angle = angle;
            state = "Custom Angle";
        }
        int getAngle(){
            return angle;
        }
        String getState(){
            return state;
        }
    };
//Define motor servo variables
ServoControl sv;
Machinery mc(motorPin1, motorPin2);

void statePublish(String content){
    Serial.println(content);
    haClient.publish(("AMS/"+filamentID+"/state").c_str(),content.c_str());
}

//Connect Bambu MQTT
void connectBambuMQTT() {
    int count = 1;
    while (!bambuClient.connected()) {
        count ++;
        Serial.println("Try to connect to Bambu mqtt|"+bambu_mqtt_id+"|"+bambu_mqtt_user+"|"+bambu_mqtt_password);
        if (bambuClient.connect(bambu_mqtt_id.c_str(), bambu_mqtt_user.c_str(), bambu_mqtt_password.c_str())) {
            Serial.println("Connection successfull!");
            //Serial.println(bambu_topic_subscribe);
            bambuClient.subscribe(bambu_topic_subscribe.c_str());
            ledAll(ledR,ledG,ledB);
        } else {
            Serial.print("Connection failed, reason:");
            Serial.print(bambuClient.state());
            Serial.println("Reconect after one second");
            delay(1000);
            ledAll(255,0,0);
        }

        if (count > 30){
            ledAll(255,0,0);
            Serial.println("Bambu connection time out! Please check your configuration");
            Serial.println("Bambu ip adress["+String(bambu_mqtt_broker)+"]");
            Serial.println("Bambu Serial Number["+String(bambu_device_serial)+"]");
            Serial.println("Tuozuh Acess Code["+String(bambu_mqtt_password)+"]");
            Serial.println("There is no build-in space in the output this time!");
            Serial.println("You wil have two options:");
            Serial.println("1:I have confirmed that there is no problem with my wifi confuguration! Keep try again!");
            Serial.println("2:My configuration is wrong, delete the configuration and rewrite it");
            Serial.println("Please enter your selection:");
            while (Serial.available() == 0){
                Serial.print(".");
                delay(100);
            }
            String content = Serial.readString();
            if (content == "2"){if(LittleFS.remove("/config.json")){Serial.println("SUCCESS!");ESP.restart();}}
            ESP.restart();
        }
    }
}

//Bambu MQTT callback
void bambuCallback(char* topic, byte* payload, unsigned int length) {
    JsonDocument* data = new JsonDocument();
    deserializeJson(*data, payload, length);
    String sequenceId = (*data)["print"]["sequence_id"].as<String>();
    String amsStatus = (*data)["print"]["ams_status"].as<String>();
    String printError = (*data)["print"]["print_error"].as<String>();
    String hwSwitchState = (*data)["print"]["hw_switch_state"].as<String>();
    String gcodeState = (*data)["print"]["gcode_state"].as<String>();
    String mcPercent = (*data)["print"]["mc_percent"].as<String>();
    String mcRemainingTime = (*data)["print"]["mc_remaining_time"].as<String>();
    // Manually freeing memory
    delete data;

    if (!(amsStatus == printError && printError == hwSwitchState && hwSwitchState == gcodeState && gcodeState == mcPercent && mcPercent == mcRemainingTime && mcRemainingTime == "null")) {
        if (debug){
            statePublish(sequenceId+"|ams["+amsStatus+"]"+"|err["+printError+"]"+"|hw["+hwSwitchState+"]"+"|gcode["+gcodeState+"]"+"|mcper["+mcPercent+"]"+"|mcrtime["+mcRemainingTime+"]");
            //Serial.print("Free memory: ");
            //Serial.print(ESP.getFreeHeap());
            //Serial.println(" bytes");
            statePublish("Free memory: "+String(ESP.getFreeHeap())+" bytes");
            statePublish("-=-=-=-=-");}
        bambuLastTime = millis();
    }
    
    /*
    step represents the five main steps of color change
    1——REcive color change instructions, plan and allocate red, green and blue
    2——Return white
    3——Feed yellow
    4——Standby Teal
    5——Keep Green
    subStep represents a substep, which is used to subdivide the main step
    */
    JsonDocument Pdata = getPData();
    if (Pdata["step"] == "1"){
        if (gcodeState == "PAUSE" and mcPercent.toInt() > 100){
            statePublish("Recive color change command and enter color change preperation state");
            leds.setPixelColor(2,leds.Color(255,0,0));
            leds.setPixelColor(1,leds.Color(0,255,0));
            leds.setPixelColor(0,leds.Color(0,0,255));
            leds.show();
            
            String nextFilament = String(mcPercent.toInt() - 110 + 1);
            Pdata["nextFilament"] = nextFilament;
            unloadMsg = false;
            completeMSG = false;
            reSendUnload = false;
            sv.pull();
            mc.stop();
            statePublish("Local Channel"+String(Pdata["filamentID"])+"|Feeding chanel"+String(Pdata["lastFilament"])+"|Next consumables channel"+nextFilament);

            if (Pdata["filamentID"] == Pdata["lastFilament"]){
                statePublish("Local Channel["+String(Pdata["filamentID"])+"]In loading");//If it is in the loading state, then for this color changing unit, the next step is to either return material or continue printing (not return the material)
                if (nextFilament == Pdata["filamentID"]){
                    statePublish("The machine channel, the loading channel, and the next sonsumable channel are all in the same! No need to change colors!");
                    Pdata["step"] = "5";
                    Pdata["subStep"] = "1";
                }else{
                    //bambuClient.publish(bambu_topic_publish.c_str(),bambu_pause.c_str());
                    statePublish("The next consumable channel is differnt from the machine channel, and the material needs to be changed. Prepare to return the material.");
                    Pdata["step"] = "2";
                    Pdata["subStep"] = "1";
                }
            }else{
                statePublish("Local Channel["+String(Pdata["filamentID"])+"]Not loading");//If the color-changing unit is not loading, there are two possiblities: either the color-changing unit has nothing to do with it, or it is preparing the load material.
                if (nextFilament == Pdata["filamentID"]){
                    statePublish("The machine channel is about to change color and is ready to feed material.");
                    Pdata["step"] = String("3");
                    Pdata["subStep"] = String("1");
                }else{
                    statePublish("The local channel has nothing to to with this color change, so no color change is required.");
                    Pdata["step"] = String("4");
                    Pdata["subStep"] = String("1");
                }
            }
        }else{
            ledAll(ledR,ledG,ledB);
            }
    }else if (Pdata["step"] == "2"){
        if (Pdata["subStep"] == "1"){
            statePublish("Entering the return state");
            leds.clear();
            leds.setPixelColor(2,leds.Color(255,255,255));
            leds.show();
            bambuClient.publish(bambu_topic_publish.c_str(),bambu_unload.c_str());
            reSendUnload = true;
            Pdata["subStep"] = "2";
        }else if (Pdata["subStep"] == "2"){
            leds.setPixelColor(1,leds.Color(255,255,255));
            leds.show();
            if (not reSendUnload){
                reSendUnload = true;
                delay(3000);
                statePublish("The material has not been returned yet, send a return request");
                bambuClient.publish(bambu_topic_publish.c_str(),bambu_unload.c_str());
            }else if (printError == "318750723") {
                statePublish("Pull out the consumables");
                sv.push();
                mc.backforward();
                Pdata["subStep"] = "3";
            } else if (printError == "318734339") {
                statePublish("Pull out the consumables");
                sv.push();
                mc.backforward();
                bambuClient.publish(bambu_topic_publish.c_str(), bambu_resume.c_str());
                Pdata["subStep"] = "3";
            }
        }else if (Pdata["subStep"] == "3" && amsStatus == "0"){
            statePublish("The material return is compleded and the color change is completed");
            leds.setPixelColor(2,leds.Color(255,255,255));
            leds.show();
            delay(backTime);
            sv.pull();
            mc.stop();
            Pdata["step"] = "4";
            Pdata["subStep"] = "1";
        }
    }else if (Pdata["step"] == "3"){
        if (Pdata["subStep"] == "1"){
            if (amsStatus == "0") {
                statePublish("Enter feeding state");
                leds.clear();
                leds.setPixelColor(2,leds.Color(255,255,0));
                leds.show();
                bambuClient.publish(bambu_topic_publish.c_str(), bambu_load.c_str());
                Pdata["subStep"] = "2"; // Update subStep
            } else {
                if (!unloadMsg){
                    statePublish("Waiting for the consumables to be returned…");
                    unloadMsg = true;
                }else{
                    Serial.print(".");
                }

                //Marquee
                if (inLed == -1){
                    inLed = 2;
                    ledAll(0,0,0);
                }else{
                    leds.setPixelColor(inLed,leds.Color(255,255,0));
                    leds.show();
                    inLed--;
                }
            }
        }else if (Pdata["subStep"] == "2" && printError == "318750726"){
            statePublish("Feeding consumables");
            sv.push();
            mc.forward();
            Pdata["subStep"] = "3";

            leds.clear();
            leds.setPixelColor(2,leds.Color(255,255,0));
            leds.setPixelColor(1,leds.Color(255,255,0));
            leds.show();
        }else if ((Pdata["subStep"] == "3" && amsStatus == "262" && hwSwitchState == "1") or digitalRead(bufferPin1) == 1){
            statePublish("Stop Shipping");
            mc.stop();
            bambuClient.publish(bambu_topic_publish.c_str(),bambu_done.c_str());
            Pdata["subStep"] = "4";

            leds.setPixelColor(0,leds.Color(255,255,0));
            leds.show();
        }else if (Pdata["subStep"] == "4"){
            if (hwSwitchState == "0") {
                statePublish("Feeding faulure detected, re-feed!");
                mc.backforward();
                delay(2000);
                mc.stop();
                Pdata["subStep"] = "2";
                
                leds.setPixelColor(2,leds.Color(255,255,0));
                leds.setPixelColor(1,leds.Color(255,0,0));
                leds.show();
            } else if (hwSwitchState == "1") {
                statePublish("Feeding is successful, waiting for extrusion and meterial change");
                sv.pull(); 
                Pdata["subStep"] = "5"; // Update subStep

                leds.setPixelColor(2,leds.Color(255,255,0));
                leds.setPixelColor(1,leds.Color(0,255,0));
                leds.show();
            }
        }else if (Pdata["subStep"] == "5"){
            if (printError == "318734343") {
                if (hwSwitchState == "1"){
                    statePublish("I was a false alarm, Click again to confirm");
                    bambuClient.publish(bambu_topic_publish.c_str(), bambu_done.c_str());
                    leds.setPixelColor(2,leds.Color(255,255,0));
                    leds.setPixelColor(1,leds.Color(0,255,0));
                    leds.setPixelColor(0,leds.Color(0,255,0));
                    leds.show();
                }else if (hwSwitchState == "0"){
                    statePublish("Feeding failure detected... Enter step AGAIN to feed again");
                    leds.setPixelColor(2,leds.Color(255,255,0));
                    leds.setPixelColor(1,leds.Color(255,0,0));
                    leds.setPixelColor(0,leds.Color(255,0,0));
                    leds.show();
                    sv.push();
                    mc.backforward();
                    delay(2000);
                    mc.stop();
                    Pdata["subStep"] = "AGAIN";
                }
            } else if (amsStatus == "768") {
                statePublish("Wuhu-Material replacement competed！");
                ledAll(0,255,0);
                delay(1000);
                bambuClient.publish(bambu_topic_publish.c_str(), bambu_resume.c_str());
                Pdata["step"] = "4";
                Pdata["subStep"] = "1";
            }
        }else if (Pdata["subStep"] == "AGAIN"){
            if (hwSwitchState == "0"){
                statePublish("Try to re-feed");
                mc.forward();
            }else if (hwSwitchState == "1"){
                statePublish("Shipping success!");
                mc.stop();
                sv.pull();
                Pdata["subStep"] = "5";
            }
        }
    }else if (Pdata["step"] == "4"){
        if (!completeMSG){
            statePublish("Enter the watching state and wait for the color change to complete");
            completeMSG = true;
        }else{
            Serial.print(".");
        }

        //Marquee
        if (waitLed == -1){
            waitLed = 2;
            ledAll(0,0,0);
        }else{
            leds.setPixelColor(waitLed,leds.Color(0,255,255));
            leds.show();
            waitLed--;
        }

        if (amsStatus == "1280" and gcodeState != "PAUSE") {
            String nextFilament = Pdata["nextFilament"];
            statePublish("Color change completed! Swich the feeding changel to ["+nextFilament+"]");
            Pdata["step"] = "1";
            Pdata["subStep"] = "1";
            Pdata["lastFilament"] = nextFilament;
            ledAll(0,255,0);
        }
    }else if (Pdata["step"] == "5"){
        bambuClient.publish(bambu_topic_publish.c_str(),bambu_resume.c_str());
        if (!completeMSG){
            statePublish("Send Continue command");
            completeMSG = true;
        }else{
            Serial.print(".");
        }
        
        //Marquee
        if (completeLed == -1){
            completeLed = 2;
            ledAll(0,0,0);
        }else{
            leds.setPixelColor(completeLed,leds.Color(0,255,255));
            leds.show();
            completeLed--;
        }

        if (amsStatus == "1280") {
            statePublish("Finish!");
            Pdata["step"] = "1";
            Pdata["subStep"] = "1";
            ledAll(0,255,0);
            delay(500);
            statePublish("Send new temperatur!");
            bambuClient.publish(bambu_topic_publish.c_str(),
            ("{\"print\": {\"command\": \"gcode_line\",\"sequence_id\": \"1\",\"param\": \"M109 S"+String(filamentTemp)+"\"},\"user_id\": \"1\"}")
            .c_str());
        }
    }
    
    writePData(Pdata);
}

//Connect hamqtt
void connectHaMQTT() {
    int count = 1;
    while (!haClient.connected()) {
        count ++;
        Serial.println("Try to connect to HomeAssitant mqtt|"+ha_mqtt_broker+"|"+ha_mqtt_user+"|"+ha_mqtt_password+"|"+String(ESP.getChipId(), HEX));
        if (haClient.connect(String(ESP.getChipId(), HEX).c_str(), ha_mqtt_user.c_str(), ha_mqtt_password.c_str())) {
            Serial.println("Connection successful!");
            //Serial.println(ha_topic_subscribe);
            haClient.subscribe(ha_topic_subscribe.c_str());
            ledAll(ledR,ledG,ledB);
        } else {
            Serial.print("Connection failed, reason:");
            Serial.print(haClient.state());
            Serial.println("Recconnect after one second");
            delay(1000);
            ledAll(255,0,0);
        }

        if (count > 5){
            ledAll(255,0,0);
            Serial.println("HomeAssistant connection timed out! Please check your configuration");
            Serial.println("HomeAssistantip adress["+String(ha_mqtt_broker)+"]");
            Serial.println("HomeAssistant Account["+String(ha_mqtt_user)+"]");
            Serial.println("HomeAssistant Password["+String(ha_mqtt_password)+"]");
            Serial.println("There is no built-in space in the output this time!");
            Serial.println("You will have two options:");
            Serial.println("1:I have confirmed that there is no problem with my wifi confuguration! Keep try again!");
            Serial.println("2:My configuration is wrong, delete the configuration and rewrite it");
            Serial.println("Please enter your selection:");
            while (Serial.available() == 0){
                Serial.print(".");
                delay(100);
            }
            String content = Serial.readString();
            if (content == "2"){if(LittleFS.remove("/config.json")){Serial.println("SUCCESS!");ESP.restart();}}
            ESP.restart();
        }
    }
}

void haCallback(char* topic, byte* payload, unsigned int length) {
    JsonDocument data;
    deserializeJson(data, payload, length);
    serializeJsonPretty(data,Serial);
    // Manually freeing memory
    JsonDocument PData = getPData();
    JsonDocument CData = getCData();

    if (data["command"] == "onTun"){
        PData["lastFilament"] = data["value"].as<String>();
    }else if (data["command"] == "svAng"){
        sv.writeAngle(data["value"].as<String>().toInt());
    }else if (data["command"] == "step"){
        PData["step"] = data["value"].as<String>();
    }else if (data["command"] == "subStep"){
        PData["subStep"] = data["value"].as<String>();
    }else if (data["command"] == "wifiName"){
        CData["wifiName"] = data["value"].as<String>();
        wifiName = data["value"].as<String>();
    }else if (data["command"] == "wifiKey"){
        CData["wifiKey"] = data["value"].as<String>();
        wifiKey = data["value"].as<String>();
    }else if (data["command"] == "bambuIPAD"){
        CData["bambu_mqtt_broker"] = data["value"].as<String>();
        bambu_mqtt_broker = data["value"].as<String>();
    }else if (data["command"] == "bambuSID"){
        CData["bambu_device_serial"] = data["value"].as<String>();
        bambu_device_serial = data["value"].as<String>();
    }else if (data["command"] == "bambuKey"){
        CData["bambu_mqtt_password"] = data["value"].as<String>();
        bambu_mqtt_password = data["value"].as<String>();
    }else if (data["command"] == "LedBri"){
        ledBrightness = data["value"].as<String>().toInt();
        leds.setBrightness(ledBrightness);
        CData["ledBrightness"] = ledBrightness;
    }else if (data["command"] == "command"){
        commandStr = data["value"].as<String>();
        haClient.publish(("AMS/"+filamentID+"/command").c_str(),data["value"].as<String>().c_str());
        haClient.publish(("AMS/"+filamentID+"/command").c_str(),"");
    }else if (data["command"] == "mcState"){
        if (data["value"] == "go ahead"){
            mc.forward();
        }else if (data["value"] == "Back"){
            mc.backforward();
        }else if (data["value"] == "stop"){
            mc.stop();
        }
    }else if (data["command"] == "svState"){
        if (data["value"] == "push"){
            sv.push();
        }else if (data["value"] == "pull"){
            sv.pull();
        }
    }else if (String(data["command"]).indexOf("filaLig") != -1){
        if (String(data["command"]).indexOf("swi") != -1){
            if (data["value"] == "ON"){
                leds.setBrightness(ledBrightness);
                haClient.publish(("AMS/"+filamentID+"/filaLig/swi").c_str(),"{\"command\":\"filaLigswi\",\"value\":\"ON\"}");
            }else if (data["value"] == "OFF"){
                leds.setBrightness(0);
                haClient.publish(("AMS/"+filamentID+"/filaLig/swi").c_str(),"{\"command\":\"filaLigswi\",\"value\":\"OFF\"}");
            }
        }else if (String(data["command"]).indexOf("bri") != -1){
            ledBrightness = data["value"].as<String>().toInt();
            leds.setBrightness(ledBrightness);
            CData["ledBrightness"] = ledBrightness;
        }else if (String(data["command"]).indexOf("rgb") != -1){
            String input = String(data["value"]);
            int comma1 = input.indexOf(',');
            int comma2 = input.indexOf(',', comma1 + 1);

            int r = input.substring(0, comma1).toInt();
            int g = input.substring(comma1 + 1, comma2).toInt();
            int b = input.substring(comma2 + 1).toInt();

            ledR = r;
            ledG = g;
            ledB = b;

            CData["ledR"] = ledR;
            CData["ledG"] = ledG;
            CData["ledB"] = ledB;
        }
    }else if (data["command"] == "filamentTemp"){
        filamentTemp = data["value"].as<int>();
        CData["filamentTemp"] = filamentTemp;
    }else if (data["command"] == "filamentType"){
        filamentType = data["value"].as<String>();
        CData["filamentType"] = filamentType;
    }
    
    
    writePData(PData);
    writeCData(CData);
    haClient.publish(("AMS/"+filamentID+"/nowTun").c_str(),filamentID.c_str());
    haClient.publish(("AMS/"+filamentID+"/nextTun").c_str(),PData["nextFilament"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/onTun").c_str(),PData["lastFilament"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/svAng").c_str(),String(sv.getAngle()).c_str());
    haClient.publish(("AMS/"+filamentID+"/step").c_str(),PData["step"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/subStep").c_str(),PData["subStep"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/wifiName").c_str(),wifiName.c_str());
    haClient.publish(("AMS/"+filamentID+"/wifiKey").c_str(),wifiKey.c_str());
    haClient.publish(("AMS/"+filamentID+"/bambuIPAD").c_str(),bambu_mqtt_broker.c_str());
    haClient.publish(("AMS/"+filamentID+"/bambuSID").c_str(),bambu_device_serial.c_str());
    haClient.publish(("AMS/"+filamentID+"/bambuKey").c_str(),bambu_mqtt_password.c_str());
    haClient.publish(("AMS/"+filamentID+"/LedBri").c_str(),String(ledBrightness).c_str());
    haClient.publish(("AMS/"+filamentID+"/mcState").c_str(),mc.getState().c_str());
    haClient.publish(("AMS/"+filamentID+"/svState").c_str(),sv.getState().c_str());
    haClient.publish(("AMS/"+filamentID+"/filaLig/bri").c_str(),String(ledBrightness).c_str());
    haClient.publish(("AMS/"+filamentID+"/filaLig/rgb").c_str(),((String(ledR)+","+String(ledG)+","+String(ledB))).c_str());
    haClient.publish(("AMS/"+filamentID+"/filamentTemp").c_str(),String(filamentTemp).c_str());
    haClient.publish(("AMS/"+filamentID+"/filamentType").c_str(),String(filamentType).c_str());
}

//Scheduled tasks
void bambuTimerCallback() {
    if (debug){Serial.println("bambu scheduled task execution！");}
    bambuClient.publish(bambu_topic_publish.c_str(), bambu_status.c_str());
}
//Scheduled tasks
void haTimerCallback() {
    if (debug){Serial.println("HomeAssistant scheduled task execution！");}
    JsonDocument PData = getPData();
    haClient.publish(("AMS/"+filamentID+"/nowTun").c_str(),filamentID.c_str());
    haClient.publish(("AMS/"+filamentID+"/nextTun").c_str(),PData["nextFilament"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/onTun").c_str(),PData["lastFilament"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/svAng").c_str(),String(sv.getAngle()).c_str());
    haClient.publish(("AMS/"+filamentID+"/step").c_str(),PData["step"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/subStep").c_str(),PData["subStep"].as<String>().c_str());
    haClient.publish(("AMS/"+filamentID+"/wifiName").c_str(),wifiName.c_str());
    haClient.publish(("AMS/"+filamentID+"/wifiKey").c_str(),wifiKey.c_str());
    haClient.publish(("AMS/"+filamentID+"/bambuIPAD").c_str(),bambu_mqtt_broker.c_str());
    haClient.publish(("AMS/"+filamentID+"/bambuSID").c_str(),bambu_device_serial.c_str());
    haClient.publish(("AMS/"+filamentID+"/bambuKey").c_str(),bambu_mqtt_password.c_str());
    haClient.publish(("AMS/"+filamentID+"/LedBri").c_str(),String(ledBrightness).c_str());
    haClient.publish(("AMS/"+filamentID+"/mcState").c_str(),mc.getState().c_str());
    haClient.publish(("AMS/"+filamentID+"/svState").c_str(),sv.getState().c_str());
    haClient.publish(("AMS/"+filamentID+"/backTime").c_str(),String(backTime).c_str());
    haClient.publish(("AMS/"+filamentID+"/filaLig/rgb").c_str(),((String(ledR)+","+String(ledG)+","+String(ledB))).c_str());
    haClient.publish(("AMS/"+filamentID+"/filamentTemp").c_str(),String(filamentTemp).c_str());
    haClient.publish(("AMS/"+filamentID+"/filamentType").c_str(),String(filamentType).c_str());

    haLastTime = millis();
}

JsonArray initText(String name,String id,String detail,JsonArray array){
    String topic = "homeassistant/text/ams"+id+detail+"/config";
    String json = ("{\"name\":\"" +name +"\",\"command_topic\":\"AMS/" +filamentID +"\",\"state_topic\":\"AMS/" +
    id +"/" +detail +"\",\"command_template\":\"{\\\"command\\\":\\\"" +detail +
    "\\\",\\\"value\\\":\\\"{{  value  }}\\\"}\",\"unique_id\": \"ams"+"text"+id+name+"\", \"device\":{\"identifiers\":\"APAMS"
    +id+"\",\"name\":\"AP-AMS-"+id+"aisle\",\"manufacturer\":\"AP-AMS\",\"hw_version\":\""+sw_version+"\"}}");
    //Serial.println(json);
    array.add(topic);
    haClient.publish(topic.c_str(),json.c_str());
    return array;
}

JsonArray initSensor(String name,String id,String detail,JsonArray array){
    String topic = "homeassistant/sensor/ams"+id+detail+"/config";
    String json = ("{\"name\":\""+name+"\",\"state_topic\":\"AMS/"+id+"/"+detail+"\",\"unique_id\": \"ams"
    +"sensor"+id+name+"\", \"device\":{\"identifiers\":\"APAMS"+id+"\",\"name\":\"AP-AMS-"+id
    +"aisle\",\"manufacturer\":\"AP-AMS\",\"hw_version\":\""+sw_version+"\"}}");
    array.add(topic);
    haClient.publish(topic.c_str(),json.c_str());
    return array;
}

JsonArray initSelect(String name,String id,String detail,String options,JsonArray array){
    String topic = "homeassistant/select/ams"+id+detail+"/config";
    String json = ("{\"name\":\"" +name +"\",\"command_topic\":\"AMS/" +filamentID +
    "\",\"state_topic\":\"AMS/" +id +"/" +detail +"\",\"command_template\":\"{\\\"command\\\":\\\"" +
    detail +"\\\",\\\"value\\\":\\\"{{  value  }}\\\"}\",\"options\":["+options+"],\"unique_id\": \"ams"+"select"
    +id+name+"\", \"device\":{\"identifiers\":\"APAMS"+id+"\",\"name\":\"AP-AMS-"+id
    +"aisle\",\"manufacturer\":\"AP-AMS\",\"hw_version\":\""+sw_version+"\"}}");
    array.add(topic);
    haClient.publish(topic.c_str(),json.c_str());
    return array;
}

JsonArray initLight(String name,String id,String detail,JsonArray array){
    String topic = "homeassistant/light/ams"+id+detail+"/config";
    String json = "{\"name\":\"" + name + "\""
    + ",\"state_topic\":\"AMS/" + id + "/" + detail + "/swi\",\"command_topic\":\"AMS/" + id + "\","
    + "\"brightness_state_topic\":\"AMS/" + id + "/" + detail + "/bri\",\"brightness_command_topic\":\"AMS/" + id + "\","
    + "\"brightness_command_template\":\"{\\\"command\\\":\\\"" + detail + "bri\\\",\\\"value\\\":\\\"{{ value }}\\\"}\","
    + "\"rgb_state_topic\":\"AMS/" + id + "/" + detail + "/rgb\",\"rgb_command_topic\":\"AMS/" + id + "\","
    + "\"rgb_command_template\":\"{\\\"command\\\":\\\"" + detail + "rgb\\\",\\\"value\\\":\\\"{{ value }}\\\"}\","
    + "\"unique_id\":\"ams" + "light" + id + detail + "\","
    + "\"payload_on\":\"{\\\"command\\\":\\\"" + detail + "swi\\\",\\\"value\\\":\\\"ON\\\"}\","
    + "\"payload_off\":\"{\\\"command\\\":\\\"" + detail + "swi\\\",\\\"value\\\":\\\"OFF\\\"}\""
    + ",\"device\":{\"identifiers\":\"APAMS" + id + "\",\"name\":\"AP-AMS-" + id + "aisle\",\"manufacturer\":\"AP-AMS\",\"hw_version\":\"" + sw_version + "\"}}";
    array.add(topic);
    haClient.publish(topic.c_str(),json.c_str());
    return array;
}

void setup() {
    leds.begin();
    Serial.begin(115200);
    LittleFS.begin();
    delay(1);
    leds.clear();
    leds.show();

    if (!LittleFS.exists("/config.json")) {
        ledAll(255,0,0);
        Serial.println("");
        Serial.println("The confuguration file does not exist! Create a configuration file");
        Serial.println("1. Please enter the wifi name:");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        wifiName = Serial.readString();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+wifiName);

        delay(500);
        ledAll(255,0,0);
        
        Serial.println("2. Please enter the wifi password:");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        wifiKey = Serial.readString();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+wifiKey);
        
        delay(500);
        ledAll(255,0,0);

        Serial.println("3.Please enter the IP adress of the Bambulab:");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        bambu_mqtt_broker = Serial.readString();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+bambu_mqtt_broker);
        
        delay(500);
        ledAll(255,0,0);

        Serial.println("4.Please enter the access code / pasword of the Bambu printer:");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        bambu_mqtt_password = Serial.readString();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+bambu_mqtt_password);
        
        delay(500);
        ledAll(255,0,0);
        
        Serial.println("5.Please enter the serial number of the Bambu printer:");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        bambu_device_serial = Serial.readString();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+bambu_device_serial);
        
        delay(500);
        ledAll(255,0,0);
    
        Serial.println("6. Pleas enter the channel number of this machine:");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        filamentID = Serial.readString();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+filamentID);
        
        delay(500);
        ledAll(255,0,0);
    
        Serial.println("7.Please enter the HomeAssistant server adress:");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        ha_mqtt_broker = Serial.readString();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+ha_mqtt_broker);
        
        delay(500);
        ledAll(255,0,0);
    
        Serial.println("8.Please enter the HomeAssistant account (if you don´t have, enter “NONE”):");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        String message = Serial.readString();
        if (message != "NONE"){
            ha_mqtt_user = message;
        }
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+message);
        
        delay(500);
        ledAll(255,0,0);
    
        Serial.println("9.Pleas enter the HomeAssistant password (if none, enter “NONE”):");
        while (!(Serial.available() > 0)){
            delay(100);
        }
        String tmpmessage = Serial.readString();
        if (tmpmessage != "NONE"){
            ha_mqtt_password = tmpmessage;
        }
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+tmpmessage);

        delay(500);
        ledAll(255,0,0);

        Serial.println("10. Please enter the withdrawal delay [in milliseconds]:");
        while (!(Serial.available() > 0)){
            delay(100);
        }

        backTime = Serial.readString().toInt();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+String(backTime));
        
        Serial.println("11.Pleas enter the consumable temperatur of this channel (can be chaned later):");
        while (!(Serial.available() > 0)){
            delay(100);
        }

        filamentTemp = Serial.readString().toInt();
        ledAll(0,255,0);
        Serial.println("The data obtained-> "+String(filamentTemp));
        
        JsonDocument Cdata;
        Cdata["wifiName"] = wifiName;
        Cdata["wifiKey"] = wifiKey;
        Cdata["bambu_mqtt_broker"] = bambu_mqtt_broker;
        Cdata["bambu_mqtt_password"] = bambu_mqtt_password;
        Cdata["bambu_device_serial"] = bambu_device_serial;
        Cdata["filamentID"] = filamentID;
        Cdata["ledBrightness"] = 100;
        Cdata["HomeAssistant_mqtt_broker"] = ha_mqtt_broker;
        Cdata["HomeAssistant_mqtt_user"] = ha_mqtt_user;
        Cdata["HomeAssistant_mqtt_password"] = ha_mqtt_password;
        Cdata["backTime"] = backTime;
        Cdata["filamentTemp"]  = filamentTemp;
        Cdata["filamentType"] = filamentType;
        ledR = 0;
        ledG = 0;
        ledB = 255;
        Cdata["ledR"] = ledR;
        Cdata["ledG"] = ledG;
        Cdata["ledB"] = ledB;
        ledBrightness = 100;
        writeCData(Cdata);
    }else{
        JsonDocument Cdata = getCData();
        serializeJsonPretty(Cdata,Serial);
        wifiName = Cdata["wifiName"].as<String>();
        wifiKey = Cdata["wifiKey"].as<String>();
        bambu_mqtt_broker = Cdata["bambu_mqtt_broker"].as<String>();
        bambu_mqtt_password = Cdata["bambu_mqtt_password"].as<String>();
        bambu_device_serial = Cdata["bambu_device_serial"].as<String>();
        filamentID = Cdata["filamentID"].as<String>();
        ledBrightness = Cdata["ledBrightness"].as<unsigned int>();
        ha_mqtt_broker = Cdata["HomeAssistant_mqtt_broker"].as<String>();
        ha_mqtt_user = Cdata["HomeAssistant_mqtt_user"].as<String>();
        ha_mqtt_password = Cdata["HomeAssistant_mqtt_password"].as<String>();
        backTime = Cdata["backTime"].as<int>();
        filamentTemp = Cdata["filamentTemp"].as<int>();
        filamentType = Cdata["filamentType"].as<String>();
        ledR = Cdata["ledR"];
        ledG = Cdata["ledG"];
        ledB = Cdata["ledB"];
        ledAll(0,255,0);
    }
    bambu_topic_subscribe = "device/" + String(bambu_device_serial) + "/report";
    bambu_topic_publish = "device/" + String(bambu_device_serial) + "/request";
    ha_topic_subscribe = "AMS/"+filamentID;
    leds.setBrightness(ledBrightness);

    connectWF(wifiName,wifiKey);

    servo.attach(servoPin,500,2500);
    //servo.write(20);//Initial 20° fo easy debugging

    pinMode(bufferPin1, INPUT_PULLDOWN_16);
    pinMode(bufferPin2, INPUT_PULLDOWN_16);

    bambuWifiClient.setInsecure();
    bambuClient.setServer(bambu_mqtt_broker.c_str(), 8883);
    bambuClient.setCallback(bambuCallback);
    bambuClient.setBufferSize(4096);
    haClient.setServer(ha_mqtt_broker.c_str(),1883);
    haClient.setCallback(haCallback);
    haClient.setBufferSize(4096);
    
    if (!LittleFS.exists("/data.json")) {
        JsonDocument Pdata;
        Pdata["lastFilament"] = "1";
        Pdata["step"] = "1";
        Pdata["subStep"] = "1";
        Pdata["filamentID"] = filamentID;
        writePData(Pdata);
        Serial.println("Initialised data successfully！");
    } else {
        JsonDocument Pdata = getPData();
        Pdata["filamentID"] = filamentID;
        //Pdata["lastFilament"] = "1";//Define the last consumable as 1 each time (not recommended)
        writePData(Pdata);
        serializeJsonPretty(Pdata, Serial);
        Serial.println("Successfully read the configuration file!");
    }

    connectBambuMQTT();
    connectHaMQTT();

    JsonDocument haData;
    JsonArray discoverList = haData["discovery_topic"].to<JsonArray>();

    discoverList = initText("Feeding channel",filamentID,"onTun",discoverList);
    discoverList = initText("SErvo Angle",filamentID,"svAng",discoverList);
    discoverList = initText("Main steps",filamentID,"step",discoverList);
    discoverList = initText("Secondary Steps",filamentID,"subStep",discoverList);
    discoverList = initText("WIFI NAME",filamentID,"wifiName",discoverList);
    discoverList = initText("WIFI PASSWORD",filamentID,"wifiKey",discoverList);
    discoverList = initText("Bambu IP Adress",filamentID,"bambuIPAD",discoverList);
    discoverList = initText("Bambu Serial Number",filamentID,"bambuSID",discoverList);
    discoverList = initText("Bambu Access Code",filamentID,"bambuKey",discoverList);
    discoverList = initText("LED brightness",filamentID,"LedBri",discoverList);
    discoverList = initText("Execute Instructions",filamentID,"command",discoverList);
    discoverList = initText("Withdrawal delay",filamentID,"backTime",discoverList);
    discoverList = initText("Consumables temperature",filamentID,"filamentTemp",discoverList);
    discoverList = initText("Consumables types",filamentID,"filamentType",discoverList);
    discoverList = initSelect("Motor status",filamentID,"mcState","\"go ahead\",\"Back\",\"stop\"",discoverList);
    discoverList = initSelect("舵机状态",filamentID,"svState","\"push\",\"pull\",\"Custom Angle\"",discoverList);
    discoverList = initSensor("state",filamentID,"state",discoverList);
    discoverList = initSensor("Local Channel",filamentID,"nowTun",discoverList);
    discoverList = initSensor("NEyt channel",filamentID,"nextTun",discoverList);
    discoverList = initLight("Supplies indicator",filamentID,"filaLig",discoverList);

    File file = LittleFS.open("/ha.json", "w");
    serializeJson(haData, file);
    Serial.println("Initialization of HomeAssistant successful!");
    Serial.println("");
    serializeJsonPretty(haData,Serial);
    Serial.println("");

    haClient.publish(("AMS/"+filamentID+"/filaLig/swi").c_str(),"{\"command\":\"filaLigswi\",\"value\":\"ON\"}");
    haClient.publish(("AMS/"+filamentID+"/filaLig/bri").c_str(),String(ledBrightness).c_str());
    haClient.publish(("AMS/"+filamentID+"/filaLig/rgb").c_str(),((String(ledR)+","+String(ledG)+","+String(ledB))).c_str());
    Serial.println("-=-=-=setup execution completed!=-=-=-");
}

void loop() {
    if (!bambuClient.connected()) {
        connectBambuMQTT();
    }
    bambuClient.loop();
    
    if (!haClient.connected()) {
        connectHaMQTT();
    }
    haClient.loop();

    unsigned long nowTime =  millis();
    if (nowTime-bambuLastTime > bambuRenewTime and nowTime-bambuCheckTime > bambuRenewTime*0.8){
        bambuTimerCallback();
        bambuCheckTime = millis();
        leds.setPixelColor(0,leds.Color(10,255,10));
        leds.show();
        delay(10);
        leds.setPixelColor(0,leds.Color(0,0,0));
        leds.show();
    }
    if (nowTime-haLastTime > haRenewTime and nowTime-haCheckTime > haRenewTime*0.8){
        haTimerCallback();
        haCheckTime = millis();
        leds.setPixelColor(0,leds.Color(10,10,255));
        leds.show();
        delay(10);
        leds.setPixelColor(0,leds.Color(0,0,0));
        leds.show();
    }

    if (not mc.getStopState()){
        if (digitalRead(bufferPin1) == 1 or digitalRead(bufferPin2) == 1){
        mc.stop();}
        delay(100);
    }

    if (Serial.available()>0 or commandStr != ""){

        String content;
        if (Serial.available()>0){
            content = Serial.readString();
        }else if (commandStr != ""){
            content = commandStr;
            commandStr = "";
        }

        if (content=="delete config"){
            if(LittleFS.remove("/config.json")){Serial.println("SUCCESS!");ESP.restart();}
        }else if (content == "delet data")
        {
            if(LittleFS.remove("/data.json")){Serial.println("SUCCESS!");ESP.restart();}
        }else if (content == "delet ha")
        {
            if(LittleFS.remove("/ha.json")){Serial.println("SUCCESS!");ESP.restart();}
        }else if (content == "confirm")
        {
            bambuClient.publish(bambu_topic_publish.c_str(),bambu_done.c_str());
            Serial.println("confirm SEND!");
        }else if (content == "resume")
        {
            bambuClient.publish(bambu_topic_publish.c_str(),bambu_resume.c_str());
            Serial.println("resume SEND!");
        }else if (content == "debug")
        {
            debug = not debug;
            Serial.println("debug change");
        }else if (content == "push")
        {
            sv.push();
            Serial.println("push COMPLETE");
        }else if (content == "pull")
        {
            sv.pull();
            Serial.println("pull COMPLETE");
        }else if (content.indexOf("sv") != -1)
        {   
            String numberString = "";
            for (unsigned int i = 0; i < content.length(); i++) {
            if (isdigit(content[i])) {
                numberString += content[i];
            }
            }
            int number = numberString.toInt(); 
            sv.writeAngle(number);
            Serial.println("["+numberString+"]COMPLETE");
        }else if (content == "forward" or content == "fw")
        {
            mc.forward();
            Serial.println("forwarding!");
        }else if (content == "backforward" or content == "bfw")
        {
            mc.backforward();
            Serial.println("backforwarding!");
        }else if (content == "stop"){
            mc.stop();
            Serial.println("Stop!");
        }else if (content.indexOf("renewTime") != -1 or content.indexOf("rt") != -1)        {
            String numberString = "";
            for (unsigned int i = 0; i < content.length(); i++) {
            if (isdigit(content[i])) {
                numberString += content[i];
            }}
            unsigned int number = numberString.toInt();
            bambuRenewTime = number;
            Serial.println("["+numberString+"]COMPLETE");
        }else if (content.indexOf("ledbright") != -1 or content.indexOf("lb") != -1)        {
            String numberString = "";
            for (unsigned int i = 0; i < content.length(); i++) {
            if (isdigit(content[i])) {
                numberString += content[i];
            }}
            unsigned int number = numberString.toInt();
            ledBrightness = number;
            JsonDocument Cdata = getCData();
            Cdata["ledBrightness"] = ledBrightness;
            writeCData(Cdata);
            Serial.println("["+numberString+"]Modification successful! Brightness will take effect after restart");
        }else if (content == "rgb"){
            Serial.println("RGB Testing......");
            ledAll(255,0,0);
            delay(1000);
            ledAll(0,255,0);
            delay(1000);
            ledAll(0,0,255);
            delay(1000);
        }else if (content == "delet all HomeAssitant device")
        {
            File file = LittleFS.open("/ha.json", "r");
            JsonDocument haData;
            deserializeJson(haData, file);
            JsonArray list = haData["discovery_topic"].as<JsonArray>();
            for (JsonVariant value : list) {
                String topic = value.as<String>();
                haClient.publish(topic.c_str(),"");
                Serial.println("Deleted ["+topic+"]");
            }
        }
    }
}