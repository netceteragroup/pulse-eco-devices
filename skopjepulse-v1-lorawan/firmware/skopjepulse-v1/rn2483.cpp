/*
 * A library for controlling a Microchip RN2483 LoRa radio.
 *
 * @Author JP Meijers
 * @Date 18/12/2015
 * @ModifiedBy hsilomedus
 *
 */

#include "Arduino.h"
#include "rn2483.h"

extern "C" {
#include <string.h>
#include <stdlib.h>
}

//#define DEBUG_PROFILE 1
//#ifdef DEBUG_PROFILE
  #define SH_DEBUG_PRINTLN(a) Serial.println(a)
  #define SH_DEBUG_PRINT(a) Serial.print(a)
  #define SH_DEBUG_PRINT_DEC(a,b) Serial.print(a,b)
  #define SH_DEBUG_PRINTLN_DEC(a,b) Serial.println(a,b)
//#else
//  #define SH_DEBUG_PRINTLN(a) 
//  #define SH_DEBUG_PRINT(a) 
//  #define SH_DEBUG_PRINT_DEC(a,b) 
//  #define SH_DEBUG_PRINTLN_DEC(a,b) 
//#endif

/*
  @param serial Needs to be an already opened stream to write to and read from.
*/
rn2483::rn2483(SoftwareSerial& serial):
_serial(serial)
{
  _serial.setTimeout(2000);
}
//
//rn2483::rn2483(HardwareSerial& serial):
//_serial(serial)
//{
//  _serial.setTimeout(2000);
//}

void rn2483::autobaud()
{
  String response = "";
  while (response=="")
  {
    delay(1000);
    _serial.write((byte)0x00);
    _serial.write(0x55);
    _serial.println();
    _serial.println("sys get ver");
    response = _serial.readStringUntil('\n');
  }
}

String rn2483::hweui()
{
  //clear serial buffer
  while(_serial.read() != -1);

  _serial.println("sys get hweui");
  String addr = _serial.readStringUntil('\n');
  addr.trim();
  return addr;
}

String rn2483::sysver()
{
  //clear serial buffer
  while(_serial.read() != -1);

  _serial.println("sys get ver");
  String ver = _serial.readStringUntil('\n');
  ver.trim();
  return ver;
}

void rn2483::init()
{
  if(*_appeui=="0")
  {
    return;
  }
  else if(_otaa==true)
  {
//    init(_appeui, _appskey);
  }
  else
  {
    init(_appeui, _nwkskey, _appskey, _devAddr, _dataRate);
  }
}
//
//void rn2483::init(String AppEUI, String AppKey)
//{
//  _otaa = true;
//  _appeui = AppEUI;
//  _nwkskey = "0";
//  _appskey = AppKey; //reuse the variable
//
//  //clear serial buffer
//  while(_serial.read() != -1);
//
//  _serial.println("sys get hweui");
//  String addr = _serial.readStringUntil('\n');
//  addr.trim();
//
//  _serial.println("mac reset 868");
//  String receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT(receivedData);
//
//  _serial.println("mac set appeui "+_appeui);
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT(receivedData);
//
//  _serial.println("mac set appkey "+_appskey);
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT(receivedData);
//
//  if(addr!="" && addr.length() == 16)
//  {
//    _serial.println("mac set deveui "+addr);
//  }
//  else
//  {
//    _serial.println("mac set deveui "+_default_deveui);
//  }
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT(receivedData);
//
//  _serial.println("mac set pwridx 1");
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT(receivedData);
//
//  _serial.println("mac set adr off");
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT(receivedData);
////
////  _serial.println("mac set dr 0");
////  receivedData = _serial.readStringUntil('\n');
////  SH_DEBUG_PRINT(receivedData);
//
//  _serial.println("mac set rx2 3 869525000");
////  _serial.println("mac set rx2 0 869525000");
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT(receivedData);
//
//  // _serial.println("mac set retx 10");
//  // _serial.readStringUntil('\n');
//  // _serial.println("mac set linkchk 60");
//  // _serial.readStringUntil('\n');
//  // _serial.println("mac set ar on");
//  // _serial.readStringUntil('\n');
//  _serial.setTimeout(30000);
//  _serial.println("mac save");
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINTLN(receivedData);
//
//  _serial.println("mac get dr");
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT("DR: ");
//  SH_DEBUG_PRINTLN(receivedData);
//
//  _serial.println("radio get sf");
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT("SF: ");
//  SH_DEBUG_PRINTLN(receivedData);
//
//  _serial.println("mac get pwridx");
//  receivedData = _serial.readStringUntil('\n');
//  SH_DEBUG_PRINT("PWRIDX: ");
//  SH_DEBUG_PRINTLN(receivedData);
//
//
//  bool joined = false;
//
//  for(int i=0; i<10 && !joined; i++)
//  {
//    _serial.println("mac join otaa");
//    receivedData = _serial.readStringUntil('\n');
//    SH_DEBUG_PRINT(receivedData);
//    receivedData = _serial.readStringUntil('\n');
//    SH_DEBUG_PRINT(receivedData);
//
//    if(receivedData.startsWith("accepted"))
//    {
//      joined=true;
//      delay(1000);
//    }
//    else
//    {
//      delay(1000);
//    }
//  }
//  _serial.setTimeout(2000);
//}

