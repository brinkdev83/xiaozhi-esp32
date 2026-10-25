#pragma once

// Mochi: draws the pet itself (instead of an emoji) on the round 466 px screen,
// with four need bars along the top edge, and polls the server's /mochi/state
// so the face follows the pet's stage and needs. When the pet wants attention
// and the device is idle, it starts a conversation by itself.
//
// All LVGL access happens with the display lock held: the animation timer runs
// in the LVGL task, SetEmotion/ApplyState are called under DisplayLockGuard.

#include "application.h"
#include "board.h"
#include "display/display.h"
#include "mochi_house.h"

#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string>

// The pet's state as served by /mochi/state (core/utils/mochi_pet.py:device_state()).
struct MochiStatus {
    std::string name = "Mochi";
    std::string stage = "baby";
    std::string mood = "gewoon";
    bool asleep = false;
    int age_days = 0;
    int bond = 0;
    int needs[4] = {80, 80, 80, 80};  // eten, energie, plezier, schoon
};

// Base URL of the Mochi server: the host of the OTA URL.
inline std::string MochiServerUrl(const char* path) {
    std::string ota = CONFIG_OTA_URL;
    auto pos = ota.find("/xiaozhi/ota/");
    return (pos == std::string::npos ? ota : ota.substr(0, pos)) + path;
}

class MochiFace {
public:
    // Builds the face on `screen`, just above the background container.
    explicit MochiFace(lv_obj_t* screen) {
        root_ = lv_obj_create(screen);
        Plain(root_);
        lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
        lv_obj_center(root_);
        lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
        lv_obj_move_to_index(root_, 1);  // index 0 is the opaque background container

        // Need bars: two arcs on each side, clear of the icons at the top and the
        // chat text at the bottom (angles: 0 = 3 o'clock, clockwise).
        // Left: eten, energie; right: plezier, schoon.
        static const int kBarStart[4] = {142, 178, 330, 6};
        for (int i = 0; i < 4; i++) {
            lv_obj_t* arc = lv_arc_create(root_);
            lv_obj_set_size(arc, LV_HOR_RES - 8, LV_VER_RES - 8);
            lv_obj_center(arc);
            lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
            lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
            lv_arc_set_rotation(arc, 0);
            lv_arc_set_bg_angles(arc, kBarStart[i], (kBarStart[i] + 32) % 360);
            lv_arc_set_range(arc, 0, 100);
            lv_arc_set_value(arc, 80);
            lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
            lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A2230), LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, lv_color_hex(kNeedColors[i]), LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
            lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
            bars_[i] = arc;
        }

        BuildOutside();
        house_.BuildBackground(root_);

        antenna_ = Shape(root_, 0xF0A0B4);
        antenna_ball_ = Shape(root_, 0xFF80A0);

        body_ = Shape(root_, kBodyColor);
        lv_obj_set_style_border_width(body_, 6, 0);
        lv_obj_set_style_border_color(body_, lv_color_hex(0xF0A0B4), 0);

        for (int i = 0; i < 3; i++) {
            spots_[i] = Shape(body_, 0x9C7A5B);
            lv_obj_set_style_bg_opa(spots_[i], LV_OPA_60, 0);
        }
        for (int i = 0; i < 2; i++) {
            cheeks_[i] = Shape(body_, 0xFF80A0);
            lv_obj_set_style_bg_opa(cheeks_[i], LV_OPA_70, 0);
            eyes_[i] = Shape(body_, 0x322038);
            shine_[i] = Shape(eyes_[i], 0xFFFFFF);
        }

