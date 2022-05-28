#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "Lib/Free_Fonts.h"
#include "Lib/images.h"
#include "mbedtls/md.h"
#include "configs.h"
#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

long templates = 0;
long hashes = 0;
int halfshares = 0; // increase if blockhash has 16 bits of zeroes
int shares = 0; // increase if blockhash has 32 bits of zeroes
int valids = 0; // increased if blockhash <= target

bool checkHalfShare(unsigned char* hash) {
  bool valid = true;
  for(uint8_t i=31; i>31-2; i--) {
    if(hash[i] != 0) {
      valid = false;
      break;
    }
  }
  if (valid) {
    Serial.print("\thalf share : ");
    for (size_t i = 0; i < 32; i++)
        Serial.printf("%02x ", hash[i]);
    Serial.println();
  }
  return valid;
}

bool checkShare(unsigned char* hash) {
  bool valid = true;
  for(uint8_t i=31; i>31-4; i--) {
    if(hash[i] != 0) {
      valid = false;
      break;
    }
  }
  if (valid) {
    Serial.print("\tshare : ");
    for (size_t i = 0; i < 32; i++)
        Serial.printf("%02x ", hash[i]);
    Serial.println();
  }
  return valid;
}

bool checkValid(unsigned char* hash, unsigned char* target) {
  bool valid = true;
  for(uint8_t i=31; i>=0; i--) {
    if(hash[i] > target[i]) {
      valid = false;
      break;
    } else if (hash[i] < target[i]) {
      valid = true;
      break;
    }
  }
  if (valid) {
    Serial.print("\tvalid : ");
    for (size_t i = 0; i < 32; i++)
        Serial.printf("%02x ", hash[i]);
    Serial.println();
  }
  return valid;
}

uint8_t hex(char ch) {
    uint8_t r = (ch > 57) ? (ch - 55) : (ch - 48);
    return r & 0x0F;
}

int to_byte_array(const char *in, size_t in_size, uint8_t *out) {
    int count = 0;
    if (in_size % 2) {
        while (*in && out) {
            *out = hex(*in++);
            if (!*in)
                return count;
            *out = (*out << 4) | hex(*in++);
            *out++;
            count++;
        }
        return count;
    } else {
        while (*in && out) {
            *out++ = (hex(*in++) << 4) | hex(*in++);
            count++;
        }
        return count;
    }
}

