#include <TimeLib.h>
#include <DS1307RTC.h>
#include <LiquidCrystal.h>
#include <LCD_Key.h>
#include <Wire.h>
#include <SdFat.h>
//#include <MemoryFree.h>

//define keycode
#define None     0
#define Select   1
#define Left     2
#define Up       3
#define Down     4
#define Right    5

//define fingerprint command code
#define ADD_FINGERPRINT 0x40
#define DELETE_FINGERPRINT 0x42
#define SEARCH_FINGERPRINT 0x44
#define EMPTY_DATABASE 0x46
#define MATCHING 0x4B

//define fingerprint response code
#define OP_SUCCESS 0x31
#define FP_DETECTED 0x32
#define TIME_OUT 0x33
#define PROCESS_FAILED 0x34
#define PARAMETER_ERR 0x35
#define MATCH 0x37
#define NO_MATCH 0x38
#define FP_FOUND 0x39
#define FP_UNFOUND 0x3A

//define hardware pin
#define chipSelect 13
#define buzzer A1

//define some constants
#define pb_delay 200
#define worker_no 100

//data address for SM360
//(worker_no*30+1)- address store the time device opened
//(worker_no*30+1) address store last time log in/out
#define ADDR 30 // with address (worker_id-1)*ADDR + type
#define ID   1
#define NAME 4
#define POS 21
#define STAT 27

//writelog option
#define TIME 0
#define STATUS 1

//initialize library function
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
LCD_Key keypad;

tmElements_t tm;
//define global variables
Sd2Card card;
SdVolume vol;
SdFat sd;

//define other variables
const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
char* Wday[7]={"Sun","Mon","Tues","Weds","Thur","Fri","Sat"};
char* Month[12]={"Jan","Feb","Mar","Apr","May","Jun",
                 "Jul","Aug","Sep","Oct","Nov","Dec"};
char* worker_stat[2]={"OUT","IN"};




// variables for SD
String fileName;
String _info;
String time_info;

// variables for fprint scanning operation
int worker_id = -1;
boolean stat;
//String worker_lvl;

//variables for admin menu and interface
byte choice[2];
byte page_no;
boolean reset_ = false;
boolean time_on=true;
boolean A = false;
boolean key_status = false;

void setup()
{ 
  lcd.begin(16,2);
msg("Booting...");
  //pinMode(53,OUTPUT); // pin 53 always high for SD function
  //pinMode(10,OUTPUT);digitalWrite(10,HIGH);
  pinMode(buzzer,OUTPUT); 

  while(!checkSDCard())continue;

  Wire.begin();
 setSyncProvider(RTC.get);
  
  wr_log(TIME);
  wrFlash(1,worker_no*30+1,String(now()));
     SET_rtc();
  fileName.reserve(15);
  fileName = String(Month[month()-1])+"_"
                        +String(year()%100)+".csv";
}

void loop()
{ 
 
 
   
  if(keypad.getKey()==Select||key_status)
  {

    lcd.clear();
delay(pb_delay);

    worker_id = -1;//
    A = false;//
    key_status = false;
admin();
//    add_fprint();
  }
//  if(keypad.getKey()==Down)
//  {
//   lcd.clear();
//delay(pb_delay);
//    worker_id = -1;//
//    A = false;//
//    key_status = false;
//del_fprint();
//add_fprint();
//  }
  attendance();
  
}

