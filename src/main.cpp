#include <Arduino.h>

#define ENABLE_GxEPD2_GFX 0

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <GxEPD2_BW.h>

#include "datetime.h"
#include "certs.h"
#include "secrets.h"

#include "stop_parser.h"
#define JIMP_IMPLEMENTATION
#include "jimp.h"

#if defined(ESP32) && defined(USE_HSPI_FOR_EPD)
SPIClass hspi(HSPI);
#endif

#if defined (ESP8266)
// #define MAX_DISPLAY_BUFFER_SIZE (81920ul-34000ul-5000ul) // ~34000 base use, change 5000 to your application use
#define MAX_DISPLAY_BUFFER_SIZE (8000ul)
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))
#define PIN_CS SS  // Use the default CS pin (NodeMCU: GPIO15=D8).
#define PIN_DC 0   // NodeMCU: GPIO0=D3
#define PIN_RST 2  // NodeMCU: GPIO2=D4
#define PIN_BUSY 4 // NodeMCU: GPIO4=D2
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display(GxEPD2_DRIVER_CLASS(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));
#undef MAX_DISPLAY_BUFFER_SIZE
#undef MAX_HEIGHT
#endif

BearSSL::WiFiClientSecure client;
static constexpr size_t NUM_STOPS{9};
static constexpr size_t BUF_LEN{30};

struct FormattedPlatform {
	char buffer[NUM_STOPS][BUF_LEN];
	uint32_t count;
};

struct FormattedStops {
	FormattedPlatform platform_a;
	FormattedPlatform platform_e;
} formattedStops{};

Jimp jimp = {0};

HTTPClient http;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

const String host = "fahrtauskunft.avv-augsburg.de";

char errorBuffer[1000];

void printError(char const * const format, ...) {
	va_list args;
	va_start(args, format);
	vsnprintf(errorBuffer, sizeof(errorBuffer), format, args);
	va_end(args);

	static constexpr uint16_t PADDING{10};
	int16_t tbx, tby;
	uint16_t tbw, tbh;
	display.setTextColor(GxEPD_BLACK);
	display.setFont(&FreeMonoBold12pt7b);

	display.getTextBounds(errorBuffer, 0, 0, &tbx, &tby, &tbw, &tbh);
	uint16_t x = PADDING;
	uint16_t y = tbh + PADDING;


	display.setPartialWindow(PADDING, PADDING, tbw, tbh + 2);
	display.firstPage();
	do {
		display.fillScreen(GxEPD_WHITE);
		display.setCursor(x, y);
		display.print(errorBuffer);
	} while (display.nextPage());
}

void printProgress(uint16_t const width) {
	static const uint16_t HEIGHT = 2;
	display.setPartialWindow(0, 0, display.width(), HEIGHT);
	display.firstPage();
	do {
		display.fillScreen(GxEPD_WHITE);
		display.fillRect(0, 0, width, HEIGHT, GxEPD_BLACK);
	} while (display.nextPage());
}

// Print the time in the top right
void printTime(const DateTime &now) {
	static constexpr uint16_t PADDING{10};
	int16_t tbx, tby;
	uint16_t tbw, tbh;
	display.setTextColor(GxEPD_BLACK);
	display.setFont(&FreeMonoBold18pt7b);

	char text[9];
	snprintf(text, sizeof(text), "%02d:%02d", now.hour(), now.minute());

	display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
	uint16_t x = (display.width() - tbw) - tbx - PADDING;
	uint16_t y = tbh + PADDING;

	display.setCursor(x, y);
	display.print(text);
}

bool addStop(ParsedStopEvent const &stop_event, DateTime const &nowUtc) {
	if (stop_event.platform != 'a' && stop_event.platform != 'e') return true;

	FormattedPlatform &platform = stop_event.platform == 'a'
		? formattedStops.platform_a : formattedStops.platform_e;

	if (platform.count == NUM_STOPS) {
		return true;
	}

	// Prefer departureTimeEstimated, fallback to departureTimePlanned.
	DateTime const departureTime = stop_event.hasDepartureTimeEstimated
		? stop_event.departureTimeEstimated : stop_event.departureTimePlanned;

	TimeSpan const diff = departureTime - nowUtc;
	int32_t const seconds = diff.totalseconds();
	int16_t const minutes = seconds / 60;

	Serial.printf("Adding stop   platform: %c   time: %2d:%02d:%02d   number: %3d   minutes: %3d\n",
			stop_event.platform,
			departureTime.hour(), departureTime.minute(), departureTime.second(),
			stop_event.number,
			minutes);

	// Don't show busses more that left more than 2 minutes ago.
	// Show if you just missed one though.
	if (minutes < -2) {
		return true;
	}

	snprintf(platform.buffer[platform.count++], BUF_LEN,
		"%-3d   %3d min", stop_event.number, minutes);

	return true;
}

