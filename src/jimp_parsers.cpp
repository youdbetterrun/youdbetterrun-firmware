#include "jimp_parsers.h"

bool parse_server_info(Jimp *jimp, DateTime &serverLocalTime) {
	if (!jimp_object_begin(jimp)) return false;

	while (jimp_object_member(jimp)) {
		if (strcmp(jimp->string, "serverTime") == 0) {
			if (!jimp_string(jimp)) return false;
			serverLocalTime = DateTime(jimp->string);
		} else if (strcmp(jimp->string, "calcTime") == 0) {
			if (!jimp_number(jimp)) return false;
		} else {
			if (!jimp_string(jimp)) return false;
		}
	}

	if (!jimp_object_end(jimp)) return false;

	return true;
}

bool parse_location_properties(Jimp *jimp, char * platform) {
	if (!jimp_object_begin(jimp)) return false;

	while (jimp_object_member(jimp)) {
		if (strcmp(jimp->string, "platform") == 0) {
			if (!jimp_string(jimp)) return false;
			*platform = *jimp->string;
		} else {
			if (!jimp_string(jimp)) return false;
		}
	}

	if (!jimp_object_end(jimp)) return false;

	return true;
}

bool parse_location(Jimp *jimp, char * platform) {
	if (!jimp_object_begin(jimp)) return false;

	while (jimp_object_member(jimp)) {
		if (strcmp(jimp->string, "id") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "isGlobalId") == 0) {
			if (!jimp_bool(jimp)) return false;
		} else if (strcmp(jimp->string, "name") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "disassembledName") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "type") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "pointType") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "coord") == 0) {
			if (!jimp_skip_array(jimp)) return false;
		} else if (strcmp(jimp->string, "properties") == 0) {
			if (!parse_location_properties(jimp, platform)) return false;
		} else if (strcmp(jimp->string, "parent") == 0) {
			if (!jimp_skip_object(jimp)) return false;
		} else {
			jimp_unknown_member(jimp);
			return false;
		}
	}

	if (!jimp_object_end(jimp)) return false;

	return true;
}

bool parse_transportation(Jimp *jimp, int * number) {
	if (!jimp_object_begin(jimp)) return false;

	while (jimp_object_member(jimp)) {
		if (strcmp(jimp->string, "id") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "name") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "disassembledName") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "description") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "number") == 0) {
			if (!jimp_string(jimp)) return false;
			*number = atoi(jimp->string);
		} else if (strcmp(jimp->string, "product") == 0) {
			if (!jimp_skip_object(jimp)) return false;
		} else if (strcmp(jimp->string, "destination") == 0) {
			if (!jimp_skip_object(jimp)) return false;
		} else if (strcmp(jimp->string, "properties") == 0) {
			if (!jimp_skip_object(jimp)) return false;
		} else if (strcmp(jimp->string, "origin") == 0) {
			if (!jimp_skip_object(jimp)) return false;
		} else if (strcmp(jimp->string, "operator") == 0) {
			if (!jimp_skip_object(jimp)) return false;
		} else {
			jimp_unknown_member(jimp);
			return false;
		}
	}

	if (!jimp_object_end(jimp)) return false;
	return true;
}

bool parse_stop_event(Jimp *jimp, DateTime const &nowUtc,
		bool (*stopCallback)(ParsedStopEvent const &, DateTime const &)) {
	ParsedStopEvent result{};
	if (!jimp_object_begin(jimp)) return false;

	while (jimp_object_member(jimp)) {
		if (strcmp(jimp->string, "realtimeStatus") == 0) {
			if (!jimp_skip_array(jimp)) return false;
		} else if (strcmp(jimp->string, "isRealtimeControlled") == 0) {
			if (!jimp_bool(jimp)) return false;
		} else if (strcmp(jimp->string, "location") == 0) {
			if (!parse_location(jimp, &result.platform)) return false;
		} else if (strcmp(jimp->string, "departureTimePlanned") == 0) {
			if (!jimp_string(jimp)) return false;
			result.departureTimePlanned = DateTime(jimp->string);
		} else if (strcmp(jimp->string, "departureTimeEstimated") == 0) {
			if (!jimp_string(jimp)) return false;
			result.hasDepartureTimeEstimated = true;
			result.departureTimeEstimated = DateTime(jimp->string);
		} else if (strcmp(jimp->string, "departureTimeBaseTimetable") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "transportation") == 0) {
			if (!parse_transportation(jimp, &result.number)) return false;
		} else if (strcmp(jimp->string, "properties") == 0) {
			if (!jimp_skip_object(jimp)) return false;
		} else {
			jimp_unknown_member(jimp);
			return false;
		}
	}

	if (!jimp_object_end(jimp)) return false;

	stopCallback(result, nowUtc);

	return true;
}

bool parse_stop_events(Jimp *jimp, DateTime const & nowUtc, bool (*stopCallback)(ParsedStopEvent const &, DateTime const &)) {
	if (!jimp_array_begin(jimp)) return false;

	while (jimp_array_item(jimp)) {
		if (!parse_stop_event(jimp, nowUtc, stopCallback)) return false;
	}

	if (!jimp_array_end(jimp)) return false;

	return true;
}

bool parse_payload(Jimp *jimp, DateTime &serverLocalTime, DateTime const &nowUtc,
		bool (*stopCallback)(ParsedStopEvent const &, DateTime const &)) {
	if (!jimp_object_begin(jimp)) return false;

	while (jimp_object_member(jimp)) {
		if (strcmp(jimp->string, "serverInfo") == 0) {
			if (!parse_server_info(jimp, serverLocalTime)) return false;
		} else if (strcmp(jimp->string, "version") == 0) {
			if (!jimp_string(jimp)) return false;
		} else if (strcmp(jimp->string, "systemMessages") == 0) {
			if (!jimp_array_begin(jimp)) return false;
			if (!jimp_array_end(jimp)) return false;
		} else if (strcmp(jimp->string, "locations") == 0) {
			if (!jimp_skip_array(jimp)) return false;
		} else if (strcmp(jimp->string, "stopEvents") == 0) {
			if (!parse_stop_events(jimp, nowUtc, stopCallback)) return false;
		} else {
			jimp_unknown_member(jimp);
			return false;
		}
	}

	if (!jimp_object_end(jimp)) return false;

	return true;
}
