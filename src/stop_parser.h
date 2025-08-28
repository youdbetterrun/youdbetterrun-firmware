#ifndef STOP_PARSER
#define STOP_PARSER

#include "jimp.h"
#include "datetime.h"

struct ParsedStopEvent {
	char platform;
	int number;
	bool hasDepartureTimeEstimated;
	DateTime departureTimePlanned;
	DateTime departureTimeEstimated;
};

struct StopParserUserData {
	DateTime &serverLocalTime;
	DateTime const &nowUtc;
	bool (*stopCallback)(ParsedStopEvent const &, DateTime const &);
};

bool parse_stops(Jimp *jimp);

#endif // STOP_PARSER