void runWorker(void *name) {
  Serial.printf("\nRunning %s on core %d\n", (char *)name, xPortGetCoreID());
  delay(random(0,2000));
  while(true) { 
    // connect to pool
    WiFiClient client;
    if (!client.connect(POOL_URL, POOL_PORT)) {
      Serial.println("Connection to host failed");
      delay(1000);
      break;
    }

    // get template
    templates++;
    DynamicJsonDocument doc(4 * 1024);
    String payload;
    String line;
    // pool: server connection
    payload = String("{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": []}\n");
    Serial.print("Sending  : "); Serial.println(payload);
    client.print(payload.c_str());
    line  = client.readStringUntil('\n');
    Serial.print("Receiving: "); Serial.println(line);
    deserializeJson(doc, line);
    String sub_details = String((const char*) doc["result"][0][0][1]);
    String extranonce1 = String((const char*) doc["result"][1]);
    int extranonce2_size = doc["result"][2];
    line  = client.readStringUntil('\n');
    deserializeJson(doc, line);
    String method = String((const char*) doc["method"]);
    Serial.print("sub_details: "); Serial.println(sub_details);
    Serial.print("extranonce1: "); Serial.println(extranonce1);
    Serial.print("extranonce2_size: "); Serial.println(extranonce2_size);
    Serial.print("method: "); Serial.println(method);
    if(extranonce1.length() == 0) { Serial.println(">>>>>>>>> Worker aborted"); break; }
    
    // pool: authorize work
    payload = String("{\"params\": [\"") + ADDRESS + String("\", \"password\"], \"id\": 2, \"method\": \"mining.authorize\"}\n");
    Serial.print("Sending  : "); Serial.println(payload);
    client.print(payload.c_str());
    line  = client.readStringUntil('\n');
    Serial.print("Receiving: "); Serial.println(line);
    deserializeJson(doc, line);
    String job_id = String((const char*) doc["params"][0]);
    String prevhash = String((const char*) doc["params"][1]);
    String coinb1 = String((const char*) doc["params"][2]);
    String coinb2 = String((const char*) doc["params"][3]);
    JsonArray merkle_branch = doc["params"][4];
    String version = String((const char*) doc["params"][5]);
    String nbits = String((const char*) doc["params"][6]);
    String ntime = String((const char*) doc["params"][7]);
    bool clean_jobs = doc["params"][8]; //bool
    Serial.print("job_id: "); Serial.println(job_id);
    Serial.print("prevhash: "); Serial.println(prevhash);
    Serial.print("coinb1: "); Serial.println(coinb1);
    Serial.print("coinb2: "); Serial.println(coinb2);
    Serial.print("merkle_branch size: "); Serial.println(merkle_branch.size());
    Serial.print("version: "); Serial.println(version);
    Serial.print("nbits: "); Serial.println(nbits);
    Serial.print("ntime: "); Serial.println(ntime);
    Serial.print("clean_jobs: "); Serial.println(clean_jobs);
    line  = client.readStringUntil('\n');
    deserializeJson(doc, line);
    line  = client.readStringUntil('\n');
    deserializeJson(doc, line);

    // calculate target - target = (nbits[2:]+'00'*(int(nbits[:2],16) - 3)).zfill(64)
    String target = nbits.substring(2);
    int zeros = (int) strtol(nbits.substring(0, 2).c_str(), 0, 16) - 3;
    for (int k=0; k<zeros; k++) {
      target = target + String("00");
    }
    int fill = 64 - target.length();
    for (int k=0; k<fill; k++) {
      target = String("0") + target;
    }
    Serial.print("target: "); Serial.println(target);
    // bytearray target
    uint8_t bytearray_target[32];
    size_t size_target = to_byte_array(target.c_str(), 32, bytearray_target);
    uint8_t buf;
    for (size_t j = 0; j < 16; j++) {
        buf = bytearray_target[j];
        bytearray_target[j] = bytearray_target[size_target - 1 - j];
        bytearray_target[size_target - 1 - j] = buf;
    }

    // get extranonce2 - extranonce2 = hex(random.randint(0,2**32-1))[2:].zfill(2*extranonce2_size)
    uint32_t extranonce2_a_bin = esp_random();
    uint32_t extranonce2_b_bin = esp_random();
    String extranonce2_a = String(extranonce2_a_bin, HEX);
    String extranonce2_b = String(extranonce2_b_bin, HEX);
    uint8_t pad = 8 - extranonce2_a.length();
    for (int k=0; k<pad; k++) {
      extranonce2_a = String("0") + extranonce2_a;
    }
    pad = 8 - extranonce2_b.length();
    for (int k=0; k<pad; k++) {
      extranonce2_b = String("0") + extranonce2_b;
    }
    String extranonce2 = extranonce2_a + extranonce2_b;
    Serial.print("extranonce2: "); Serial.println(extranonce2);

    //get coinbase - coinbase_hash_bin = hashlib.sha256(hashlib.sha256(binascii.unhexlify(coinbase)).digest()).digest()
    String coinbase = coinb1 + extranonce1 + extranonce2 + coinb2;
    Serial.print("coinbase: "); Serial.println(coinbase);
    size_t str_len = coinbase.length()/2;
    uint8_t bytearray[str_len];

    Serial.print("coinbase bytes - size ");
    size_t res = to_byte_array(coinbase.c_str(), str_len*2, bytearray);
    Serial.println(res);
    for (size_t i = 0; i < res; i++)
        Serial.printf("%02x ", bytearray[i]);
    Serial.println("---");

    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);

    byte interResult[32]; // 256 bit
    byte shaResult[32]; // 256 bit
  
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, bytearray, str_len);
    mbedtls_md_finish(&ctx, interResult);

    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, interResult, 32);
    mbedtls_md_finish(&ctx, shaResult);

    Serial.print("coinbase double sha: ");
    for (size_t i = 0; i < 32; i++)
        Serial.printf("%02x ", shaResult[i]);
    Serial.println("");

    byte merkle_result[32];
    // copy coinbase hash
    for (size_t i = 0; i < 32; i++)
      merkle_result[i] = shaResult[i];
    
    byte merkle_concatenated[32 * 2];
    for (size_t k=0; k<merkle_branch.size(); k++) {
        const char* merkle_element = (const char*) merkle_branch[k];
        uint8_t bytearray[32];
        size_t res = to_byte_array(merkle_element, 64, bytearray);

        Serial.print("\tmerkle element    "); Serial.print(k); Serial.print(": "); Serial.println(merkle_element);
        for (size_t i = 0; i < 32; i++) {
          merkle_concatenated[i] = merkle_result[i];
          merkle_concatenated[32 + i] = bytearray[i];
        }
        Serial.print("\tmerkle concatenated: ");
        for (size_t i = 0; i < 64; i++)
            Serial.printf("%02x", merkle_concatenated[i]);
        Serial.println("");
            
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, merkle_concatenated, 64);
        mbedtls_md_finish(&ctx, interResult);

        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, interResult, 32);
        mbedtls_md_finish(&ctx, merkle_result);

        Serial.print("\tmerkle sha         : ");
        for (size_t i = 0; i < 32; i++)
            Serial.printf("%02x", merkle_result[i]);
        Serial.println("");
    }
    // merkle root from merkle_result
    String merkle_root = String("");
    for (int i=0; i<32; i++)
      merkle_root = merkle_root + String(merkle_result[i], HEX);

    // create block header
    uint8_t dest_buff[80];

    // calculate blockheader
    String blockheader = version + prevhash + merkle_root + nbits + ntime + "00000000"; 
    Serial.println("blockheader bytes");
    str_len = blockheader.length()/2;
    uint8_t bytearray_blockheader[str_len];
    Serial.println(str_len);
    res = to_byte_array(blockheader.c_str(), str_len*2, bytearray_blockheader);
    Serial.println(res);
    // reverse version
    uint8_t buff;
    size_t bsize, boffset;
    boffset = 0;
    bsize = 4;
    for (size_t j = boffset; j < boffset + (bsize/2); j++) {
        buff = bytearray_blockheader[j];
        bytearray_blockheader[j] = bytearray_blockheader[2 * boffset + bsize - 1 - j];
        bytearray_blockheader[2 * boffset + bsize - 1 - j] = buff;
    }
    // reverse merkle 
    boffset = 36;
    bsize = 32;
    for (size_t j = boffset; j < boffset + (bsize/2); j++) {
        buff = bytearray_blockheader[j];
        bytearray_blockheader[j] = bytearray_blockheader[2 * boffset + bsize - 1 - j];
        bytearray_blockheader[2 * boffset + bsize - 1 - j] = buff;
    }
    // reverse difficulty
    boffset = 72;
    bsize = 4;
    for (size_t j = boffset; j < boffset + (bsize/2); j++) {
        buff = bytearray_blockheader[j];
        bytearray_blockheader[j] = bytearray_blockheader[2 * boffset + bsize - 1 - j];
        bytearray_blockheader[2 * boffset + bsize - 1 - j] = buff;
    }
    Serial.println("");
    Serial.println("version");
    for (size_t i = 0; i < 4; i++)
        Serial.printf("%02x ", bytearray_blockheader[i]);
    Serial.println("");
    Serial.println("prev hash");
    for (size_t i = 4; i < 4+32; i++)
        Serial.printf("%02x ", bytearray_blockheader[i]);
    Serial.println("");
    Serial.println("merkle root");
    for (size_t i = 36; i < 36+32; i++)
        Serial.printf("%02x ", bytearray_blockheader[i]);
    Serial.println("");
    Serial.println("time");
    for (size_t i = 68; i < 68+4; i++)
        Serial.printf("%02x ", bytearray_blockheader[i]);
    Serial.println("");
    Serial.println("difficulty");
    for (size_t i = 72; i < 72+4; i++)
        Serial.printf("%02x ", bytearray_blockheader[i]);
    Serial.println("");
    Serial.println("nonce");
    for (size_t i = 76; i < 76+4; i++)
        Serial.printf("%02x ", bytearray_blockheader[i]);
    Serial.println("");

    // search a valid nonce
    uint32_t nonce = 0;
    uint32_t startT = micros();
    while(true) {
      bytearray_blockheader[76] = (nonce >> 0) & 0xFF;
      bytearray_blockheader[77] = (nonce >> 8) & 0xFF;
      bytearray_blockheader[78] = (nonce >> 16) & 0xFF;
      bytearray_blockheader[79] = (nonce >> 24) & 0xFF;

      // double sha
      mbedtls_md_starts(&ctx);
      mbedtls_md_update(&ctx, bytearray_blockheader, 80);
      mbedtls_md_finish(&ctx, interResult);

      mbedtls_md_starts(&ctx);
      mbedtls_md_update(&ctx, interResult, 32);
      mbedtls_md_finish(&ctx, shaResult);

      // check if half share
      if(checkHalfShare(shaResult)) {
        Serial.printf("%s on core %d: ", (char *)name, xPortGetCoreID());
        Serial.printf("Half share completed with nonce: %d | 0x%x\n", nonce, nonce);
        halfshares++;
        // check if share
        if(checkShare(shaResult)) {
          Serial.printf("%s on core %d: ", (char *)name, xPortGetCoreID());
          Serial.printf("Share completed with nonce: %d | 0x%x\n", nonce, nonce);
          shares++;
        }
      }
      
      // check if valid header
      if(checkValid(shaResult, bytearray_target)) {
        Serial.printf("%s on core %d: ", (char *)name, xPortGetCoreID());
        Serial.printf("Valid completed with nonce: %d | 0x%x\n", nonce, nonce);
        valids++;
        payload = String("{\"params\": [\"") + ADDRESS + String("\", \"") + job_id + String("\", \"") + extranonce2 + String("\", \"") + ntime + String("\", \"") + nonce +String("\"], \"id\": 1, \"method\": \"mining.submit\"");
        Serial.print("Sending  : "); Serial.println(payload);
        client.print(payload.c_str());
        line  = client.readStringUntil('\n');
        Serial.print("Receiving: "); Serial.println(line);
        // exit 
        nonce = MAX_NONCE;
        break;
      }

      nonce++;
      hashes++;
      // exit 
      if (nonce >= MAX_NONCE) {
        Serial.printf("%s on core %d: ", (char *)name, xPortGetCoreID());
        Serial.printf("Nonce > MAX_NONCE\n");
        break;
      }
    } // exit if found a valid result or nonce > MAX_NONCE
    uint32_t duration = micros() - startT;
    mbedtls_md_free(&ctx);
    // close pool connection
    client.stop();
  }

}

