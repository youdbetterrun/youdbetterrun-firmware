#ifndef JIMP_PARSERS
#define JIMP_PARSERS

#include "jimp.h"
#include "datetime.h"

struct ParsedStopEvent {
	char platform;
	int number;
	bool hasDepartureTimeEstimated;
	DateTime departureTimePlanned;
	DateTime departureTimeEstimated;
};

bool parse_payload(Jimp *jimp, DateTime &serverLocalTime, DateTime const &nowUtc,
		bool (*stopCallback)(ParsedStopEvent const &, DateTime const &));

#endif // JIMP_PARSERS