///////////////////////////////FingerPrint Function//////////////////////////////
void _cmd(byte cmd,int number)
{
  if(number>767)
  {
    msg("INVALID ID");
    return;
  }
  
  Serial.begin(57600);
  byte packet_content;
  switch(cmd)
  {
    case SEARCH_FINGERPRINT:packet_content = 5;break;
    case EMPTY_DATABASE:packet_content = 1;break;
    default: packet_content = 3;break;
  }
  
  byte Hbyte = number/256;
  byte Lbyte = number%256;
  byte checksum = 0;
  
  byte send_cmd[packet_content+5];
  
  for(byte i=0;i<sizeof(send_cmd);i++) send_cmd[i]=0;
  
  send_cmd[0] = 0x4D;
  send_cmd[1] = 0x58;
  send_cmd[2] = 0x10;
  send_cmd[3] = packet_content;
  send_cmd[4] = cmd;
  
  for(byte i=0;i<sizeof(send_cmd);i++)
  {
    checksum+=send_cmd[i];
  }

  if(packet_content>=3)
  {
    send_cmd[5] = Hbyte;send_cmd[6] = Lbyte;
    checksum+=send_cmd[5];
    checksum+=send_cmd[6];
    
    if(cmd==SEARCH_FINGERPRINT)
    {   
      for(byte i=7;i>4;i--)
      {
        send_cmd[i+2]=send_cmd[i];
      }
      send_cmd[5] = 0;send_cmd[6] = 0;
      checksum+=send_cmd[5];
      checksum+=send_cmd[6];
    }
  }
  
  send_cmd[packet_content+5-1]=checksum;

  Serial.write(send_cmd,sizeof(send_cmd));
  delay(1);
  
}

boolean _respond(byte cmd)
{
 boolean end_flag = false;
 boolean success = false;
 byte addfp_count = 0;

 while(!end_flag)
 {
    int packet_length = 9;
    byte resp_temp[packet_length];
    
    for(byte j=0;j<packet_length;j++)
    { 
      while(Serial.available()==0)
      {
        if(time_on) Time_Display();
        if(keypad.getKey()==Select)
        {
          key_status = true;
          Serial.end();
          return success;
        }
      }
      
      resp_temp[j]=Serial.read();
      if(j==3) packet_length = resp_temp[3]+5;
      
    }
  
    byte response[packet_length];
    for(byte j=0;j<packet_length;j++) response[j]=resp_temp[j];
    
    worker_id = -1;
    
    //if(response[4]==0x02&&cmd==SEARCH_FINGERPRINT) return 0;//

    switch(response[5])
    {
      case OP_SUCCESS:
      if(cmd==SEARCH_FINGERPRINT) {msg("SEARCHING...");}
      else if(cmd==ADD_FINGERPRINT) msg("ADDING...");
      else if(cmd==DELETE_FINGERPRINT)
      {
        msg("DELETING...");
        end_flag = true;
        success = true;
      }
      else if(cmd==EMPTY_DATABASE)
      {
        end_flag=true;
        success = true;
      }
      
      if(cmd==byte(ADD_FINGERPRINT)) addfp_count++;
      
      break;
      
      case FP_DETECTED:
      msg("FPRINT DETECTED");
      success = true;
      break;
      
      case TIME_OUT:
      //msg("TIME OUT");
      if(time_on)
      {
        lcd.clear();
        lcd.setCursor(5, 0);lcd.print("Cytron");
        lcd.setCursor(2, 1);lcd.print("Technologies");
        delay(1000);lcd.clear();
      }
      if(!time_on) beep(3);
      end_flag=true;
      break;
      
      case PROCESS_FAILED:
      beep(3);
      msg("PROCESS FAILED");
      end_flag=true;
      break;
      
      case PARAMETER_ERR:
      beep(3);
      msg("PARAMETER ERR");
      end_flag=true;
      break;
      
      case MATCH:
      //msg("FPRINT MATCHED");
      success = true;
      end_flag=true;
      break;
      
      case NO_MATCH:
      //msg("FPRINT UNMATCH");
      end_flag=true;
      worker_id = 0;
      break;
      
      case FP_FOUND:
      //msg("FPRINT FOUND");
      
      worker_id = response[6]*256+response[7];
      end_flag=true;
      success = true;
      break;
      
      case FP_UNFOUND:
      if(!A)
      {
        beep(3);
        msg("FPRINT UNFOUND");
      }
      end_flag=true;
      worker_id = -2;
      break;
      
    }

    if(cmd==byte(ADD_FINGERPRINT)&&addfp_count>1)
    {
      success = true;
      end_flag=true;
    }
}

  Serial.end();
  return success;
}