void runMonitor(void *name) {
  unsigned long start = millis();
  while (1) {
    unsigned long elapsed = millis()-start;
    Serial.printf(">>> Completed %d share(s), %d hashes, avg. hashrate %.3f KH/s\n",
      shares, hashes, (1.0*hashes)/elapsed);

    tft.fillRect(0,HEADER_HEIGHT,SC_WIDTH,5,0x5AEB);
    unsigned int progress = ((hashes%MAX_NONCE)*1.0/MAX_NONCE)*SC_WIDTH;
    tft.fillRect(0,HEADER_HEIGHT,progress,5,TFT_GREENYELLOW);

    tft.setFreeFont(GLCD);
    tft.fillRect(SC_XCENTER + 5, HEADER_HEIGHT+5, 80, 60, TFT_BLACK);//Clear header text area
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE); tft.setCursor(0, HEADER_HEIGHT+10);
    tft.print("Avg. hashrate: "); tft.setTextColor(TFT_GREEN); tft.print(String((1.0*hashes)/elapsed)); tft.setTextColor(TFT_WHITE); tft.println(" KH/s");
    tft.print("Running time : "); tft.setTextColor(TFT_GREEN); tft.print(String(elapsed/1000/60)); tft.setTextColor(TFT_WHITE); tft.print(" m "); tft.setTextColor(TFT_GREEN); tft.print(String((elapsed/1000)%60)); tft.setTextColor(TFT_WHITE); tft.println(" s");
    tft.print("Total hashes : "); tft.setTextColor(TFT_GREEN); tft.print(String(hashes/1000000)); tft.setTextColor(TFT_WHITE); tft.println(" M");
    tft.print("Block templ. : "); tft.setTextColor(TFT_YELLOW); tft.println(String(templates)); tft.setTextColor(TFT_WHITE);
    tft.print("Shares 16bits: "); tft.setTextColor(TFT_YELLOW); tft.println(String(halfshares)); tft.setTextColor(TFT_WHITE);
    tft.print("Shares 32bits: "); tft.setTextColor(TFT_YELLOW); tft.println(String(shares)); tft.setTextColor(TFT_WHITE);
    tft.print("Valid blocks : "); tft.setTextColor(TFT_RED); tft.println(String(valids)); tft.setTextColor(TFT_WHITE);
    tft.println("");tft.println("");
    tft.drawLine(0,SC_HEIGHT - 30,SC_WIDTH,SC_HEIGHT - 30,TFT_GREENYELLOW);
    tft.print("Pool: "); tft.setTextColor(TFT_GREENYELLOW); tft.print(POOL_URL); tft.print(":"); tft.println(POOL_PORT); tft.setTextColor(TFT_WHITE);
    tft.print("IP  : "); tft.setTextColor(TFT_GREENYELLOW); tft.println(WiFi.localIP()); tft.setTextColor(TFT_WHITE);
    tft.println("");
    delay(5000);
  }
}

