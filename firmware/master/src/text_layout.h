// Pure text logic (no Arduino dependencies) so it can be unit-tested on the host: `pio test -e native`.
#pragma once
#include <string>
#include <vector>

namespace layout {

enum class Align { Left, Center, Right };

Align parseAlign(const std::string& name);  // "left" | "center" | "right" (default left)

// Break a message into display lines of at most `width` characters:
//  - words are kept together where possible
//  - a word longer than a line is split with a '-'
//  - "\n" (real newline, or the two characters backslash + n) forces a new line
std::vector<std::string> splitLines(std::string text, int width);

// Upper-case, pad and truncate one line to exactly `width` characters.
std::string align(const std::string& line, int width, Align align);

// Replace characters that aren't on the flaps with a blank. Returns how many were replaced.
int sanitize(std::string& line);

// "HH:MM" -> minutes after midnight, or -1 if invalid.
int parseHHMM(const std::string& text);

// Quiet hours window check. Handles windows that wrap past midnight (22:00 -> 08:00).
// The end minute is exclusive.
bool inWindow(int nowMinutes, int fromMinutes, int toMinutes);

// Clock text: "06:55", "6:55PM", "05  06:55", "MON 06:55", ...
//   day: "none" | "number" (day of the month) | "weekday" (SUN..SAT)
//   leadingZero: "06:55" / "05" vs "6:55" / "5"
// Day and time are separated by two spaces, or fewer if it wouldn't fit `width` flaps.
std::string clockText(int hour, int minute, int monthDay, int weekDay, bool h24, bool leadingZero,
                      const std::string& day, int width);

// Days left until a unix timestamp, rounded up, never negative (countdown mode).
long daysUntil(long long targetUnix, long long nowUnix);

}  // namespace layout
