// Host tests: pio test -e native
#include <unity.h>

#include "text_layout.cpp"  // pure C++, compiled directly into the test binary
#include "popup_rules.h"
#include "content.h"

using namespace layout;

void setUp() {}
void tearDown() {}

static void assertLines(const std::vector<std::string>& expected, const std::vector<std::string>& actual) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(expected.size(), actual.size(), "line count");
  for (size_t i = 0; i < expected.size(); i++) TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), actual[i].c_str());
}

void test_short_text_is_one_line() { assertLines({"HELLO"}, splitLines("HELLO", 10)); }

void test_words_wrap() { assertLines({"HELLO", "WORLD"}, splitLines("HELLO WORLD", 10)); }

void test_words_pack_onto_line() { assertLines({"HI THERE", "FRIEND"}, splitLines("HI THERE FRIEND", 10)); }

void test_long_word_hyphenates() { assertLines({"SUPERCALI-", "FRAGILIST-", "IC"}, splitLines("SUPERCALIFRAGILISTIC", 10)); }

void test_newlines_force_breaks() {
  assertLines({"A", "B"}, splitLines("A\nB", 10));
  assertLines({"A", "B"}, splitLines("A\\nB", 10));
  assertLines({"A", "B"}, splitLines("A \n\n B", 10));
}

void test_empty_text_is_one_blank_line() { assertLines({""}, splitLines("   ", 10)); }

void test_exact_width() { assertLines({"0123456789"}, splitLines("0123456789", 10)); }

void test_align() {
  TEST_ASSERT_EQUAL_STRING("HI        ", align("hi", 10, Align::Left).c_str());
  TEST_ASSERT_EQUAL_STRING("        HI", align("hi", 10, Align::Right).c_str());
  TEST_ASSERT_EQUAL_STRING("    HI    ", align("hi", 10, Align::Center).c_str());
  TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJ", align("abcdefghijKL", 10, Align::Left).c_str());
}

void test_sanitize() {
  std::string line = "A@B";
  TEST_ASSERT_EQUAL_INT(1, sanitize(line));
  TEST_ASSERT_EQUAL_STRING("A B", line.c_str());
}

void test_letter_index() {
  TEST_ASSERT_EQUAL_INT(0, unitproto::letterIndex(' '));
  TEST_ASSERT_EQUAL_INT(1, unitproto::letterIndex('a'));
  TEST_ASSERT_EQUAL_INT(30, unitproto::letterIndex('0'));
  TEST_ASSERT_EQUAL_INT(44, unitproto::letterIndex('!'));
  TEST_ASSERT_EQUAL_INT(-1, unitproto::letterIndex('@'));
}

void test_rpm_mapping_matches_original() {
  TEST_ASSERT_EQUAL_INT(1, unitproto::rpmFromSpeed(1));
  TEST_ASSERT_EQUAL_INT(9, unitproto::rpmFromSpeed(80));  // what the old firmware sent for the default
  TEST_ASSERT_EQUAL_INT(12, unitproto::rpmFromSpeed(100));
}

void test_quiet_hours() {
  TEST_ASSERT_EQUAL_INT(22 * 60, parseHHMM("22:00"));
  TEST_ASSERT_EQUAL_INT(-1, parseHHMM("25:00"));
  TEST_ASSERT_EQUAL_INT(-1, parseHHMM("abc"));
  // 22:00 -> 08:00 wraps past midnight
  TEST_ASSERT_TRUE(inWindow(23 * 60, 22 * 60, 8 * 60));
  TEST_ASSERT_TRUE(inWindow(3 * 60, 22 * 60, 8 * 60));
  TEST_ASSERT_FALSE(inWindow(8 * 60, 22 * 60, 8 * 60));
  TEST_ASSERT_FALSE(inWindow(12 * 60, 22 * 60, 8 * 60));
  // 09:00 -> 17:00 same day
  TEST_ASSERT_TRUE(inWindow(9 * 60, 9 * 60, 17 * 60));
  TEST_ASSERT_FALSE(inWindow(17 * 60, 9 * 60, 17 * 60));
  TEST_ASSERT_FALSE(inWindow(5, 60, 60));
}

void test_days_until() {
  TEST_ASSERT_EQUAL_INT(0, daysUntil(1000, 2000));
  TEST_ASSERT_EQUAL_INT(1, daysUntil(86400, 1));
  TEST_ASSERT_EQUAL_INT(2, daysUntil(86400 * 2, 0));
}