void setup(){
  Serial.begin(115200);
  delay(500);

  // Idle task that would reset WDT never runs, because core 0 gets fully utilized
  disableCore0WDT();

  tft.init(); //Init tft
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.pushImage(SC_XCENTER - (logo_Width/2) , SC_YCENTER - (logo_Height/2), logo_Width, logo_Height, cryptopasivo);
  delay(2000);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.pushImage(SC_XCENTER - (btc_Width/2) , SC_YCENTER - (btc_Width/2), btc_Width, btc_Height, bitcoin);
  delay(2000);
  tft.fillScreen(TFT_BLACK);

  //Print header
  tft.fillRect(0, 0, SC_WIDTH, HEADER_HEIGHT-1, 0xF4C3);//Clear header text area
  tft.setTextDatum(MC_DATUM); //MIDDLE CENTER - MC_DATUM / TOP CENTER - TC_DATUM
  tft.setTextColor(TFT_BLACK);tft.setFreeFont(FMBO9); tft.drawString("NERD SOLOminer", (SC_WIDTH/2), (HEADER_HEIGHT/2) - 1, GFXFF); //drawString(const String& string, int32_t poX, int32_t poY, uint8_t font);
  //tft.setSwapBytes(true);
  //tft.pushImage(0 , 0, btcMini_Width, btcMini_Height, btcMini);
  
  tft.setFreeFont(GLCD);  
  tft.setCursor(0, HEADER_HEIGHT+10);
  tft.setTextColor(TFT_WHITE);
  delay(100);
  tft.println("Nerd SOLOMiner is a solo miner");
  tft.println("on a ESP32."); tft.println(""); tft.setTextColor(TFT_RED);
  tft.println("WARNING: you may have to wait longer than the current age of the universe to find a valid block.");
  tft.println(); tft.println("Check your probability at Solochance.com");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  delay(3000);
  tft.fillRect(0, HEADER_HEIGHT+1, SC_WIDTH, SC_HEIGHT - HEADER_HEIGHT, TFT_BLACK);//Clear header text area
  
  for (size_t i = 0; i < THREADS; i++) {
    char *name = (char*) malloc(32);
    sprintf(name, "Worker[%d]", i);

    // Start mining tasks
    BaseType_t res = xTaskCreate(runWorker, name, 35000, (void*)name, 1, NULL);
    Serial.printf("Starting %s %s!\n", name, res == pdPASS? "successful":"failed");
  }

  // Higher prio monitor task
  xTaskCreate(runMonitor, "Monitor", 5000, NULL, 4, NULL);
}

void loop(){
}