char readBuf[21];

void rn2483::init(String* AppEUI, String* NwkSKey, String* AppSKey, String* addr, int dataRate)
{
  _otaa = false;
  _appeui = AppEUI;
  _nwkskey = NwkSKey;
  _appskey = AppSKey;
  _devAddr = addr;
  _dataRate = dataRate;

  //clear serial buffer
  while(_serial.read() != -1);

  String RN2483 = "RN2483 ";

  readBuf[20] = 0;
  _serial.setTimeout(3000);

  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("reset");
  _serial.println("mac reset 868");
  readreplyResponse();

  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("set rx2");
  _serial.println("mac set rx2 3 869525000");
  readreplyResponse();

  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("set devaddr");
  _serial.print("mac set devaddr ");
  _serial.println(*_devAddr);
  readreplyResponse();

  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("set appskey");
  _serial.print("mac set appskey ");
  _serial.println(*_appskey);
  readreplyResponse();

  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("set nwskey");
  _serial.print("mac set nwkskey ");
  _serial.println(*_nwkskey);
  readreplyResponse();
    

  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("set adr off");
  _serial.println("mac set adr off");
  readreplyResponse();
  
  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("set ar off");
  _serial.println("mac set ar off");
  readreplyResponse();

  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("set pwridx 1");
  _serial.println("mac set pwridx 1"); //1=max, 5=min
  readreplyResponse();
  
  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINT("set dr ");
  SH_DEBUG_PRINTLN_DEC(_dataRate, DEC);
  _serial.print("mac set dr ");
  _serial.println(_dataRate, DEC); //0= min, 7=max
  readreplyResponse();

  _serial.setTimeout(60000);
  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("save");
  _serial.println("mac save");
  readreplyResponse();
  
  SH_DEBUG_PRINT(RN2483);
  SH_DEBUG_PRINTLN("join abp");
  _serial.println("mac join abp");
  readreplyResponse();
  readreplyResponse();
}

void rn2483::readreplyResponse() {
  int read = _serial.readBytesUntil('\n', readBuf, 20);
  //#ifdef DEBUG_PROFILE
  Serial.write(readBuf, read); SH_DEBUG_PRINTLN("");
  //#endif
  delay(100);
}

void rn2483::tx(byte bytes[], int length)
{
  txUncnf(bytes, length); //we are unsure which mode we're in. Better not to wait for acks.
}

//void rn2483::txCnf(byte bytes[], int length)
//{
//  txData("mac tx cnf 1 ", bytes, length);
//}

void rn2483::txUncnf(byte bytes[], int length)
{
  txData("mac tx uncnf 1 ", bytes, length);
}