int fetchStops(DateTime const &nowUtc, DateTime &nowLocal) {
	http.begin(client, host, 443, uri);
	int httpCode = http.GET();

	if (!(httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)) {
		printError("[HTTPS] GET... failed with error code %d, error: %s\n",
				httpCode, http.errorToString(httpCode).c_str());
		http.end();
		return 1;
	}

	Serial.printf("HTTP response size: %d\n", http.getSize());
	Stream &stream = http.getStream();

	memset(&formattedStops, 0, sizeof(formattedStops));

	StopParserUserData stopParserUserData = StopParserUserData{
		.serverLocalTime = nowLocal,
		.nowUtc = nowUtc,
		.stopCallback = addStop,
	};

	jimp_begin(&jimp, stream, &stopParserUserData);

	if (!parse_stops(&jimp)) {
		printError("[JSON] Failed to jimp\n");
		http.end();
		return 1;
	}

	// Round to the closest minute
	if (nowLocal.second() > 30) {
		nowLocal = nowLocal + TimeSpan(60);
	}

	http.end();
	return 0;
}

DateTime getCurrentTime() {
	if (!timeClient.forceUpdate()) {
		Serial.println("Could not update NTP time. Using millis time. Will try again next refresh.");
	}

	DateTime now(timeClient.getEpochTime());
	Serial.print("Got current time: ");
	Serial.println(timeClient.getFormattedTime());
	return now;
}

// Text height plus a bit extra to push the main text away from the clock
static constexpr uint16_t TOP_PADDING{40};
static constexpr uint16_t LINE_SPACING{40};
// +1 for title
static constexpr uint16_t NUM_LINES{NUM_STOPS + 1};
static constexpr uint16_t TEXT_BLOCK_HEIGHT{LINE_SPACING * NUM_LINES};
// Result of display.getTextBounds for this font
static constexpr uint16_t TEXT_BLOCK_WIDTH{270};
static constexpr uint8_t NUM_RETRIES{3};

void refresh() {
	Serial.println();
	Serial.println("Refresh");
	Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());

	// Use NTP to get current UTC time and use API to get current local time.
	// Current UTC time needed because stop events are in UTC.
	// Current local time use to print time in the corner.
	Serial.println("Getting current time...");
	DateTime const nowUtc{getCurrentTime()};
	DateTime nowLocal;

	Serial.println("Fetching stops....");

	for (uint8_t i{0};; i++) {
		if (i == NUM_RETRIES) {
			return;
		}
		if (fetchStops(nowUtc, nowLocal) == 0) {
			break;
		} else {
			Serial.printf("Error during fetchstops (try %d): %s\n", i, errorBuffer);
		}
		delay(1000);
	}

	const uint16_t topPadding =
		(display.height() - TEXT_BLOCK_HEIGHT) / 2u + TOP_PADDING;
	const uint16_t leftPadding = ((display.width() / 2u) - TEXT_BLOCK_WIDTH) / 2u;

	// Print the results
	for (uint8_t i{0}; i < NUM_STOPS; i++) {
		Serial.print("e ");
		Serial.println(formattedStops.platform_e.buffer[i]);
	}
	for (uint8_t i{0}; i < NUM_STOPS; i++) {
		Serial.print("a ");
		Serial.println(formattedStops.platform_a.buffer[i]);
	}

	display.setFullWindow();
	display.firstPage();
	do {
		display.fillScreen(GxEPD_WHITE);
		printTime(nowLocal);
		for (uint8_t i{0}; i < NUM_LINES; i++) {
			display.setCursor(leftPadding, topPadding + i * LINE_SPACING);
			display.print(0 == i ? "  Into city" :
				formattedStops.platform_e.buffer[i - 1]);
		}
		for (uint8_t i{0}; i < NUM_LINES; i++) {
			display.setCursor(
					display.width() / 2u + leftPadding,
					topPadding + i * LINE_SPACING);
			display.print(0 == i ? " Out of city" :
				formattedStops.platform_a.buffer[i - 1]);
		}
	} while (display.nextPage());

	Serial.print("Done refresh at ");
	Serial.println(timeClient.getFormattedTime());
}

void setup() {
	Serial1.begin(115200);
	Serial.begin(115200);
	display.init(0); // default 10ms reset pulse, e.g. for bare panels

	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

	printError("Connecting to WiFi...");

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
	}

	printError("IP Address: %s", WiFi.localIP().toString().c_str());
	Serial.printf("%s\n", errorBuffer);

	client.setFingerprint(fingerprint_fahrtauskunft_avv_augsburg_de);

	timeClient.begin();
	// Stop events are in UTC.
	timeClient.setTimeOffset(0);
}

void loop() {
	refresh();

	// Calculate the number of seconds to wait so we line up on the minute for the
	// next update
	uint32_t msToWait = 60'000 - (timeClient.getSeconds() * 1000);

	// Wait at least 30 seconds.
	if (msToWait < 30'000) {
		msToWait += 60'000;
	}

	// Start 4 seconds early because update takes about 4 seconds
	msToWait -= 4'000;

	uint32_t const startMillis = millis();
	uint32_t const endMillis = millis() + msToWait;
	Serial.printf("Next update in %u ms\n", msToWait);
	Serial.printf("end millis %u ms\n", endMillis);
	Serial.printf("current millis %lu ms\n", millis());
	
	while (millis() < endMillis) {
		Serial.print(".");

		// Remove progress for now. The partial updates were adding artifacts to the
		// full refresh.
		// printProgress(second * display.width() / 60);
		delay(1000);
	}
}