void wrFlash(int id,int type,String data2wr)
{
  Serial.begin(57600);
  char data[data2wr.length()+1];
  data2wr.toCharArray(data,data2wr.length()+1);
  
  if(sizeof(data)>128) {msg("too big");return;}
  
  byte wr_data[sizeof(data)+9];
  byte checksum = 0;
  byte i;
  
  wr_data[0] = 0x4D;
  wr_data[1] = 0x58;
  wr_data[2] = 0x10;
  wr_data[3] = 4 + sizeof(data);
  wr_data[4] = 0x64;
  wr_data[5] = ((id-1)*ADDR + type)/256;
  wr_data[6] = ((id-1)*ADDR + type)%256;
  wr_data[7] = sizeof(data);
  
  for(i = 0;i<sizeof(data);i++) wr_data[i+8] = data[i];
  
  for(i=0;i<(sizeof(wr_data)-1);i++) checksum+= wr_data[i];
  
  wr_data[sizeof(wr_data)-1] = checksum;
  
  Serial.write(wr_data,sizeof(wr_data));
  delay(1);
  
  char rx[6];
  while(Serial.available()==0){}
  Serial.readBytes(rx,6);
  
  if(rx[3]==0x01&&rx[4]==0x02) {msg("RECEIVE ERR");return;}
  
  char ops_[7];
  while(Serial.available()==0){}
  Serial.readBytes(ops_,7);
  
  if(ops_[5]!=OP_SUCCESS) {msg("WR FAILED");return;}
  Serial.end();
  
}

char* rdFlash(int id,int type,byte num)
{ 
  Serial.begin(57600);
  if(num>128) {msg("too big");return 0;}
  
  byte rd_data[9];
  byte checksum = 0;
  byte i;
  
  rd_data[0] = 0x4D;
  rd_data[1] = 0x58;
  rd_data[2] = 0x10;
  rd_data[3] = 0x04;
  rd_data[4] = 0x62;
  rd_data[5] = ((id-1)*ADDR + type)/256;
  rd_data[6] = ((id-1)*ADDR + type)%256;
  rd_data[7] = num;

  for(i=0;i<8;i++) checksum+= rd_data[i];
  
  rd_data[8] = checksum;

  Serial.write(rd_data,sizeof(rd_data));
  delay(1);
  
  char rx[6];
  while(Serial.available()==0)continue;
  Serial.readBytes(rx,6);

  if(rx[3]==0x01&&rx[4]==0x02) {msg("RECEIVE ERR");return 0;}
  
  char ops_[6+num];
  while(Serial.available()==0){}
  Serial.readBytes(ops_,sizeof(ops_));

  char dat[num];
  for(i=0;i<num;i++) dat[i] = ops_[i+5];  
  
  Serial.end();
  return dat;  
}


///////////////////////////////Attendance Function//////////////////////////////
void attendance()
{ 
  _info.reserve(10);
  
  _info = "";
  time_info = "";
  
  _cmd(SEARCH_FINGERPRINT,500);
  _respond(SEARCH_FINGERPRINT);

  if(worker_id>=0)
  {
    stat = String(rdFlash(worker_id,STAT,2)).toInt();
    stat = !stat;
    wrFlash(worker_id,STAT,String(stat));
   
    _info = String(worker_id)+String(",")+
      String(worker_stat[stat])+",";
    //record worker login/out time
    wrFlash(worker_id,(worker_no+1)*30+1,String(now()));
    
    time_info = showTime(now());
    _info = time_info +_info;
    
    record(_info);
    wr_log(STATUS);
  }
  //msg(String(freeMemory()));
   
}
///////////////////////////////Time Function//////////////////////////////
void Time_Display()
{
  lcd.setCursor(0,1);
  
  lcd.print(Wday[weekday()-1]);lcd.print(" ");

  lcd.setCursor(0,0);
 
  printDigits(day());
  lcd.print("-");
  printDigits(month());
  lcd.print("-");
  printDigits(year()%100);
  
  lcd.setCursor(11,0);

  printDigits(hourFormat12());
  lcd.print(":");
  printDigits(minute());
  //lcd.print(":");lcd.print(second());
  
  lcd.setCursor(14,1);
  
  if(isAM()==1){
    lcd.print("AM");
  }
  
  else{
    lcd.print("PM");
  }
  
}