bool rn2483::txData(String command, byte bytes[], int length)
{
  bool send_success = false;
  uint8_t busy_count = 0;
  uint8_t retry_count = 0;

  while(!send_success)
  {
    //retransmit a maximum of 10 times
    retry_count++;
    if(retry_count>10)
    {
      return false;
    }

    _serial.print(command);
    sendEncoded(bytes, length);
    _serial.println();
    String receivedData = _serial.readStringUntil('\n');
    SH_DEBUG_PRINTLN(receivedData);
    if(receivedData.startsWith("ok"))
    {
      _serial.setTimeout(30000);
      receivedData = _serial.readStringUntil('\n');
      SH_DEBUG_PRINTLN(receivedData);
      _serial.setTimeout(2000);

      if(receivedData.startsWith("mac_tx_ok"))
      {
        //SUCCESS!!
        send_success = true;
        return true;
      }

      else if(receivedData.startsWith("mac_rx"))
      {
        //we received data downstream
        //TODO: handle received data
        send_success = true;
        return true;
      }

      else if(receivedData.startsWith("mac_err"))
      {
        init();
      }

      else if(receivedData.startsWith("invalid_data_len"))
      {
        //this should never happen if the prototype worked
        send_success = true;
        return false;
      }

      else if(receivedData.startsWith("radio_tx_ok"))
      {
        //SUCCESS!!
        send_success = true;
        return true;
      }

      else if(receivedData.startsWith("radio_err"))
      {
        //This should never happen. If it does, something major is wrong.
        init();
      }

      else
      {
        //unknown response
        //init();
      }
    }

    else if(receivedData.startsWith("invalid_param"))
    {
      //should not happen if we typed the commands correctly
      send_success = true;
      return false;
    }

    else if(receivedData.startsWith("not_joined"))
    {
      init();
    }

    else if(receivedData.startsWith("no_free_ch"))
    {
      //retry
      delay(1000);
    }

    else if(receivedData.startsWith("silent"))
    {
      init();
    }

    else if(receivedData.startsWith("frame_counter_err_rejoin_needed"))
    {
      init();
    }

    else if(receivedData.startsWith("busy"))
    {
      busy_count++;

      if(busy_count>=10)
      {
        init();
      }
      else
      {
        delay(1000);
      }
    }

    else if(receivedData.startsWith("mac_paused"))
    {
      init();
    }

    else if(receivedData.startsWith("invalid_data_len"))
    {
      //should not happen if the prototype worked
      send_success = true;
      return false;
    }

    else
    {
      //unknown response after mac tx command
      init();
    }
  }

  return false; //should never reach this
}

void rn2483::sendEncoded(byte bytes[], int length)
{
//  char working;
  char buffer[3];
  for(int i=0; i<length; i++)
  {
//    working = input.charAt(i);
    sprintf(buffer, "%02x", int(bytes[i]));
    _serial.print(buffer);
  }
}
//
//String rn2483::base16encode(String input)
//{
//  char charsOut[input.length()*2+1];
//  char charsIn[input.length()+1];
//  input.trim();
//  input.toCharArray(charsIn, input.length()+1);
//
//  int i = 0;
//  for(i = 0; i<input.length()+1; i++)
//  {
//    if(charsIn[i] == '\0') break;
//
//    int value = int(charsIn[i]);
//
//    char buffer[3];
//    sprintf(buffer, "%02x", value);
//    charsOut[2*i] = buffer[0];
//    charsOut[2*i+1] = buffer[1];
//  }
//  charsOut[2*i] = '\0';
//  String toReturn = String(charsOut);
//  return toReturn;
//}
//
//String rn2483::base16decode(String input)
//{
//  char charsIn[input.length()+1];
//  char charsOut[input.length()/2+1];
//  input.trim();
//  input.toCharArray(charsIn, input.length()+1);
//
//  int i = 0;
//  for(i = 0; i<input.length()/2+1; i++)
//  {
//    if(charsIn[i*2] == '\0') break;
//    if(charsIn[i*2+1] == '\0') break;
//
//    char toDo[2];
//    toDo[0] = charsIn[i*2];
//    toDo[1] = charsIn[i*2+1];
//    int out = strtoul(toDo, 0, 16);
//
//    if(out<128)
//    {
//      charsOut[i] = char(out);
//    }
//  }
//  charsOut[i] = '\0';
//  return charsOut;
//}