        mouth_arc_ = lv_arc_create(body_);
        lv_obj_remove_style(mouth_arc_, nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_rotation(mouth_arc_, 0);
        lv_obj_set_style_arc_width(mouth_arc_, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(mouth_arc_, 0, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(mouth_arc_, lv_color_hex(0x322038), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(mouth_arc_, true, LV_PART_MAIN);
        mouth_open_ = Shape(body_, 0x96284A);

        house_.BuildForeground(root_);  // tub front, duvet and props over Mochi

        zzz_ = lv_label_create(root_);
        lv_label_set_text(zzz_, "z Z z");
        lv_obj_set_style_text_color(zzz_, lv_color_hex(0xB8B0FF), 0);

        // Thought bubble with the most urgent need, top-right of Mochi.
        for (int i = 0; i < 2; i++) {
            bubble_dots_[i] = Shape(root_, 0xFFFFFF);
        }
        bubble_ = lv_obj_create(root_);
        Plain(bubble_);
        lv_obj_set_size(bubble_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(bubble_, 18, 0);
        lv_obj_set_style_bg_opa(bubble_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(bubble_, 12, 0);
        lv_obj_set_style_pad_ver(bubble_, 6, 0);
        lv_obj_align(bubble_, LV_ALIGN_CENTER, 135, -125);
        bubble_label_ = lv_label_create(bubble_);
        lv_obj_set_style_text_color(bubble_label_, lv_color_hex(0x322038), 0);
        lv_obj_center(bubble_label_);

        // Debug: log every touch press and the object that receives it.
        for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr;
             indev = lv_indev_get_next(indev)) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
                lv_indev_add_event_cb(indev, &MochiFace::OnIndevPressed, LV_EVENT_PRESSED, this);
            }
        }

        timer_ = lv_timer_create(&MochiFace::TimerCallback, 50, this);
        Render();
    }

    // A short-lived expression from the conversation (Claude's emoji).
    void SetEmotion(const char* emotion) {
        emotion_ = emotion ? emotion : "";
        emotion_until_ = lv_tick_get() + 6000;
    }

    void ApplyState(const MochiStatus& status) {
        status_ = status;
        stage_ = status.stage;
        mood_ = status.mood;
        asleep_ = status.asleep;
        for (int i = 0; i < 4; i++) {
            lv_arc_set_value(bars_[i], status.needs[i]);
        }
        dirty_ = status.needs[3] < 35;
    }

    const MochiStatus& status() const { return status_; }

    // Voice command (MCP tool self.mochi.ga_naar): go to a room or piece of
    // furniture, entering the house first if needed.
    bool GoToPlace(const std::string& place) {
        if (place == "buiten") {
            if (inside_view_) SetInside(false);
            return true;
        }
        if (!inside_view_) {
            SetInside(true);
        }
        return house_.GoToPlace(place);
    }
    void SetName(const std::string& name) { status_.name = name; }

private:
    enum class Look { Normal, Happy, Sad, Sleepy, Surprised };
    static constexpr uint32_t kBodyColor = 0xFFC4CE;
    // eten, energie, plezier, schoon (same order as the server's needs)
    static constexpr uint32_t kNeedColors[4] = {0xFF9F43, 0xFFD93D, 0xFF6B9D, 0x4FC3F7};
    static constexpr const char* kNeedMood[4] = {"honger", "moe", "verveling", "vies"};
    static constexpr const char* kNeedText[4] = {"Honger!", "Slaap...", "Spelen?", "Badje!"};

    lv_obj_t* root_ = nullptr;
    lv_obj_t* bars_[4] = {};
    lv_opa_t bar_opa_[4] = {LV_OPA_COVER, LV_OPA_COVER, LV_OPA_COVER, LV_OPA_COVER};
    lv_obj_t* body_ = nullptr;
    lv_obj_t* antenna_ = nullptr;
    lv_obj_t* antenna_ball_ = nullptr;
    lv_obj_t* eyes_[2] = {};
    lv_obj_t* shine_[2] = {};
    lv_obj_t* cheeks_[2] = {};
    lv_obj_t* spots_[3] = {};
    lv_obj_t* mouth_arc_ = nullptr;
    lv_obj_t* mouth_open_ = nullptr;
    lv_obj_t* zzz_ = nullptr;
    lv_obj_t* bubble_ = nullptr;
    lv_obj_t* bubble_label_ = nullptr;
    lv_obj_t* bubble_dots_[2] = {};
    lv_timer_t* timer_ = nullptr;

    // Scenes: outside with a little house; inside, MochiHouse's rooms.
    lv_obj_t* outside_ = nullptr;
    MochiHouse house_;
    bool inside_view_ = false;
    float x_ = 0, y_ = -10;          // Mochi's centre (animated)
    float target_x_ = 0, target_y_ = -10;

    std::string stage_ = "baby";
    std::string mood_ = "gewoon";
    std::string emotion_;
    uint32_t emotion_until_ = 0;
    bool asleep_ = false;
    bool dirty_ = false;
    MochiStatus status_;
    uint32_t next_blink_ = 3000;

    static void Plain(lv_obj_t* obj) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
    }

    static lv_obj_t* Shape(lv_obj_t* parent, uint32_t color) {
        lv_obj_t* obj = lv_obj_create(parent);
        Plain(obj);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        return obj;
    }

    // Only touches the flag on a real change: LVGL redraws the object on every
    // HIDDEN flag write, and this runs 20 times a second.
    static void Show(lv_obj_t* obj, bool visible) {
        bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
        if (visible && hidden) {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else if (!visible && !hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    static void TimerCallback(lv_timer_t* timer) {
        static_cast<MochiFace*>(lv_timer_get_user_data(timer))->Render();
    }

    // A rectangle with rounded corners (or a circle with radius LV_RADIUS_CIRCLE).
    static lv_obj_t* Box(lv_obj_t* parent, int w, int h, int x, int y, uint32_t color,
                         int radius = 6) {
        lv_obj_t* obj = Shape(parent, color);
        lv_obj_set_style_radius(obj, radius, 0);
        lv_obj_set_size(obj, w, h);
        lv_obj_align(obj, LV_ALIGN_CENTER, x, y);
        return obj;
    }

    static lv_obj_t* Layer(lv_obj_t* parent) {
        lv_obj_t* obj = lv_obj_create(parent);
        Plain(obj);
        lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);
        lv_obj_center(obj);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        return obj;
    }

    static void Clickable(lv_obj_t* obj, lv_event_cb_t cb, void* self) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, self);
    }

    // Outside: a green hill and a little house bottom-left (tap to go in).
    void BuildOutside() {
        outside_ = Layer(root_);
        Box(outside_, 440, 150, 0, 165, 0x2E6B3F, LV_RADIUS_CIRCLE);

        lv_obj_t* house = lv_obj_create(outside_);
        Plain(house);
        lv_obj_set_size(house, 96, 104);
        lv_obj_align(house, LV_ALIGN_CENTER, -128, 72);
        lv_obj_set_style_transform_pivot_x(house, 48, 0);
        lv_obj_set_style_transform_pivot_y(house, 104, 0);
        lv_obj_set_style_transform_scale(house, 384, 0);  // 1.5x (256 = 1x)
        lv_obj_set_style_bg_opa(house, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(house, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        Clickable(house, &MochiFace::OnHouseClicked, this);

        // Roof: a square turned 45 degrees, its lower half hidden behind the walls.
        lv_obj_t* roof = Box(house, 56, 56, 0, -20, 0xC0392B, 4);
        lv_obj_set_style_transform_pivot_x(roof, 28, 0);
        lv_obj_set_style_transform_pivot_y(roof, 28, 0);
        lv_obj_set_style_transform_rotation(roof, 450, 0);
        Box(house, 12, 22, 22, -34, 0x8E5B3E, 2);           // chimney
        Box(house, 70, 52, 0, 24, 0xF5E6C8, 4);             // walls
        Box(house, 20, 32, -14, 34, 0x8E5B3E, 4);           // door
        Box(house, 4, 4, -9, 36, 0xFFD93D, LV_RADIUS_CIRCLE);  // door knob
        Box(house, 20, 18, 16, 18, 0x6FD3FF, 3);            // window
        Box(house, 2, 18, 16, 18, 0xF5E6C8, 0);             // window cross
        Box(house, 20, 2, 16, 18, 0xF5E6C8, 0);
        Box(house, 26, 6, 16, 30, 0x8E5B3E, 2);             // flower box
        Box(house, 5, 5, 9, 26, 0xFF6B9D, LV_RADIUS_CIRCLE);
        Box(house, 5, 5, 17, 26, 0xFFD93D, LV_RADIUS_CIRCLE);
        Box(house, 5, 5, 24, 26, 0xFF6B9D, LV_RADIUS_CIRCLE);
    }

    static void OnIndevPressed(lv_event_t* e) {
        auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(e));
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        lv_obj_t* target = lv_indev_get_active_obj();
        ESP_LOGI("MochiFace", "touch at %d,%d -> obj %p (root %p)", static_cast<int>(point.x),
                 static_cast<int>(point.y), target,
                 static_cast<MochiFace*>(lv_event_get_user_data(e))->root_);
    }

    static void OnHouseClicked(lv_event_t* e) {
        ESP_LOGI("MochiFace", "house clicked");
        static_cast<MochiFace*>(lv_event_get_user_data(e))->SetInside(true);
    }

    void SetInside(bool inside) {
        inside_view_ = inside;
        Show(outside_, !inside);
        if (inside) {
            house_.Enter(x_, y_);
        } else {
            house_.Leave();
            x_ = -135;  // out through the front door of the little house
            y_ = 70;
            target_x_ = 0;
            target_y_ = -10;
        }
    }

    Look CurrentLook(uint32_t now) const {
        if (asleep_ || stage_ == "ei") {
            return Look::Sleepy;
        }
        if (now < emotion_until_) {
            const std::string& e = emotion_;
            if (e == "sad" || e == "crying" || e == "angry") return Look::Sad;
            if (e == "sleepy") return Look::Sleepy;
            if (e == "surprised" || e == "shocked" || e == "embarrassed") return Look::Surprised;
            static const char* const kHappy[] = {
                "happy", "laughing", "funny", "loving", "kissy", "winking",
                "cool", "confident", "silly", "delicious", "relaxed",
            };
            for (const char* name : kHappy) {
                if (e == name) return Look::Happy;
            }
            // neutral, thinking, confused and system icons fall through to the mood
        }
        if (mood_ == "blij") return Look::Happy;
        if (mood_ == "moe") return Look::Sleepy;
        if (mood_ == "honger" || mood_ == "verveling") return Look::Sad;
        return Look::Normal;
    }

    void Render() {
        const uint32_t now = lv_tick_get();
        const bool egg = stage_ == "ei";
        const auto state = Application::GetInstance().GetDeviceState();
        const bool speaking = state == kDeviceStateSpeaking;
        const bool listening = state == kDeviceStateListening;
        Look look = CurrentLook(now);

        // In the house, MochiHouse decides where Mochi goes and what he does.
        auto pose = MochiHouse::Pose::Stand;
        if (inside_view_) {
            house_.Tick(now, stage_, asleep_, x_, y_);
            if (house_.wants_outside()) {
                SetInside(false);
            } else {
                target_x_ = house_.walk_x();
                target_y_ = house_.walk_y();
                pose = house_.pose();
            }
        }
        if (pose == MochiHouse::Pose::Lie) {
            look = Look::Sleepy;
        }
        // Walk at a steady pace toward the target.
        const float to_x = target_x_ - x_, to_y = target_y_ - y_;
        const float distance = std::sqrt(to_x * to_x + to_y * to_y);
        const float pace = inside_view_ ? 3.5f : 6.0f;
        if (distance <= pace) {
            x_ = target_x_;
            y_ = target_y_;
        } else {
            x_ += to_x / distance * pace;
            y_ += to_y / distance * pace;
        }
        const bool walking = distance > 1.0f;
        const int full = egg ? 150 : stage_ == "baby" ? 150 : stage_ == "kind" ? 185 : 215;
        const int size = inside_view_ ? full * 65 / 100 : full;

        // Body: breathing bob (an egg wobbles instead), a hop when happy.
        float t = now / 1000.0f;
        int dy = static_cast<int>(4 * std::sin(t * 2.6f));
        int dx = egg ? static_cast<int>(5 * std::sin(t * 1.7f)) : 0;
        if ((look == Look::Happy || walking) && !egg) {
            dy -= static_cast<int>(size / 15 * std::fabs(std::sin(t * (walking ? 9.0f : 5.0f))));
        }
        int w = size;
        int h = egg ? size * 6 / 5 : size;
        if (pose == MochiHouse::Pose::Lie) {
            w = size * 13 / 10;
            h = size * 7 / 10;
            dy = 0;
        } else if (pose == MochiHouse::Pose::Sit) {
            h = size * 9 / 10;
        }
        lv_obj_set_size(body_, w, h);
        lv_obj_set_style_border_width(body_, inside_view_ ? 3 : 6, 0);
        lv_obj_align(body_, LV_ALIGN_CENTER, static_cast<int>(x_) + dx, static_cast<int>(y_) + dy);
        lv_obj_set_style_bg_color(body_, lv_color_hex(egg ? 0xFFF1D0 : kBodyColor), 0);
        lv_obj_set_style_border_color(body_, lv_color_hex(egg ? 0xE8D5A8 : 0xF0A0B4), 0);

        // Antenna for older stages.
        bool antenna = stage_ == "kind" || stage_ == "volwassen";
        Show(antenna_, antenna);
        Show(antenna_ball_, antenna);
        if (antenna) {
            int sway = static_cast<int>(6 * std::sin(t * 1.8f));
            lv_obj_set_size(antenna_, inside_view_ ? 3 : 6, size / 5);
            lv_obj_align_to(antenna_, body_, LV_ALIGN_OUT_TOP_MID, sway / 2, 6);
            lv_obj_set_size(antenna_ball_, size / 8, size / 8);
            lv_obj_align_to(antenna_ball_, antenna_, LV_ALIGN_OUT_TOP_MID, sway / 2, 4);
            lv_obj_set_style_bg_color(antenna_ball_, lv_color_hex(listening ? 0xFFD93D : 0xFF80A0), 0);
        }

        // Eyes.
        bool blink = false;
        if (now >= next_blink_) {
            blink = now < next_blink_ + 140;
            if (!blink) next_blink_ = now + 2500 + (now % 3000);
        }
        int eye_w = size * 11 / 100;
        int eye_h = size * 16 / 100;
        if (listening) eye_h += 3;
        if (look == Look::Sleepy || blink) eye_h = 5;
        else if (look == Look::Happy) eye_h = size * 6 / 100;
        else if (look == Look::Surprised) { eye_w += 4; eye_h += 6; }
        for (int i = 0; i < 2; i++) {
            int side = i == 0 ? -1 : 1;
            Show(eyes_[i], !egg);
            Show(cheeks_[i], !egg);
            lv_obj_set_size(eyes_[i], eye_w, eye_h);
            lv_obj_align(eyes_[i], LV_ALIGN_CENTER, side * size * 20 / 100, -size * 8 / 100);
            bool shine = eye_h > 12;
            Show(shine_[i], shine);
            if (shine) {
                lv_obj_set_size(shine_[i], eye_w / 3, eye_w / 3);
                lv_obj_align(shine_[i], LV_ALIGN_TOP_LEFT, eye_w / 5, eye_h / 6);
            }
            lv_obj_set_size(cheeks_[i], size * 15 / 100, size * 9 / 100);
            lv_obj_align(cheeks_[i], LV_ALIGN_CENTER, side * size * 32 / 100, size * 8 / 100);
        }

        // Dirt spots when not clean.
        static const int kSpot[3][2] = {{-30, 30}, {28, -32}, {10, 38}};
        for (int i = 0; i < 3; i++) {
            Show(spots_[i], dirty_ && !egg);
            lv_obj_set_size(spots_[i], size / 12, size / 14);
            lv_obj_align(spots_[i], LV_ALIGN_CENTER, kSpot[i][0] * size / 100, kSpot[i][1] * size / 100);
        }

        // Mouth: open while speaking (flapping) or surprised, else a smile or frown.
        bool open = !egg && ((speaking && (now / 160) % 2 == 0) || look == Look::Surprised);
        Show(mouth_open_, open);
        Show(mouth_arc_, !egg && !open);
        int mouth = size * 24 / 100;
        lv_obj_set_style_arc_width(mouth_arc_, inside_view_ ? 3 : 5, LV_PART_MAIN);
        if (open) {
            lv_obj_set_size(mouth_open_, mouth * 6 / 10, mouth / 2);
            lv_obj_align(mouth_open_, LV_ALIGN_CENTER, 0, size * 15 / 100);
        } else {
            lv_obj_set_size(mouth_arc_, mouth, mouth);
            bool frown = look == Look::Sad;
            if (frown) {
                lv_arc_set_bg_angles(mouth_arc_, 210, 330);
                lv_obj_align(mouth_arc_, LV_ALIGN_CENTER, 0, size * 22 / 100);
            } else {
                int spread = look == Look::Happy ? 15 : 35;
                lv_arc_set_bg_angles(mouth_arc_, spread, 180 - spread);
                lv_obj_align(mouth_arc_, LV_ALIGN_CENTER, 0, size * 2 / 100);
            }
        }

        // Sleeping: floating z's.
        bool sleeping = asleep_ && !egg;
        Show(zzz_, sleeping);
        if (sleeping) {
            int float_y = static_cast<int>((now / 40) % 30);
            lv_obj_align_to(zzz_, body_, LV_ALIGN_OUT_TOP_RIGHT, 10, 10 - float_y);
        }

        // The most urgent need: a thought bubble and its bar blinking.
        int need = -1;
        for (int i = 0; i < 4; i++) {
            if (mood_ == kNeedMood[i]) need = i;
        }
        bool bubble = need >= 0 && !egg && !asleep_ && !inside_view_;
        Show(bubble_, bubble);
        for (auto* dot : bubble_dots_) Show(dot, bubble);
        if (bubble) {
            lv_label_set_text_static(bubble_label_, kNeedText[need]);
            lv_obj_set_style_bg_color(bubble_, lv_color_hex(kNeedColors[need]), 0);
            int pop = static_cast<int>(3 * std::sin(t * 4.0f));
            lv_obj_align(bubble_, LV_ALIGN_CENTER, 135, -125 + pop);
            // Dots from Mochi's head (up-right, at 45 degrees) toward the bubble.
            const int near = (size / 2 + 10) * 707 / 1000;
            const int far = (size / 2 + 32) * 707 / 1000;
            lv_obj_set_size(bubble_dots_[0], 10, 10);
            lv_obj_align(bubble_dots_[0], LV_ALIGN_CENTER, near, -10 - near);
            lv_obj_set_size(bubble_dots_[1], 16, 16);
            lv_obj_align(bubble_dots_[1], LV_ALIGN_CENTER, far, -10 - far);
            for (auto* dot : bubble_dots_) {
                lv_obj_set_style_bg_color(dot, lv_color_hex(kNeedColors[need]), 0);
            }
        }
        bool blink_on = (now / 500) % 2 == 0;
        for (int i = 0; i < 4; i++) {
            Show(bars_[i], !egg);  // bars mean nothing for an egg
            lv_opa_t opa = (bubble && i == need && !blink_on) ? LV_OPA_30 : LV_OPA_COVER;
            if (opa != bar_opa_[i]) {  // the arcs span the whole screen: redraw only on change
                bar_opa_[i] = opa;
                lv_obj_set_style_arc_opa(bars_[i], opa, LV_PART_INDICATOR);
            }
        }
    }
};

// Polls /mochi/state on the server that serves the OTA endpoint.
class MochiStatePoller {
public:
    MochiStatePoller(MochiFace* face, Display* display) : face_(face), display_(display) {
        url_ = MochiServerUrl("/mochi/state");
        xTaskCreate(&MochiStatePoller::TaskEntry, "mochi_state", 6144, this, 2, nullptr);
    }

private:
    static constexpr const char* kTag = "MochiState";
    static constexpr int kIntervalMs = 30000;

    MochiFace* face_;
    Display* display_;
    std::string url_;

    static void TaskEntry(void* arg) {
        static_cast<MochiStatePoller*>(arg)->Run();
    }

    void Run() {
        vTaskDelay(pdMS_TO_TICKS(15000));  // let Wi-Fi and activation finish
        while (true) {
            PollOnce();
            vTaskDelay(pdMS_TO_TICKS(kIntervalMs));
        }
    }

    void PollOnce() {
        auto& app = Application::GetInstance();
        bool idle = app.GetDeviceState() == kDeviceStateIdle;
        auto network = Board::GetInstance().GetNetwork();
        if (network == nullptr) {
            return;
        }
        auto http = network->CreateHttp(0);
        http->SetTimeout(5000);
        std::string url = url_ + (idle ? "?claim=1" : "");
        if (auto opened = http->Open("GET", url); !opened) {
            ESP_LOGW(kTag, "GET %s failed", url.c_str());
            return;
        }
        auto status = http->GetStatusCode();
        if (!status || *status != 200) {
            http->Close();
            return;
        }
        std::string body = http->ReadAll();
        http->Close();

        cJSON* root = cJSON_Parse(body.c_str());
        if (root == nullptr) {
            return;
        }
        auto str = [&](const char* key, const char* fallback) {
            cJSON* item = cJSON_GetObjectItem(root, key);
            return std::string(cJSON_IsString(item) ? item->valuestring : fallback);
        };
        auto num = [&](const char* key) {
            cJSON* item = cJSON_GetObjectItem(root, key);
            return cJSON_IsNumber(item) ? item->valueint : 0;
        };
        MochiStatus pet;
        pet.name = str("naam", "Mochi");
        pet.stage = str("fase", "baby");
        pet.mood = str("stemming", "gewoon");
        pet.asleep = cJSON_IsTrue(cJSON_GetObjectItem(root, "slaapt"));
        pet.age_days = num("leeftijd_dagen");
        pet.bond = num("band");
        const char* keys[4] = {"verzadigd", "energie", "plezier", "schoon"};
        for (int i = 0; i < 4; i++) pet.needs[i] = num(keys[i]);
        std::string attention = str("aandacht", "");
        cJSON_Delete(root);

        {
            DisplayLockGuard lock(display_);
            face_->ApplyState(pet);
        }

        if (!attention.empty() && app.GetDeviceState() == kDeviceStateIdle) {
            ESP_LOGI(kTag, "Mochi wants attention: %s", attention.c_str());
            app.WakeWordInvoke("[Mochi wil aandacht: " + attention + "]");
        }
    }
};
