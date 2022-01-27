
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <vector>
#include <M5EPD.h>
#include <ArduinoJson.h>
#include <map>
#include <Preferences.h>

#include "cert.h"
#include "token.h"
#include "cred.h"

using namespace std;

std::map<string, string> images;

const size_t capacity = 500;
DynamicJsonDocument doc(capacity);
DynamicJsonDocument filter_body(2048);

M5EPD_Canvas canvas(&M5.EPD);

String host = "www.googleapis.com";
String dirId = "";
String access_token = "";

Preferences prefs;

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

    // set random seed
    randomSeed(analogRead(33));

    // set nvs
    prefs.begin("photo");

    // prefs.putString("imageid", "dummyid");
    // String hoge = prefs.getString("imageid");
    // Serial.println(hoge);
}

string Str2str(String Str)
{
    char buf[40];
    Str.toCharArray(buf, Str.length() + 1);
    return buf;
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

void drive_files(void)
{
    access_token = get_access_token();
    // Serial.println(access_token);

    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
        client->setCACert(rootCA);
        {
            HTTPClient https;
            Serial.println("HTTPS GET");

            String postData = "?q=" + dirId + "+in+parents";

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
                deserializeJson(filter_body, body, DeserializationOption::Filter(filter));
                serializeJsonPretty(filter_body, Serial);

                // deserializeJson(images, body);
                int i = 0;
                while (1)
                {
                    String temp_id = filter_body["files"][i]["id"];
                    String temp_name = filter_body["files"][i]["name"];
                    if (temp_id.equals("null"))
                    {
                        break;
                    }
                    images[Str2str(temp_id)] = Str2str(temp_name);
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

String selectImageID()
{
    String oldID = prefs.getString("imageid");
    // Serial.println(oldID);

    auto it = images.begin();

    do
    {
        advance(it, random(images.size()));
        // Serial.println(it->first.c_str());
    } while (!oldID.compareTo(it->first.c_str()));

    return it->first.c_str();
}

void drawPic_drive(void)
{

    String imageid = selectImageID();
    uint8_t *pic = nullptr;

    int size = getPic_drive(imageid, pic);

    Serial.print("Size : ");
    Serial.println(size);

    canvas.createCanvas(540, 960);
    canvas.setTextSize(3);
    canvas.drawJpg(pic, size);
    // canvas.drawJpgUrl(host + url[i]);

    canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);

    free(pic);
    pic = NULL;

    prefs.putString("imageid", imageid);
    delay(500);
}

int getPic(String url, uint8_t *&pic)
{
    HTTPClient http;
    Serial.println(url);
    Serial.print("[HTTPS] begin...\n");

    if (http.begin(url))
    {
        Serial.print("[HTTPS] GET...\n");
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

    drive_files();
    drawPic_drive();

    esp_sleep_enable_timer_wakeup(2 * 60 * 60 * 1000000);
    Serial.println("Deep Seep Start");
    esp_deep_sleep_start();

}