void printDigits(int digits)
{
  if(digits < 10)
    lcd.print('0');
  lcd.print(digits);
}

void setup_Time()
{ 
  byte lcd_hpos[7]={0,3,6,11,14,14};
  byte lcd_vpos[7]={0,0,0,0,0,1};
  int lcd_pos = 0;
  
  int cur_time=millis();
  int prev_time=millis();
  boolean display_stat;
  boolean time_update = 0;
  int localKey;
  
  Time_Display();

  while(1)
  {
    int sec_update = second();
    int min_update = minute();
    int hour_update = hour();
    int day_update = day();
    int month_update = month();
    int year_update = year();
    
    localKey = keypad.getKey();
    
    cur_time = millis();
    if(cur_time-prev_time>400)
    {
      display_stat = !display_stat;
      prev_time = cur_time;
    }
    
    if(display_stat==true)
    {
      lcd.setCursor(lcd_hpos[lcd_pos],lcd_vpos[lcd_pos]);
      lcd.print("  ");
    }
    else
    {
      Time_Display();
    }  
   
    if(localKey==Select)
    {
      if(time_update==true) setTime(hour(),minute(),0,day(),month(),year());
      RTC.set(now());
      msg("UPDATING TIME");
      fileName = String(Month[month()-1])+String("_")+String(year()%100)+String(".csv");
      break;
    }
    
    else if(localKey==Left)
    {
      if(--lcd_pos<0) lcd_pos = 5;
      delay(pb_delay);
    }
    
    else if(localKey==Right)
    {
      if(++lcd_pos>5) lcd_pos = 0;
      delay(pb_delay);
    }
   
    else if(localKey==Up)
    {
      switch(lcd_pos)
      {
        case 0: day_update++; if(day_update>31) day_update=1;break;
        case 1: month_update++; if(month_update>12) month_update =1;break;
        case 2: year_update++; if(year_update>2100) year_update = 2013;break;
        case 3: hour_update++;
                if(isAM()==1){if(hour_update>11) hour_update =0;}
                else if(isPM()==1){if(hour_update>23) hour_update =12;}
                time_update = 1;break;
        case 4: min_update++;if(min_update>59) min_update =0;
                time_update = 1;break;
        case 5: if(isAM()==1)hour_update+=12;else hour_update-=12;break;
      }
      setTime(hour_update,min_update,sec_update,day_update,
                                month_update,year_update);
      Time_Display();
      delay(pb_delay);
    }
      
    else if(localKey==Down)
    {
      switch(lcd_pos)
      {
        case 0: day_update--;if(day_update<1) day_update = 31;break;
        case 1: month_update--;if(month_update<1) month_update = 12;break;
        case 2: year_update--; if(year_update<2013) year_update = 2013;break;
        case 3: hour_update--; 
                if(isAM()==1){if(hour_update<0) hour_update =11;}
                else if(isPM()==1) {if(hour_update<12) hour_update =23;}
                time_update = 1;break;
        case 4: min_update--;if(min_update<0) min_update =59;time_update = 1;break;
        case 5: if(isAM()==1) hour_update+=12; else hour_update-=12;break;
      }
      setTime(hour_update,min_update,second(),day_update,
                                month_update,year_update);
      Time_Display();
      delay(pb_delay);
    }
  }
}  


String showTime(time_t time)
{
  String temp_t;
  temp_t.reserve(23);
  String day_night;
    if(isAM(time)) day_night = "AM";
    else day_night = "PM";
    
  if(day(time)<10) temp_t+="0";
  temp_t += String(day(time))+"/";
  if(month(time)<10) temp_t+="0";
  temp_t += String(month(time))+"/"+String(year(time))+",";
  if(hourFormat12(time)<10) temp_t+="0";
  temp_t+=(String(hourFormat12(time))+":");
  if(minute(time)<10) temp_t+="0";
  temp_t += (String(minute(time))+":");
  if(second(time)<10) temp_t+="0";
  temp_t += (String(second(time))+" ");
  temp_t += day_night+",";
  
  return  temp_t;
}