void test_param_table() {
  for (int i = 0; i < unitproto::P_COUNT; i++) {
    unitproto::ParamLimits l = unitproto::paramLimits(i);
    TEST_ASSERT_TRUE_MESSAGE(l.min <= l.def && l.def <= l.max, unitproto::PARAM_NAMES[i]);
    TEST_ASSERT_NOT_NULL(unitproto::PARAM_NAMES[i]);
    TEST_ASSERT_NOT_NULL(unitproto::PARAM_HELP[i]);
    TEST_ASSERT_EQUAL_INT(i, unitproto::paramFromName(unitproto::PARAM_NAMES[i]));
  }
  TEST_ASSERT_EQUAL_INT(unitproto::P_LED, unitproto::paramFromName("LED"));
  TEST_ASSERT_EQUAL_INT(-1, unitproto::paramFromName("nope"));
  TEST_ASSERT_EQUAL_INT(32, (int)sizeof(unitproto::Info));
}

void test_popup_priorities() {
  using namespace popup;
  // Over the base mode
  TEST_ASSERT_TRUE(admits(PRIO_HIGH, false, 0, 0));
  TEST_ASSERT_TRUE(admits(PRIO_NORMAL, false, 0, 0));
  TEST_ASSERT_FALSE(admits(PRIO_LOW, false, 0, 1000));      // a pop-up just ended
  TEST_ASSERT_TRUE(admits(PRIO_LOW, false, 0, LOW_GAP_MS));
  // Over another pop-up
  TEST_ASSERT_TRUE(admits(PRIO_HIGH, true, PRIO_NORMAL, 0));     // PRINTY DONE over BART
  TEST_ASSERT_TRUE(admits(PRIO_HIGH, true, PRIO_LOW, 0));
  TEST_ASSERT_FALSE(admits(PRIO_HIGH, true, PRIO_HIGH, 0));      // equal never interrupts
  TEST_ASSERT_FALSE(admits(PRIO_NORMAL, true, PRIO_LOW, 0));     // normal only over the base mode
  TEST_ASSERT_FALSE(admits(PRIO_NORMAL, true, PRIO_NORMAL, 0));
  TEST_ASSERT_FALSE(admits(PRIO_LOW, true, PRIO_LOW, LOW_GAP_MS));
  TEST_ASSERT_EQUAL_INT(PRIO_HIGH, parsePriority("high"));
  TEST_ASSERT_EQUAL_INT(PRIO_NORMAL, parsePriority("whatever"));
}

void test_popup_windows() {
  using namespace popup;
  TEST_ASSERT_TRUE(dayMatches(0x7F, 3));
  TEST_ASSERT_TRUE(dayMatches(0x3E, 1));               // Mon-Fri
  TEST_ASSERT_FALSE(dayMatches(0x3E, 0));              // not Sunday
  TEST_ASSERT_TRUE(windowMatches("", "", 3 * 60));
  TEST_ASSERT_TRUE(windowMatches("07:00", "09:00", 7 * 60 + 30));
  TEST_ASSERT_FALSE(windowMatches("07:00", "09:00", 9 * 60));
  TEST_ASSERT_TRUE(windowMatches("22:00", "02:00", 1 * 60));
}

// Every built-in station / number name must fit on 10 flaps and use only flap characters
static void assertFlapText(const char* text) {
  TEST_ASSERT_TRUE_MESSAGE(strlen(text) <= 10, text);
  for (const char* c = text; *c; c++) TEST_ASSERT_TRUE_MESSAGE(unitproto::letterIndex(*c) >= 0, text);
}

void test_content_fits_the_flaps() {
  for (uint8_t i = 0; i < content::RANDOM_STATION_COUNT; i++) assertFlapText(content::RANDOM_STATIONS[i]);
  for (uint8_t l = 0; l < content::TRAIN_LINE_COUNT; l++) {
    const content::TrainLine* line = content::TRAIN_LINES[l];
    TEST_ASSERT_TRUE(line->count >= 2);
    for (uint8_t i = 0; i < line->count; i++) assertFlapText(line->stops[i]);
  }
  for (uint8_t i = 0; i < content::NUMBER_COUNT; i++) {
    assertFlapText(content::NUMBERS[i][0]);
    assertFlapText(content::NUMBERS[i][1]);
  }
}

