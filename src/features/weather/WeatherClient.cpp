#include "WeatherClient.h"
#include "Platform.h"
#include "config.h"
#include <ArduinoJson.h>

#if defined(SMALLTV_ESP8266)
  #include <ESP8266HTTPClient.h>
  #include <WiFiClientSecureBearSSL.h>
#else
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
#endif

static WeatherData s_data;
static uint32_t    s_lastFetch = 0;
static bool        s_forceRefresh = false;
static String      s_city;
static String      s_apiKey;
static bool        s_metric = true;

void weatherInit(const Settings& s) {
  s_city   = s.weather.city;
  s_apiKey = s.weather.apiKey;
  s_metric = s.weather.metric;
  s_data.valid = false;
  s_data.error = false;
  s_lastFetch = 0;
}

void weatherForceRefresh() { s_forceRefresh = true; }

const WeatherData& weatherGet() { return s_data; }

// Build the OWM URL for current weather
static String buildCurrentUrl(const Settings& s) {
  String url = String("http://") + OWM_HOST + OWM_CURRENT_PATH;
  url += "?q=" + s.weather.city;
  url += "&appid=" + s.weather.apiKey;
  url += s.weather.metric ? "&units=metric" : "&units=imperial";
  return url;
}

// Build the OWM URL for 5-day/3-hour forecast
static String buildForecastUrl(const Settings& s) {
  String url = String("http://") + OWM_HOST + OWM_FORECAST_PATH;
  url += "?q=" + s.weather.city;
  url += "&appid=" + s.weather.apiKey;
  url += s.weather.metric ? "&units=metric" : "&units=imperial";
  url += "&cnt=24";  // 3 days * 8 entries/day
  return url;
}

static bool fetchCurrent(const Settings& s) {
  WiFiClient client;
  HTTPClient http;
  String url = buildCurrentUrl(s);

  http.setTimeout(s.httpTimeout);
  http.setUserAgent(OWM_USER_AGENT);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  s_data.temp      = doc["main"]["temp"].as<float>();
  s_data.feelsLike = doc["main"]["feels_like"].as<float>();
  s_data.pressure  = doc["main"]["pressure"].as<float>();
  s_data.humidity  = doc["main"]["humidity"].as<int>();
  s_data.windSpeed = doc["wind"]["speed"].as<float>();

  JsonObject weather0 = doc["weather"][0];
  s_data.conditionId = weather0["id"].as<int>();
  strlcpy(s_data.description, weather0["description"] | "?", sizeof(s_data.description));
  strlcpy(s_data.condMain, weather0["main"] | "?", sizeof(s_data.condMain));
  strlcpy(s_data.city, doc["name"] | "?", sizeof(s_data.city));

  // Sunrise/sunset (UTC unix timestamps → local H:M via timezone offset)
  long tzOffset = doc["timezone"].as<long>();  // seconds offset from UTC
  long sunrise = doc["sys"]["sunrise"].as<long>() + tzOffset;
  long sunset  = doc["sys"]["sunset"].as<long>() + tzOffset;
  s_data.sunriseH = (uint8_t)((sunrise % 86400) / 3600);
  s_data.sunriseM = (uint8_t)((sunrise % 3600) / 60);
  s_data.sunsetH  = (uint8_t)((sunset % 86400) / 3600);
  s_data.sunsetM  = (uint8_t)((sunset % 3600) / 60);

  return true;
}

// Parse forecast: extract min/max per day from the 3-hourly data
static bool fetchForecast(const Settings& s) {
  WiFiClient client;
  HTTPClient http;
  String url = buildForecastUrl(s);

  http.setTimeout(s.httpTimeout);
  http.setUserAgent(OWM_USER_AGENT);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  // Use JSON filter to reduce heap usage — only parse fields we need
  JsonDocument filter;
  filter["city"]["timezone"] = true;
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp_min"] = true;
  filter["list"][0]["main"]["temp_max"] = true;
  filter["list"][0]["weather"][0]["id"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) return false;

  long tzOffset = doc["city"]["timezone"] | 0L;  // seconds from UTC
  JsonArray list = doc["list"];
  s_data.forecastCount = 0;

  int prevDay = -1;
  for (JsonObject entry : list) {
    long dt = entry["dt"].as<long>();
    // Apply city timezone offset for correct day grouping
    time_t ts = (time_t)(dt + tzOffset);
    struct tm t;
    gmtime_r(&ts, &t);  // treat adjusted time as UTC to get local day
    int dayNum = t.tm_yday;

    if (dayNum != prevDay) {
      if (s_data.forecastCount >= WEATHER_FORECAST_DAYS) break;
      prevDay = dayNum;
      ForecastDay& fd = s_data.forecast[s_data.forecastCount];
      fd.tempMin = entry["main"]["temp_min"].as<float>();
      fd.tempMax = entry["main"]["temp_max"].as<float>();
      fd.conditionId = entry["weather"][0]["id"].as<int>();
      static const char* dows[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      strlcpy(fd.dow, dows[t.tm_wday], sizeof(fd.dow));
      s_data.forecastCount++;
    } else {
      // Update min/max for the current day
      if (s_data.forecastCount > 0) {
        ForecastDay& fd = s_data.forecast[s_data.forecastCount - 1];
        float tmin = entry["main"]["temp_min"].as<float>();
        float tmax = entry["main"]["temp_max"].as<float>();
        if (tmin < fd.tempMin) fd.tempMin = tmin;
        if (tmax > fd.tempMax) fd.tempMax = tmax;
      }
    }
  }
  return s_data.forecastCount > 0;
}

void weatherService(const Settings& s) {
  if (s.weather.apiKey.length() < 8 || s.weather.city.length() < 2) {
    s_data.valid = false;
    s_data.error = true;
    return;
  }

  uint32_t now = millis();
  uint32_t interval = (uint32_t)s.weather.pollSec * 1000UL;
  if (!s_forceRefresh && s_lastFetch != 0 && (now - s_lastFetch) < interval) return;
  s_forceRefresh = false;
  s_lastFetch = now;

  bool ok = fetchCurrent(s);
  if (ok) fetchForecast(s);

  s_data.valid = ok;
  s_data.error = !ok;
  if (ok) s_data.lastOkMs = now;
}