///////////////////////////////SD Card Function//////////////////////////////
void record(String dataString)
{ 
  lcd.clear();
  lcd.print("STORING...");

  char filename[fileName.length()+1];
  fileName.toCharArray(filename,fileName.length()+1);
  
  //checking card condition,return if faulty
  if (!sd.begin(chipSelect, SPI_HALF_SPEED))
  {
    if(!checkSDCard())
    {
     msg("STORING FAILED");
     while(!checkSDCard()) continue;
    }
  }
  
  boolean title=false;
  if(!sd.exists(filename)) title = true;
  
  // create or open file
  SdFile dataFile;

  if(title==true)
  { 
    //SdFile::dateTimeCallback(dateTime);
    dataFile.open(filename,O_WRITE| O_CREAT| O_APPEND);
    dataFile.println("Date,Time,Worker ID,Status,Remark,");
    title = false;
  }
  
  else dataFile.open(filename, O_WRITE| O_APPEND);

  // if the file isn't open, pop up an error:
  if (!dataFile.isOpen())  
  {
    beep(3);
    msg("ERR ACCESS DATA");
    msg("STORING FAILED");
    return;
  } 
  
  else
  {// if the file is available, write to it:
    dataFile.println(dataString);
    dataFile.close();
//  msg("STORING SUCCESS");
  }
  
  if(stat) beep(1);
  else beep(2);

  lcd.clear();
  lcd.print("ID:");lcd.print(worker_id);lcd.print(" ");
  lcd.print(String(worker_stat[stat]));
  lcd.setCursor(11,0);lcd.print(time_info.substring(11,16));
  lcd.setCursor(0,1);lcd.print(String(rdFlash(worker_id,NAME,10)));
  lcd.setCursor(14,1);lcd.print(time_info.substring(20,22));
  
  delay(2000);lcd.clear();

}

void wr_log(boolean type)
{
   if (!sd.begin(chipSelect, SPI_HALF_SPEED))
  {
    if(!checkSDCard())
    {
      msg("UPDATE FAILED");
      while(!checkSDCard()) continue;
    }
  }
  
  boolean title=false;
  if(!(sd.exists("log.txt"))) title = true;
  
  // create or open file
  SdFile dataFile;
  
  if(title==true)
  { 
    SdFile::dateTimeCallback(dateTime);
    dataFile.open("log.txt",O_RDWR|O_CREAT);
    for(byte i =0;i<worker_no+1;i++)
    {
      dataFile.println("###############");
    }
    title = false;
    dataFile.close();
  }
  
  if(!(dataFile.open("log.txt", O_RDWR)))
  {
    beep(3);
    msg("ERR ACCESS DATA");
    msg("UPDATE FAILED");
    return;
  }
  
  char line[20]; 
  int pos = 0;
  uint32_t cur;
  String data;
  
  while (1) 
  {
    cur = dataFile.curPosition();
    
    if (dataFile.fgets(line, sizeof(line)) < 0)
    {
      msg("Line not found");
      return;
    }
    
    if(pos==0&&type==TIME)
    {
      data = String(now())+",";
      break;
    }
    else if(pos==worker_id&&type==STATUS)
    {
      data = String(stat)+","+String(now())+",";
      break;
    }
    
    pos++;
    
  }
  
  if (!dataFile.seekSet(cur))msg("seekSet");
  dataFile.print(data);
  dataFile.rewind();
  dataFile.close();
  //msg("finish");
}

