// Host tests: pio test -e native
#include <unity.h>

#include "text_layout.cpp"  // pure C++, compiled directly into the test binary

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
  return UNITY_END();
}
