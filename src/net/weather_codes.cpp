// WMO weather interpretation codes, as used by Open-Meteo
// https://open-meteo.com/en/docs (section "WMO Weather interpretation codes")

#include "weather.h"

namespace weather {

// Glyphs from the Weather Icons font (tools/fonts), included by tools/gen_fonts.sh
const char *icon(uint8_t code, bool isDay) {
    switch (code) {
        case 0:
            return isDay ? "\uF00D" : "\uF02E";  // clear
        case 1:
        case 2:
            return isDay ? "\uF002" : "\uF086";  // partly cloudy
        case 3:
            return "\uF013";  // overcast
        case 45:
        case 48:
            return "\uF014";  // fog
        case 51:
        case 53:
        case 55:
            return "\uF01C";  // drizzle
        case 56:
        case 57:
        case 66:
        case 67:
            return "\uF017";  // freezing drizzle / rain
        case 61:
        case 63:
        case 65:
            return "\uF019";  // rain
        case 71:
        case 73:
        case 75:
        case 77:
            return "\uF01B";  // snow
        case 80:
        case 81:
        case 82:
            return isDay ? "\uF009" : "\uF029";  // showers
        case 85:
        case 86:
            return isDay ? "\uF00A" : "\uF02A";  // snow showers
        case 95:
            return isDay ? "\uF010" : "\uF02D";  // thunderstorm
        case 96:
        case 99:
            return "\uF01E";  // thunderstorm with hail
        default:
            return "\uF07B";  // n/a
    }
}

const char *describe(uint8_t code) {
    switch (code) {
        case 0: return "Clear";
        case 1: return "Mainly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: return "Fog";
        case 48: return "Icy fog";
        case 51: return "Light drizzle";
        case 53: return "Drizzle";
        case 55: return "Heavy drizzle";
        case 56:
        case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66:
        case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light showers";
        case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85: return "Snow showers";
        case 86: return "Heavy snow showers";
        case 95: return "Thunderstorm";
        case 96:
        case 99: return "Thunderstorm, hail";
        default: return "Unknown";
    }
}

}  // namespace weather