char* rd_log(boolean type)
{
   if (!sd.begin(chipSelect, SPI_HALF_SPEED))
  {
    if(!checkSDCard())
    {
      msg("READLOG FAILED");
      while(!checkSDCard()) continue;
    }
  }

  SdFile dataFile;
  
  if(!(dataFile.open("log.txt", O_READ)))
  {
    beep(3);
    msg("ERR ACCESS DATA");
    msg("READLOG FAILED");
    return 0;
  }
  
  char line[20];
  int pos=0;
  
  while (1) 
  {
    if (dataFile.fgets(line, sizeof(line)) < 0)
    {
      msg("Line not found");
      return 0;
    }
    
    if(pos==0&&type==TIME)break;
    else if(pos==worker_id&&type==STATUS)break;
    
    pos++;
    
  }
  
  dataFile.close();
  return line;
}

void rdAdmin()
{
  SdFile dataFile;
  if (!sd.begin(chipSelect, SPI_HALF_SPEED))
  {
    if(!checkSDCard())
    {
      msg("RD ADMIN FAIL");
      while(!checkSDCard()) continue;
    }
  }
  
  if (!(dataFile.open("admin.csv", O_READ)||dataFile.open("admin~1.csv", O_READ))) 
  {
    msg("RD ADMIN FAIL");
    return;
  }
  
  char data;
  String worker_data[3];
  int count = 0;
  boolean title_flag = true;
  int line_count = 0;
  
  while ((data = dataFile.read()) >= 0)
  {
    if(data!=','&&int(data)!=13&&int(data)!=10) worker_data[count]+=String(data);
    else if(data==',') count++;
    else if(int(data)==13)
    { 
      dataFile.read();
      
      if(line_count>0)
      {
        wrFlash((worker_data[0].toInt()),ID,worker_data[0]);
        wrFlash((worker_data[0].toInt()),NAME,worker_data[1]);
        wrFlash((worker_data[0].toInt()),POS,worker_data[2]);
        //wrFlash((worker_data[0].toInt()),STAT,"0");
      }
      line_count++;  
      count = 0;
      memset(worker_data,0,sizeof(worker_data));      
    }
  }
  dataFile.close();
}

boolean checkSDCard()
{ 
  boolean SDCard_status = card.init(SPI_HALF_SPEED,chipSelect);

  if (!SDCard_status||!vol.init(&card)) 
  { 
    digitalWrite(buzzer,HIGH);
    msg("CARD UNDETECTED");
    digitalWrite(buzzer,LOW);
    return false;
  }
  
  uint32_t volumesize= vol.blocksPerCluster()*vol.clusterCount()*512;
  uint32_t freespace = 512*vol.freeClusterCount()*vol.blocksPerCluster();
  
  //check free space available (although it is quite impossible to get full memory)
  if(freespace<10000) msg("MEMORY FULL");
  else if(freespace<0.1*volumesize) msg("MEMORY NEAR FULL");
  
  return true;
}

void dateTime(uint16_t* date, uint16_t* time) 
{
  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(year(), month(), day());
  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(hour(), minute(), second());
}

/////////////////////////////Admin & Interface///////////////////////////////
void admin()
{
  int category = 0;
  int option = 0;
  int prev_page = 0;
  int page = 0;
  int localKey;
  
  //if(!password_check()) return;
//  if(!admin_fpcheck()) return;

  while(1)
  { 
    if(reset_)
    {
      category-=1;
      page = prev_page;
      reset_=false;
    }
    
    localKey = keypad.getKey();
    
    switch(localKey)
    {
      case Left:  page--;
                  if(page<0) page = 0;
                  else if(page>page_no-1) page = page_no-1;
                  selection(category,option,page);
                  delay(200);
                  break;
                  
      case Right:  page++;
                   if(page<0) page = 0;
                   else if(page>page_no-1) page = page_no-1;
                   selection(category,option,page);
                   delay(200);
                   break;
  
      case Select: category++; option = page;
                   prev_page = page; page=0;
                   lcd.clear();
                   selection(category,option,page);
                   delay(200);
                   break;
                   
      case Up:   category--;
                   if(category<0)
                   {
                  //category = 0; 
                    lcd.clear();
                    return;
                   }
                   else
                    page = prev_page;
                   lcd.clear();
                   selection(category,option,page);
                   delay(200);
                   break;
      
      case None:   selection(category,option,page);
                   delay(200);
                   break;
    }
  }
}

