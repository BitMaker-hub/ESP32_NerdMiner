/************************************************
 *  This code was originally forked from Valerio Vaccaro
 *  https://github.com/valerio-vaccaro/HAN
 * **********************************************/

// Wifi
#define WIFI_SSID "YourWifi"
#define WIFI_PASSWORD "Pass"

// Mining
#define THREADS 1
#define MAX_NONCE 1000000
#define ADDRESS "yourBTCAddr" 


// Pool
#define POOL_URL "solo.ckpool.org" //"btc.zsolo.bid" "eu.stratum.slushpool.com"
#define POOL_PORT 3333  //6057 //3333

//Display
#define SC_HEIGHT          128     // screen height
#define SC_WIDTH           160     // screen width
#define SC_YCENTER         64     // screen center Y
#define SC_XCENTER         80     // screen center X
#define HEADER_HEIGHT      25
/******************************************************/
//Display TFT_eSPI requirements
/*
  - Required to modify ST7735_rotation.h library and 
    set 0 offset on column/row when rotation is 1 and display is ST7735 GREENTAB2
       · colstart = 0;//1;
       · rowstart = 0;//2;

  - Added custom configuration Setup300_MiniMiner.h on TFT_eSPI library custom Setups
  - Modified User_Setup_Select.h to call custom library Setup300_MiniMiner.h
  
*/





