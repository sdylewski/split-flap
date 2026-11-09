// Built-in lists for the train station and famous numbers modes.
// Every entry fits on 10 flaps and only uses characters that are on the drum
// (A-Z 0-9 space $ & # : . - ? !).
#pragma once
#include <stdint.h>

namespace content {

struct TrainLine {
  const char* id;
  const char* name;
  const char* const* stops;
  uint8_t count;
};

#define LINE(id, name, ...)                                                               \
  static const char* const id##_stops[] = {__VA_ARGS__};                                  \
  static const TrainLine id##_line = {#id, name, id##_stops, sizeof(id##_stops) / sizeof(id##_stops[0])};

static const char* const RANDOM_STATIONS[] = {
    "GRAND CENT", "PENN STN.",  "UNION STN.", "KINGS X",    "PADDINGTON", "GARE DU N.", "GARE DE L.",
    "CENTRAL ST", "TERMINI",    "AMSTERDAM",  "FRANKFURT",  "MUNICH HB.", "ZURICH HB.", "VIENNA HB.",
    "MADRID AT.", "BARCELONA",  "MILAN CEN.", "ST PANCRAS", "LIVERPOOL",  "CHICAGO UN", "BOSTON SB.",
    "PHILADELPH", "SEATTLE KS", "PORTLAND",   "DENVER UN."};
static const uint8_t RANDOM_STATION_COUNT = sizeof(RANDOM_STATIONS) / sizeof(RANDOM_STATIONS[0]);

// Train lines (type "line")
LINE(eurostar, "Eurostar", "LONDON ST", "ASHFORD", "PARIS NORD", "BRUSSELS", "AMSTERDAM")
LINE(thameslink, "Thameslink", "BEDFORD", "LUTON", "ST ALBANS", "LONDON BL", "LONDON BR", "BRIGHTON")
LINE(acela, "Acela", "BOSTON SB.", "PROVIDENCE", "NEW HAVEN", "NYC PENN", "PHILADELPH", "BALTIMORE", "DC UNION")
LINE(california, "Coast Starlight / Pacific Surfliner", "SACRAMENTO", "DAVIS", "BERKELEY", "OAKLAND", "SAN JOSE",
     "SALINAS", "SAN LUIS", "SANTA BAR", "LOS ANGEL", "ANAHEIM", "SAN DIEGO")

// BART lines (type "bart")
LINE(bart_red, "BART Red", "RICHMOND", "EL CERRITO", "BERKELEY", "OAKLAND 19", "OAKLAND 12", "SF MONTGOM", "SF POWELL",
     "SF CIVIC", "SF 16TH", "SF 24TH", "DALY CITY", "SF AIRPORT", "MILLBRAE")
LINE(bart_yellow, "BART Yellow", "ANTIOCH", "PITTSBURG", "CONCORD", "PLEASANT", "WALNUT CR", "LAFAYETTE", "ORINDA",
     "ROCKRIDGE", "OAKLAND 19", "OAKLAND 12", "SF MONTGOM", "SF POWELL", "SF CIVIC", "SF 16TH", "SF 24TH", "GLEN PARK",
     "BALBOA PK", "DALY CITY", "SF AIRPORT", "S SAN MATE", "SAN BRUNO", "MILLBRAE")
LINE(bart_blue, "BART Blue", "DUBLIN", "CASTRO VL", "HAYWARD", "SAN LEANDR", "COLISEUM", "OAKLAND 12", "SF MONTGOM",
     "SF POWELL", "SF CIVIC", "SF 16TH", "SF 24TH", "DALY CITY")
LINE(bart_green, "BART Green", "FREMONT", "UNION CITY", "SOUTH HAY", "HAYWARD", "SAN LEANDR", "COLISEUM", "OAKLAND 12",
     "LAKE MERRT", "OAKLAND 19", "ROCKRIDGE", "BERKELEY", "EL CERRITO", "RICHMOND")
LINE(bart_orange, "BART Orange", "RICHMOND", "EL CERRITO", "BERKELEY", "OAKLAND 19", "OAKLAND 12", "LAKE MERRT",
     "FRUITVALE", "COLISEUM", "SAN LEANDR", "BAY FAIR", "CASTRO VL", "DUBLIN")

#undef LINE

// ids use '-' in settings/web ("bart-red"); the table uses '_'
static const TrainLine* const TRAIN_LINES[] = {&eurostar_line,   &thameslink_line, &acela_line,
                                               &california_line, &bart_red_line,   &bart_yellow_line,
                                               &bart_blue_line,  &bart_green_line, &bart_orange_line};
static const uint8_t TRAIN_LINE_COUNT = sizeof(TRAIN_LINES) / sizeof(TRAIN_LINES[0]);

// Famous numbers: number, name
static const char* const NUMBERS[][2] = {
    {"3.14159", "PI"},          {"2.71828", "EULER E"},     {"1.41421", "SQRT 2"},      {"1.73205", "SQRT 3"},
    {"6.022 E 23", "AVOGADRO"}, {"6.626 E-34", "PLANCK H"}, {"1.055 E-34", "PLANCK HB"},{"9.80665", "GRAVITY G"},
    {"2.9979 E 8", "LIGHT C"},  {"1.602 E-19", "CHARGE E"}, {"9.109 E-31", "ELECTRON"}, {"1.673 E-27", "PROTON"},
    {"8.314", "GAS CONST"},     {"1.380 E-23", "BOLTZMANN"},{"6.674 E-11", "GRAV CONST"},{"1.61803", "GOLDEN"},
    {"0.57721", "EULER GAM"},   {"2.68545", "KHINCHIN"}};
static const uint8_t NUMBER_COUNT = sizeof(NUMBERS) / sizeof(NUMBERS[0]);

}  // namespace content