void selection(int category,int option,int page){

  choice[category] = option;
  
  switch(category)
  {
    case 0:                         
    page_no = 5;//set pages for first category
    menu(page);
    break;
    
    case 1:
    if(choice[category]==0){delay(pb_delay);add_fprint();}      
    else if(choice[category]==1){delay(pb_delay);del_fprint();}
    else if(choice[category]==2){delay(pb_delay);empty_database();}
    else if(choice[category]==3){delay(pb_delay);setup_Time();}
    else if(choice[category]==4){delay(pb_delay);rdAdmin();}
    reset_ = true;
    break;     
}
}

void menu(int page){

 lcd.clear();
 lcd.setCursor(0,0);
 lcd.print("Menu ");
 lcd.print(page+1);lcd.print(":");

  switch(page)
  {
    case 0:  lcd.setCursor(6,1);lcd.print("ADD FPRINT");break;
    case 1:  lcd.setCursor(6,1);lcd.print("DEL FPRINT");break;
    case 2:  lcd.setCursor(2,1);lcd.print("EMPTY DATABASE");break;             
    case 3:  lcd.setCursor(6,1);lcd.print("TIME SETUP");break;
    case 4:  lcd.setCursor(5,1);lcd.print("UPDATE LIST");break; 
  }
}

void add_fprint()
{
  int id=0;
  byte localKey;
  time_on = false;
  A = true;
  
  while(A)
  {
    lcd.setCursor(0,0);
    lcd.print("ADD ID NO:");
  
    while(1)
    {
      localKey = keypad.getKey();
      
      if(localKey==Left)
      {
        id--;
        if(id<0){id=100;}
        delay(pb_delay);
      }
      
      else if(localKey==Right)
      {
        id++;
        if(id>100){id=0;}
        delay(pb_delay);
      }
      
      else if(localKey==Down)
      {
        id+=10;
        if(id>100){id=0;}
        delay(pb_delay);
      }
      
      else if(localKey==Select)
      {
        delay(pb_delay);
        break;
      }
      
      lcd.setCursor(11,0);lcd.print(id);lcd.print("  ");
    }
    
    
    while(1)
    {
      _cmd(MATCHING,id);
      if(_respond(MATCHING))
      {
        beep(3);
        msg("ID CONFLICT");
        break;
      }
      
      else if(worker_id==0) worker_id = -1;
     
      else 
      {
        time_on = true;
        return;
      }
      
      lcd.clear();
      lcd.print("PLACE FINGER");
      lcd.setCursor(0,1);
      lcd.print("ON SCREEN");
      delay(1000);
      
      _cmd(SEARCH_FINGERPRINT,id);
      if(_respond(SEARCH_FINGERPRINT)&&worker_id>=0)
      {
        beep(3);
        msg("FPRINT CONFLICT");
        break;
      }
      
      else if(worker_id==-2)
      {
        A = false;
        break;
      }
      
      else
      {
        time_on = true;
        return;
      }  
    }
  }
 
  _cmd(ADD_FINGERPRINT,id);
  if(_respond(ADD_FINGERPRINT))
  {
    beep(1);
    msg("ADDING SUCCESS");
    wrFlash(ID,NAME,"");
  }

  else
  {
    beep(3);
    msg("ADDING FAILED");
  }
  
  time_on = true;
  
}

