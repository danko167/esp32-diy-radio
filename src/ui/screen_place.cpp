// Settings > Weather location: the current place, search by name, or find it
// automatically from the internet connection

#include <Arduino.h>

#include <vector>

#include "net/weather.h"
#include "net/wifi.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

lv_obj_t *searchStatus;
lv_obj_t *results;
std::vector<weather::Place> found;
bool searching = false;

void onResultTap(lv_event_t *e) {
    const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (i < found.size()) weather::setPlace(found[i]);
    closeSubpage(results);
}

void showResults() {
    lv_obj_clean(results);
    lv_obj_set_hidden(searchStatus, !found.empty());
    if (found.empty()) lv_label_set_text(searchStatus, "Nothing found. Try another spelling.");
    for (size_t i = 0; i < found.size(); i++) {
        makeTappable(addTitleCard(results, found[i].name, &font_ui_22, found[i].region), onResultTap,
                     (void *)(intptr_t)i);
    }
}

void pollSearch(lv_timer_t *) {
    if (!searching) return;
    switch (weather::searchResults(found)) {
        case weather::SearchState::Done:
            searching = false;
            showResults();
            break;
        case weather::SearchState::Failed:
            searching = false;
            lv_label_set_text(searchStatus, "Search failed. Check the Wi-Fi connection.");
            break;
        default:
            break;
    }
}

void startSearch(const char *query) {
    if (!*query) return;
    lv_obj_set_hidden(searchStatus, false);
    lv_obj_clean(results);
    if (wifi::status() != wifi::Status::Connected) {
        lv_label_set_text(searchStatus, "Connect to Wi-Fi first.");
        return;
    }
    lv_label_set_text_fmt(searchStatus, "Searching for “%s”…", query);
    weather::search(query);
    searching = true;
}

}  // namespace

void openLocationPage(lv_obj_t *screen, void (*onClose)()) {
    lv_obj_t *body = openSubpage(screen, "Weather location", onClose, "SETTINGS");

    weather::Place p;
    if (weather::place(p)) {
        char detail[96];
        snprintf(detail, sizeof(detail), "%s%s", p.region, p.automatic ? " · found automatically" : "");
        addTitleCard(body, p.name, &font_ui_30, detail);
    } else {
        addTitleCard(body, "Not set", &font_ui_30, "Found automatically once Wi-Fi is connected");
    }

    lv_obj_t *searchCard = makeTappable(addCard(body, "Search for a place", false), [](lv_event_t *e) {
        openTextInput(lv_obj_get_screen(lv_event_get_target_obj(e)), "Search place", false,
                      startSearch);
    });
    addValue(searchCard, SYM_SEARCH);

    lv_obj_t *autoCard = makeTappable(addCard(body, "Find automatically", false), [](lv_event_t *e) {
        weather::useAutomaticPlace();
        closeSubpage(lv_event_get_target_obj(e));
    });
    addValue(autoCard, LV_SYMBOL_GPS);
    addHint(body, "Automatically means from the internet connection's location, which can be a nearby city.");

    searchStatus = addText(body, "", &font_ui_22, theme::kTextDim);
    lv_obj_set_width(searchStatus, LV_PCT(100));
    lv_label_set_long_mode(searchStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_left(searchStatus, 8, 0);
    lv_obj_set_hidden(searchStatus, true);

    results = lv_obj_create(body);
    lv_obj_remove_style_all(results);
    lv_obj_set_size(results, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(results, 10, 0);

    found.clear();
    searching = false;
    every(results, 300, pollSearch);
}

}  // namespace ui::screens
