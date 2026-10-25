#pragma once

// Mochi: extra screens reached by swiping. Swipe left for the next screen,
// right for the previous one: Mochi -> Status -> Naam -> Instellingen.
// The screens live on LVGL's top layer, so they cover everything while open;
// on the home screen only the page dots are shown.

#include "audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "mochi_face.h"

#include <cctype>
#include <cstdio>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string>

class MochiMenu {
public:
    MochiMenu(MochiFace* face, Display* display) : face_(face), display_(display) {
        ESP_LOGI("MochiMenu", "reset reason: %d", static_cast<int>(esp_reset_reason()));
        lv_obj_t* top = lv_layer_top();

        dots_ = lv_obj_create(top);
        Plain(dots_);
        lv_obj_set_size(dots_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(dots_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(dots_, 8, 0);
        lv_obj_set_style_bg_opa(dots_, LV_OPA_TRANSP, 0);
        lv_obj_align(dots_, LV_ALIGN_CENTER, 0, 205);
        for (int i = 0; i < kPages; i++) {
            dot_[i] = lv_obj_create(dots_);
            Plain(dot_[i]);
            lv_obj_set_size(dot_[i], 9, 9);
            lv_obj_set_style_radius(dot_[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot_[i], LV_OPA_COVER, 0);
        }

        BuildStatusPage();
        BuildNamePage();
        BuildSettingsPage();

        // Swipes anywhere on the home screen reach the screen object by bubbling.
        lv_obj_add_event_cb(lv_screen_active(), &MochiMenu::OnGesture, LV_EVENT_GESTURE, this);
        ShowPage(0);
        lv_timer_create(&MochiMenu::OnTimer, 1000, this);
    }

private:
    static constexpr int kPages = 4;  // 0 = home (Mochi), 1 status, 2 naam, 3 instellingen
    static constexpr uint32_t kNeedColors[4] = {0xFF9F43, 0xFFD93D, 0xFF6B9D, 0x4FC3F7};

    MochiFace* face_;
    Display* display_;
    int page_ = 0;
    lv_obj_t* pages_[kPages] = {};
    lv_obj_t* dots_ = nullptr;
    lv_obj_t* dot_[kPages] = {};

    lv_obj_t* status_title_ = nullptr;
    lv_obj_t* status_info_ = nullptr;
    lv_obj_t* status_bars_[4] = {};
    lv_obj_t* status_values_[4] = {};

    lv_obj_t* name_area_ = nullptr;
    lv_obj_t* name_feedback_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;

    lv_obj_t* volume_ = nullptr;
    lv_obj_t* brightness_ = nullptr;

    static void Plain(lv_obj_t* obj) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
    }

    // A full, round, opaque screen on the top layer (stays clickable so taps
    // never fall through to the house underneath).
    lv_obj_t* NewPage(int index, const char* title) {
        lv_obj_t* page = lv_obj_create(lv_layer_top());
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_set_size(page, LV_HOR_RES, LV_VER_RES);
        lv_obj_center(page);
        lv_obj_set_style_radius(page, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(page, lv_color_hex(0x1A1622), 0);
        lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(page, &MochiMenu::OnGesture, LV_EVENT_GESTURE, this);
        lv_obj_t* label = lv_label_create(page);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFC4CE), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -170);
        // Arrows: always a way back or forward, also where a widget eats the swipe.
        Arrow(page, LV_SYMBOL_LEFT, -200, -1);
        if (index < kPages - 1) {
            Arrow(page, LV_SYMBOL_RIGHT, 200, +1);
        }
        pages_[index] = page;
        lv_obj_move_background(page);  // keep the page dots on top
        return label;
    }

    void Arrow(lv_obj_t* page, const char* symbol, int x, intptr_t step) {
        lv_obj_t* button = lv_button_create(page);
        lv_obj_set_size(button, 44, 44);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x3A3050), 0);
        lv_obj_align(button, LV_ALIGN_CENTER, x, -20);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(step));
        lv_obj_add_event_cb(button, &MochiMenu::OnArrow, LV_EVENT_CLICKED, this);
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, symbol);
        lv_obj_center(label);
    }

    static void OnArrow(lv_event_t* e) {
        auto* self = static_cast<MochiMenu*>(lv_event_get_user_data(e));
        auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        int step = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));
        int page = self->page_ + step;
        if (page >= 0 && page < kPages) {
            self->ShowPage(page);
        }
    }

    static lv_obj_t* Text(lv_obj_t* parent, const char* text, int x, int y, uint32_t color = 0xFFFFFF) {
        lv_obj_t* label = lv_label_create(parent);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, x, y);
        return label;
    }

    // ---------- status ----------

    void BuildStatusPage() {
        status_title_ = NewPage(1, "Status");
        lv_obj_t* page = pages_[1];
        status_info_ = Text(page, "", 0, -125, 0xB8B0FF);
        static const char* const kNames[4] = {"Eten", "Energie", "Plezier", "Schoon"};
        for (int i = 0; i < 4; i++) {
            int y = -70 + i * 52;
            Text(page, kNames[i], -95, y);
            lv_obj_t* bar = lv_bar_create(page);
            lv_obj_set_size(bar, 150, 14);
            lv_obj_align(bar, LV_ALIGN_CENTER, 40, y);
            lv_bar_set_range(bar, 0, 100);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A2230), LV_PART_MAIN);
            lv_obj_set_style_bg_color(bar, lv_color_hex(kNeedColors[i]), LV_PART_INDICATOR);
            status_bars_[i] = bar;
            status_values_[i] = Text(page, "", 140, y);
        }
    }

    void RefreshStatus() {
        const MochiStatus& s = face_->status();
        lv_label_set_text(status_title_, s.name.c_str());
        std::string stage = s.stage;
        if (!stage.empty()) stage[0] = static_cast<char>(toupper(stage[0]));
        char info[96];
        snprintf(info, sizeof(info), "%s  |  %d %s  |  band %d%%%s", stage.c_str(), s.age_days,
                 s.age_days == 1 ? "dag" : "dagen", s.bond, s.asleep ? "  |  slaapt" : "");
        lv_label_set_text(status_info_, info);
        for (int i = 0; i < 4; i++) {
            lv_bar_set_value(status_bars_[i], s.needs[i], LV_ANIM_OFF);
            lv_label_set_text_fmt(status_values_[i], "%d", s.needs[i]);
        }
    }

    // ---------- naam ----------

    void BuildNamePage() {
        NewPage(2, "Naam");
        lv_obj_t* page = pages_[2];
        name_area_ = lv_textarea_create(page);
        lv_textarea_set_one_line(name_area_, true);
        lv_textarea_set_max_length(name_area_, 20);
        lv_obj_set_width(name_area_, 250);
        lv_obj_align(name_area_, LV_ALIGN_CENTER, 0, -110);
        name_feedback_ = Text(page, "Tik op OK om op te slaan", 0, -62, 0xB8B0FF);

        keyboard_ = lv_keyboard_create(page);
        lv_obj_set_size(keyboard_, 330, 180);
        lv_obj_align(keyboard_, LV_ALIGN_CENTER, 0, 62);
        lv_keyboard_set_textarea(keyboard_, name_area_);
        lv_obj_add_event_cb(keyboard_, &MochiMenu::OnNameReady, LV_EVENT_READY, this);
        lv_obj_add_event_cb(keyboard_, &MochiMenu::OnNameCancel, LV_EVENT_CANCEL, this);
    }

    static void OnNameReady(lv_event_t* e) {
        auto* self = static_cast<MochiMenu*>(lv_event_get_user_data(e));
        std::string name = lv_textarea_get_text(self->name_area_);
        if (name.find_first_not_of(' ') == std::string::npos) {
            lv_label_set_text(self->name_feedback_, "Typ eerst een naam");
            return;
        }
        lv_label_set_text(self->name_feedback_, "Opslaan...");
        self->face_->SetName(name);
        // The HTTP request blocks, so it runs in its own short-lived task.
        auto* job = new std::pair<MochiMenu*, std::string>(self, name);
        xTaskCreate(&MochiMenu::PostNameTask, "mochi_name", 6144, job, 2, nullptr);
    }

    static void OnNameCancel(lv_event_t* e) {
        auto* self = static_cast<MochiMenu*>(lv_event_get_user_data(e));
        lv_textarea_set_text(self->name_area_, self->face_->status().name.c_str());
        lv_label_set_text(self->name_feedback_, "Tik op OK om op te slaan");
    }

    static void PostNameTask(void* arg) {
        auto* job = static_cast<std::pair<MochiMenu*, std::string>*>(arg);
        MochiMenu* self = job->first;
        bool ok = false;
        auto network = Board::GetInstance().GetNetwork();
        if (network != nullptr) {
            auto http = network->CreateHttp(0);
            http->SetTimeout(5000);
            http->SetHeader("Content-Type", "application/json");
            cJSON* body = cJSON_CreateObject();
            cJSON_AddStringToObject(body, "naam", job->second.c_str());
            char* json = cJSON_PrintUnformatted(body);
            http->SetContent(std::string(json));
            cJSON_free(json);
            cJSON_Delete(body);
            if (http->Open("POST", MochiServerUrl("/mochi/naam"))) {
                auto status = http->GetStatusCode();
                ok = status && *status == 200;
                http->Close();
            }
        }
        {
            DisplayLockGuard lock(self->display_);
            lv_label_set_text(self->name_feedback_, ok ? "Opgeslagen!" : "Server niet bereikbaar");
        }
        ESP_LOGI("MochiMenu", "rename to '%s': %s", job->second.c_str(), ok ? "ok" : "failed");
        delete job;
        vTaskDelete(nullptr);
    }

    // ---------- instellingen ----------

    void BuildSettingsPage() {
        NewPage(3, "Instellingen");
        lv_obj_t* page = pages_[3];
        Text(page, "Volume", 0, -95);
        volume_ = lv_slider_create(page);
        lv_obj_set_width(volume_, 260);
        lv_obj_align(volume_, LV_ALIGN_CENTER, 0, -60);
        lv_slider_set_range(volume_, 0, 100);
        lv_obj_add_event_cb(volume_, &MochiMenu::OnVolume, LV_EVENT_RELEASED, this);

        Text(page, "Helderheid", 0, 10);
        brightness_ = lv_slider_create(page);
        lv_obj_set_width(brightness_, 260);
        lv_obj_align(brightness_, LV_ALIGN_CENTER, 0, 45);
        lv_slider_set_range(brightness_, 10, 100);
        lv_obj_add_event_cb(brightness_, &MochiMenu::OnBrightness, LV_EVENT_RELEASED, this);

        Text(page, "Veeg naar rechts om terug te gaan", 0, 120, 0x8A80A0);
    }

    static void OnVolume(lv_event_t* e) {
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        Board::GetInstance().GetAudioCodec()->SetOutputVolume(lv_slider_get_value(slider));
    }

    static void OnBrightness(lv_event_t* e) {
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto* backlight = Board::GetInstance().GetBacklight();
        if (backlight != nullptr) {
            backlight->SetBrightness(lv_slider_get_value(slider), true);
        }
    }

    // ---------- navigation ----------

    void ShowPage(int page) {
        page_ = page;
        for (int i = 1; i < kPages; i++) {
            if (i == page) {
                lv_obj_remove_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        for (int i = 0; i < kPages; i++) {
            lv_obj_set_style_bg_color(dot_[i], lv_color_hex(i == page ? 0xFFC4CE : 0x5A5070), 0);
        }
        if (page == 1) {
            RefreshStatus();
        } else if (page == 2) {
            lv_textarea_set_text(name_area_, face_->status().name.c_str());
            lv_label_set_text(name_feedback_, "Tik op OK om op te slaan");
        } else if (page == 3) {
            lv_slider_set_value(volume_, Board::GetInstance().GetAudioCodec()->output_volume(), LV_ANIM_OFF);
            auto* backlight = Board::GetInstance().GetBacklight();
            lv_slider_set_value(brightness_, backlight ? backlight->brightness() : 100, LV_ANIM_OFF);
        }
    }

    static void OnGesture(lv_event_t* e) {
        auto* self = static_cast<MochiMenu*>(lv_event_get_user_data(e));
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_LEFT && self->page_ < kPages - 1) {
            self->ShowPage(self->page_ + 1);
        } else if (dir == LV_DIR_RIGHT && self->page_ > 0) {
            self->ShowPage(self->page_ - 1);
        }
    }

    static void OnTimer(lv_timer_t* timer) {
        auto* self = static_cast<MochiMenu*>(lv_timer_get_user_data(timer));
        if (self->page_ == 1) {
            self->RefreshStatus();
        }
    }
};