void del_fprint()
{
  int id=0;
  byte localKey;
  lcd.print("DEL ID NO:");
  
  while(1)
  {
    localKey = keypad.getKey();
    
    if(localKey==Left)
    {
      id--;
      if(id<0){id=100;}
      delay(pb_delay);
    }
    
    else if(localKey==Right)
    {
      id++;
      if(id>100){id=0;}
      delay(pb_delay);
    }
    
    else if(localKey==Down)
    {
      id+=10;
      if(id>100){id=0;}
      delay(pb_delay);
    }
    
    else if(localKey==Select)
    {
      delay(pb_delay);
      break;
    }
    
    lcd.setCursor(11,0);lcd.print(id);lcd.print("  ");
  }
  time_on = false;
  
  _cmd(DELETE_FINGERPRINT,id);
  if(_respond(DELETE_FINGERPRINT)) 
  {
    beep(1);
    msg("DEL SUCCESS");
    
  }
  else 
  {
    beep(3);
    msg("DEL FAILED");
  }
  
  time_on = true;
}

void empty_database()
{
  int id=0;
  byte localKey;
  lcd.setCursor(0,0);
  lcd.print("EMPTY ALL DATA?");
  
  while(1)
  {
    localKey = keypad.getKey();
    
    if(localKey==Up)
    {
      delay(pb_delay);
      return;
    }
    
    else if(localKey==Select)
    {
      delay(pb_delay);
      time_on = false;
      
      _cmd(EMPTY_DATABASE,0);
      if(_respond(EMPTY_DATABASE)) 
      {
        msg("EMPTY SUCCESS");
        beep(1);
      }
      else 
      {
        beep(3);
        msg("EMPTY FAILED");
      }
      
      time_on = true;
      break;
    }
    
  }
}

boolean admin_fpcheck()
{
  lcd.clear();
  lcd.print("PLACE FINGER");
  lcd.setCursor(0,1);
  lcd.print("ON SCREEN");
  
  delay(1000);
  time_on = false;   

  _cmd(SEARCH_FINGERPRINT,500);
  _respond(SEARCH_FINGERPRINT);
  
  if(worker_id<0)
  {
    msg("Access DENIED");
    time_on = true;
    return false;
  }

  if(!(String(rdFlash(worker_id,POS,6)).equalsIgnoreCase("admin")))
  {
    beep(3);
    msg("Access DENIED");
    time_on = true;
    return false;
  }
  
  beep(1);
  time_on = true;
  return true; 
}

boolean password_check()
{
  byte entry[6];// backup password purpose
  byte password[6] = {3,3,4,2,5,4};// up,up,down,left,right,down
  byte entry_count=0;//
  boolean wrong = false;//
  
  int localKey;
  lcd.setCursor(0,0);lcd.print("PASSWORD:");lcd.setCursor(0,1);
  
  while(1)
  {
    localKey = keypad.getKey();
    
    if(localKey>0)
    {
      delay(pb_delay);
      entry[entry_count]=localKey;
      lcd.print("*");beep(1);
      if(entry[entry_count]!=password[entry_count]) wrong = true;
      entry_count++;
    }
    
    if(entry_count>5)
    { 
      entry_count = 0;
      if(!wrong) return true;
      else
      {
        beep(3);
        msg("Access DENIED");
        return false;
      }
    }
  }
}

///////////////////////////////Message Display//////////////////////////////
void msg(String datastring)
{ 
  lcd.clear();
  lcd.print(datastring);
  delay(1000);
  lcd.clear();
}

///////////////////////////////Sound indicator//////////////////////////////

void beep(byte i)
{
  for(byte j=0;j<i;j++)
  {
    digitalWrite(buzzer,HIGH);
    delay(100);
    digitalWrite(buzzer,LOW);
    delay(100);
  }
}
void SET_rtc() {
}

bool getTime(const char *str)
{
  int Hour, Min, Sec;

  if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3) return false;
  tm.Hour = Hour;
  tm.Minute = Min;
  tm.Second = Sec;
  return true;
}

bool getDate(const char *str)
{
  char Month[12];
  int Day, Year;
  uint8_t monthIndex;

  if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3) return false;
  for (monthIndex = 0; monthIndex < 12; monthIndex++) {
    if (strcmp(Month, monthName[monthIndex]) == 0) break;
  }
  if (monthIndex >= 12) return false;
  tm.Day = Day;
  tm.Month = monthIndex + 1;
  tm.Year = CalendarYrToTm(Year);
  return true;
}



  
