
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <vector>
#include <M5EPD.h>
#include <ArduinoJson.h>

#include "cert.h"
#include "token.h"
#include "cred.h"

using namespace std;

// sdkconfig.hでログを表示できる
// たぶん直接ファイルを編集するんじゃなくてどこかで設定をいじれるはず
// #define CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL 4

const size_t capacity = 500;
DynamicJsonDocument doc(capacity);
DynamicJsonDocument images(2048);

M5EPD_Canvas canvas(&M5.EPD);
uint8_t pic_i = 0;

String host = "www.googleapis.com";
String dirId = "'your Google Drive Dir ID'";
String access_token = "";

void setup()
{
    M5.begin();
    M5.EPD.SetRotation(90);
    M5.EPD.Clear(true);
    WiFi.begin((char *)SSID.c_str(), (char *)Password.c_str());
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.print("WiFiConnect ");
    Serial.println(WiFi.localIP());

    canvas.createCanvas(540, 960);
    canvas.setTextSize(3);
    canvas.drawString("Hello World", 45, 350);
    canvas.pushCanvas(0, 0, UPDATE_MODE_DU4);
}

String get_access_token(void)
{
    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
        client->setCACert(rootCA);
        {
            HTTPClient https;
            Serial.println("HTTPS post");
            if (https.begin(*client, "https://" + host + "/oauth2/v4/token"))
            {
                String postData = "";
                postData += "refresh_token=" + refresh_token;
                postData += "&client_id=" + client_id;
                postData += "&client_secret=" + client_secret;
                postData += "&grant_type=refresh_token";
                https.addHeader("Content-Type", "application/x-www-form-urlencoded");
                int httpResponseCode = https.POST(postData);
                Serial.println(httpResponseCode);
                String body = https.getString();
                Serial.println(body.length());
                Serial.print(body);
                deserializeJson(doc, body);
                delay(10);
                https.end();
            }
        }
    }
    client->stop();
    String access_token = doc["access_token"];
    Serial.println(access_token);

    return access_token;
}

void drive_get(void)
{
    // access_token = get_access_token();
    // Serial.println(access_token);

    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
        client->setCACert(rootCA);
        {
            HTTPClient https;
            Serial.println("HTTPS GET");
            String postData = "?q=" + dirId + "+in+parents";

            // スペースは+でエスケープする
            // String postData = "?q=mimeType='image/jpeg'";

            if (https.begin(*client, "https://" + host + "/drive/v3/files" + postData))
            {
                https.addHeader("Content-Type", "application/json");
                https.addHeader("Authorization", "Bearer " + access_token);
                int httpResponseCode = https.GET();
                Serial.println(httpResponseCode);
                String body = https.getString();
                Serial.println(body.length());
                Serial.print(body);

                StaticJsonDocument<200> filter;
                filter["files"][0]["id"] = true;
                filter["files"][0]["name"] = true;
                deserializeJson(images, body, DeserializationOption::Filter(filter));
                serializeJsonPretty(images, Serial);

                // doc3["files"][i]["id"];でアクセスしていってnullならreturnとか
                // deserializeJson(images, body);
                int i = 0;
                while (1)
                {
                    String temp_id = images["files"][i]["id"];
                    if (temp_id.equals("null"))
                    {
                        break;
                    }
                    String temp_name = images["files"][i]["name"];
                    Serial.print("id : ");
                    Serial.println(temp_id);
                    Serial.print("name : ");
                    Serial.println(temp_name);
                    i++;
                }

                https.end();
            }
            else
            {
                Serial.println("Connection failed");
            }
        }
    }
    client->stop();
}

int getPic_drive(String image_id, uint8_t *&pic)
{
    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
        client->setCACert(rootCA);
        {
            HTTPClient https;
            Serial.println("HTTPS GET");

            if (https.begin(*client, "https://" + host + "/drive/v3/files/" + image_id + "?alt=media"))
            {
                https.addHeader("Authorization", "Bearer " + access_token);
                int httpResponseCode = https.GET();
                Serial.println(httpResponseCode);
                size_t size = https.getSize();
                Serial.print("Pyload Size ");
                Serial.println(size);
                if (httpResponseCode == HTTP_CODE_OK || httpResponseCode == HTTP_CODE_MOVED_PERMANENTLY)
                {
                    WiFiClient *stream = https.getStreamPtr();
                    pic = (uint8_t *)ps_malloc(size);

                    size_t offset = 0;
                    while (https.connected())
                    {
                        size_t len = stream->available();
                        if (!len)
                        {
                            delay(1);
                            continue;
                        }

                        stream->readBytes(pic + offset, len);
                        offset += len;
                        log_d("%d / %d", offset, size);
                        if (offset == size)
                        {
                            break;
                        }
                    }
                }

                https.end();
                return size;
            }

            else
            {
                Serial.println("Connection failed");
            }
        }
    }
    client->stop();
}

void drawPic_drive(void)
{
    uint8_t *pic = nullptr;
    int size = getPic_drive("imageID", pic);

    Serial.print("Size : ");
    Serial.println(size);

    // M5.Lcd.drawJpg(pic, payload.length());
    canvas.createCanvas(540, 960);
    canvas.setTextSize(3);
    canvas.drawJpg(pic, size);
    // canvas.drawJpgUrl(host + url[i]);

    canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);

    pic_i++;
    if (pic_i == 9)
        pic_i = 0;
    free(pic);
    pic = NULL;
}

int getPic(String url, uint8_t *&pic)
{
    HTTPClient http;
    Serial.println(url);
    Serial.print("[HTTPS] begin...\n");
    // M5.Lcd.println("[HTTPS] begin...");

    if (http.begin(url))
    {
        Serial.print("[HTTPS] GET...\n");
        // M5.Lcd.println("[HTTPS] GET...");
        int httpCode = http.GET();
        Serial.println(httpCode);
        size_t size = http.getSize();
        Serial.print("Pyload Size ");
        Serial.println(size);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
            WiFiClient *stream = http.getStreamPtr();
            pic = (uint8_t *)ps_malloc(size);

            size_t offset = 0;
            while (http.connected())
            {
                size_t len = stream->available();
                if (!len)
                {
                    delay(1);
                    continue;
                }

                stream->readBytes(pic + offset, len);
                offset += len;
                log_d("%d / %d", offset, size);
                if (offset == size)
                {
                    break;
                }
            }

            return size;
            // String payload = http.getString();
            // Serial.println("Content-Length: " + String(payload.length()));
            // Serial.println(payload);
            // M5.Lcd.println(buf);
        }
        else
        {
            Serial.printf("[HTTPS] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
            return 0;
        }
    }
    return 0;
}

void loop()
{

    drive_get();
    // drawPic_drive();

    while (1)
        ;

    // drawPic();
    delay(5000);
}
