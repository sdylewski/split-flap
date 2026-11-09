#include "text_layout.h"

#include <cctype>

#include "unit_protocol.h"

namespace layout {

Align parseAlign(const std::string& name) {
  if (name == "center") return Align::Center;
  if (name == "right") return Align::Right;
  return Align::Left;
}

static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
}

static void collapse(std::string& s, const std::string& from, const std::string& to) {
  while (s.find(from) != std::string::npos) replaceAll(s, from, to);
}

static void trim(std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    s.clear();
    return;
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  s = s.substr(start, end - start + 1);
}

std::vector<std::string> splitLines(std::string text, int width) {
  std::vector<std::string> lines;
  if (width <= 0) return lines;

  replaceAll(text, "\\n", "\n");
  replaceAll(text, "\r", "");
  replaceAll(text, "\t", " ");
  trim(text);
  collapse(text, "\n\n", "\n");
  collapse(text, " \n", "\n");
  collapse(text, "\n ", "\n");
  collapse(text, "  ", " ");
  if (text.empty()) {
    lines.push_back("");
    return lines;
  }

  // Words, keeping the newlines attached so they can force breaks
  std::vector<std::string> words;
  size_t start = 0;
  while (start <= text.size()) {
    size_t space = text.find(' ', start);
    if (space == std::string::npos) {
      words.push_back(text.substr(start));
      break;
    }
    words.push_back(text.substr(start, space - start));
    start = space + 1;
  }

  std::string line;
  for (const std::string& word : words) {
    size_t newline = word.find('\n');
    if (newline != 0) {
      size_t wordLength = newline == std::string::npos ? word.size() : newline;
      if (!line.empty() && line.size() + wordLength + 1 > (size_t)width) {
        lines.push_back(line);
        line.clear();
      }
      if (!line.empty()) line += ' ';
    }
    for (size_t i = 0; i < word.size(); i++) {
      char c = word[i];
      if (c == '\n') {
        lines.push_back(line);
        line.clear();
        continue;
      }
      // Word doesn't fit on a line by itself: hyphenate
      if ((int)line.size() == width - 1 && i < word.size() - 1) {
        line += '-';
        lines.push_back(line);
        line.clear();
      }
      line += c;
      if ((int)line.size() == width) {
        lines.push_back(line);
        line.clear();
      }
    }
  }
  if (!line.empty()) lines.push_back(line);
  if (lines.empty()) lines.push_back("");
  return lines;
}

std::string align(const std::string& input, int width, Align mode) {
  std::string line = input;
  for (char& c : line) c = (char)std::toupper((unsigned char)c);
  if ((int)line.size() > width) line = line.substr(0, width);
  int pad = width - (int)line.size();
  switch (mode) {
    case Align::Left: return line + std::string(pad, ' ');
    case Align::Right: return std::string(pad, ' ') + line;
    case Align::Center: {
      int left = pad / 2;
      return std::string(left, ' ') + line + std::string(pad - left, ' ');
    }
  }
  return line;
}

int sanitize(std::string& line) {
  int replaced = 0;
  for (char& c : line) {
    if (unitproto::letterIndex(c) < 0) {
      c = ' ';
      replaced++;
    }
  }
  return replaced;
}

int parseHHMM(const std::string& text) {
  size_t colon = text.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) return -1;
  for (size_t i = 0; i < text.size(); i++) {
    if (i != colon && !std::isdigit((unsigned char)text[i])) return -1;
  }
  int hours = std::stoi(text.substr(0, colon));
  int minutes = std::stoi(text.substr(colon + 1));
  if (hours > 23 || minutes > 59) return -1;
  return hours * 60 + minutes;
}

bool inWindow(int now, int from, int to) {
  if (from < 0 || to < 0 || from == to) return false;
  if (from < to) return now >= from && now < to;
  return now >= from || now < to;  // wraps past midnight
}

long daysUntil(long long target, long long now) {
  long long seconds = target - now;
  if (seconds <= 0) return 0;
  return (long)((seconds + 86399) / 86400);
}

}  // namespace layout
