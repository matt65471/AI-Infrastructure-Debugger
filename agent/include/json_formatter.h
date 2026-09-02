#ifndef JSON_FORMATTER_H
#define JSON_FORMATTER_H

#include "telemetry_collector.h"

#include <string>

std::string format_snapshot_as_json(const TelemetrySnapshot& snapshot);

#endif