void test_clock_text() {
  // hour, minute, day of month, weekday (1 = Monday), hours, leading zero, day, width
  TEST_ASSERT_EQUAL_STRING("15:45", clockText(15, 45, 28, 1, "24", true, "none", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("06:55", clockText(6, 55, 5, 1, "24", true, "none", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("3:45", clockText(15, 45, 28, 1, "12", false, "none", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("3:45PM", clockText(15, 45, 28, 1, "12ampm", false, "none", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("12:05AM", clockText(0, 5, 5, 1, "12ampm", true, "none", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("MON 3:45", clockText(15, 45, 28, 1, "12", false, "weekday", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("MON 3:45PM", clockText(15, 45, 28, 1, "12ampm", false, "weekday", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("28TH 12:15", clockText(12, 15, 28, 1, "12", false, "ordinal", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("1ST 9:05", clockText(9, 5, 1, 1, "12", false, "ordinal", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("12TH 9:05", clockText(9, 5, 12, 1, "12", false, "ordinal", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("23RD 9:05", clockText(9, 5, 23, 1, "12", false, "ordinal", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("05  06:55", clockText(6, 55, 5, 1, "24", true, "number", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("05 06:55PM", clockText(18, 55, 5, 1, "12ampm", true, "number", 10).c_str());  // tighter gap
  TEST_ASSERT_EQUAL_STRING("28TH3:45PM", clockText(15, 45, 28, 1, "12ampm", false, "ordinal", 10).c_str());
}

void test_progress_milestones() {
  using namespace popup;
  int last = -1;
  // a print seen from the start, every 10 percent
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(0, 10, last));
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(7, 10, last));
  TEST_ASSERT_EQUAL_INT(10, progressMilestone(10, 10, last));
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(14, 10, last));   // same milestone: nothing new
  TEST_ASSERT_EQUAL_INT(20, progressMilestone(23, 10, last));
  TEST_ASSERT_EQUAL_INT(50, progressMilestone(51, 10, last));  // jumped: announce where it is now
  TEST_ASSERT_EQUAL_INT(90, progressMilestone(97, 10, last));
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(100, 10, last));  // 100 is the "done" pop-up
  // seen for the first time at 63 %: no catch-up
  last = -1;
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(63, 10, last));
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(69, 10, last));
  TEST_ASSERT_EQUAL_INT(70, progressMilestone(70, 10, last));
  // every 25 percent
  last = -1;
  progressMilestone(2, 25, last);
  TEST_ASSERT_EQUAL_INT(25, progressMilestone(26, 25, last));
  TEST_ASSERT_EQUAL_INT(50, progressMilestone(50, 25, last));
  TEST_ASSERT_EQUAL_INT(75, progressMilestone(99, 25, last));
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(100, 25, last));
  // progress goes back (a new print on the same printer): starts over
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(3, 25, last));
  TEST_ASSERT_EQUAL_INT(25, progressMilestone(30, 25, last));
  // off
  last = -1;
  TEST_ASSERT_EQUAL_INT(0, progressMilestone(50, 0, last));
}

void test_repeat_schedule() {
  using namespace popup;
  Repeat r;
  r.remaining = 2;
  r.intervalMs = 10 * 60000UL;
  r.nextAt = 1000 + r.intervalMs;                           // first repeat 10 minutes after "done" at t = 1000
  TEST_ASSERT_FALSE(repeatDue(r, 1000 + 5 * 60000UL, true));  // too early
  TEST_ASSERT_FALSE(repeatDue(r, r.nextAt, false));            // due, but can't be shown (quiet hours): waits
  TEST_ASSERT_TRUE(repeatDue(r, r.nextAt + 3600000UL, true));  // shown as soon as it can, even much later
  uint32_t shownAt = r.nextAt + 3600000UL;
  repeatShown(r, shownAt);
  TEST_ASSERT_EQUAL_INT(1, r.remaining);
  TEST_ASSERT_FALSE(repeatDue(r, shownAt + 60000UL, true));    // the interval counts from when it was shown
  TEST_ASSERT_TRUE(repeatDue(r, shownAt + r.intervalMs, true));
  repeatShown(r, shownAt + r.intervalMs);
  TEST_ASSERT_EQUAL_INT(0, r.remaining);
  TEST_ASSERT_FALSE(repeatDue(r, 0xFFFFFFFFu, true));          // finished
  // millis() wrapping around 2^32
  Repeat w;
  w.remaining = 1;
  w.nextAt = 0xFFFFFFF0u + 100;                                // wraps to 84
  TEST_ASSERT_FALSE(repeatDue(w, 0xFFFFFFF0u, true));
  TEST_ASSERT_TRUE(repeatDue(w, 200, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_short_text_is_one_line);
  RUN_TEST(test_words_wrap);
  RUN_TEST(test_words_pack_onto_line);
  RUN_TEST(test_long_word_hyphenates);
  RUN_TEST(test_newlines_force_breaks);
  RUN_TEST(test_empty_text_is_one_blank_line);
  RUN_TEST(test_exact_width);
  RUN_TEST(test_align);
  RUN_TEST(test_sanitize);
  RUN_TEST(test_letter_index);
  RUN_TEST(test_rpm_mapping_matches_original);
  RUN_TEST(test_quiet_hours);
  RUN_TEST(test_days_until);
  RUN_TEST(test_param_table);
  RUN_TEST(test_popup_priorities);
  RUN_TEST(test_popup_windows);
  RUN_TEST(test_progress_milestones);
  RUN_TEST(test_repeat_schedule);
  RUN_TEST(test_content_fits_the_flaps);
  RUN_TEST(test_clock_text);
  return UNITY_END();
}
