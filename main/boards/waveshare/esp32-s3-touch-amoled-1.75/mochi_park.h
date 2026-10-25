#pragma once

// Mochi's playground, reached from the signpost outside: a swing, a slide, a
// seesaw, a trampoline, a sandpit and a football goal. Tap a piece of
// equipment (or ask by voice, MCP tool self.mochi.ga_naar) and Mochi goes and
// plays on it; a tap also tells the server, which applies the fun, exercise
// and tiredness. Like in the house, Mochi does nothing here by himself.

#include "application.h"
#include "mochi_house.h"  // mochi_draw helpers

#include <cmath>
#include <cstdint>
#include <lvgl.h>
#include <string>

class MochiPark {
public:
    void BuildBackground(lv_obj_t* parent) {
        using namespace mochi_draw;
        scene_ = Layer(parent);
        Box(scene_, LV_HOR_RES, LV_VER_RES, 0, 0, 0x87CEEB, 0);      // sky
        Circle(scene_, 50, 120, -150, 0xF9E79F);                     // sun
        Box(scene_, 90, 26, -90, -150, 0xFFFFFF, LV_RADIUS_CIRCLE);  // cloud
        Box(scene_, 60, 22, -60, -160, 0xFFFFFF, LV_RADIUS_CIRCLE);
        Box(scene_, LV_HOR_RES, 260, 0, 140, 0x58B368, 0);           // grass
        Box(scene_, LV_HOR_RES, 6, 0, 12, 0x4A9A58, 0);
        Box(scene_, 70, LV_VER_RES, 0, 150, 0xD9B77E, 0);            // path
        Label(scene_, "Speeltuin", 0, -118, 0x1B4F72);

        BuildSwing();
        BuildSlide();
        BuildSeesaw();
        BuildTrampoline();
        BuildSandpit();
        BuildGoal();

        // Signpost back home (left).
        Box(scene_, 8, 70, -176, 20, 0x8B5A2B, 2);
        lv_obj_t* sign = Box(scene_, 74, 28, -160, -12, 0xA0522D, 6);
        Label(sign, "< Huis", 0, 0, 0xFFFFFF);
        lv_obj_set_ext_click_area(sign, 24);
        lv_obj_add_flag(sign, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(sign, &MochiPark::OnHome, LV_EVENT_CLICKED, this);

        mochi_draw::Show(scene_, false);
    }

    // Props that move in front of Mochi.
    void BuildForeground(lv_obj_t* parent) {
        using namespace mochi_draw;
        ball_ = Circle(parent, 24, 0, 0, 0xFFFFFF);
        Box(ball_, 8, 8, 0, 0, 0x222222, LV_RADIUS_CIRCLE);
        for (auto*& grain : sand_) {
            grain = Circle(parent, 8, 0, 0, 0xE5C27A);
        }
        HideProps();
    }

    void Enter(float& mochi_x, float& mochi_y) {
        mochi_draw::Show(scene_, true);
        mochi_x = -120;
        mochi_y = kGroundY;
        target_x_ = 0;
        target_y_ = kGroundY;
        activity_ = -1;
        pending_ = -1;
        wants_home_ = false;
    }

    void Leave() {
        mochi_draw::Show(scene_, false);
        HideProps();
    }

    static bool IsPlace(const std::string& place) {
        return place == "speeltuin" || Index(place) >= 0;
    }

    // Voice command: a piece of equipment (or just "speeltuin").
    bool GoToPlace(const std::string& place) {
        int i = Index(place);
        if (i >= 0) {
            GoTo(i, false);
        }
        return i >= 0 || place == "speeltuin";
    }

    // Every frame while in the park. During an activity Mochi's position is
    // set directly (swinging, sliding, bouncing); walk_x/y then equal it.
    void Tick(uint32_t now, float& mochi_x, float& mochi_y, int mochi_size) {
        now_ = now;
        bool arrived = std::fabs(mochi_x - target_x_) < 3 && std::fabs(mochi_y - target_y_) < 3;
        if (pending_ >= 0 && arrived) {
            activity_ = pending_;
            pending_ = -1;
            started_ = now;
            if (send_) {
                Application::GetInstance().SendTextEvent(
                    std::string("[Speeltuin: de gebruiker laat Mochi spelen op de ") + kNames[activity_] + "]");
            }
        }
        if (activity_ >= 0 && now - started_ > 12000) {
            activity_ = -1;
            target_x_ = mochi_x;  // step off where he is
            target_y_ = kGroundY;
        }
        sitting_ = false;
        if (activity_ >= 0) {
            resting_ = false;
            Animate(now, mochi_x, mochi_y, mochi_size);
            target_x_ = mochi_x;
            target_y_ = mochi_y;
        } else {
            RestEquipment();
        }
        UpdateProps(mochi_x, mochi_y);
    }

    float walk_x() const { return target_x_; }
    float walk_y() const { return target_y_; }
    bool sitting() const { return sitting_; }
    bool wants_home() const { return wants_home_; }

private:
    static constexpr float kGroundY = 120;
    static constexpr int kCount = 6;
    enum { kSwing = 0, kSlide, kSeesaw, kTrampoline, kSandpit, kGoal };
    static constexpr const char* kNames[kCount] = {"schommel", "glijbaan", "wip", "trampoline", "zandbak", "voetbal"};
    // Where Mochi walks to before starting.
    static constexpr float kSpot[kCount][2] = {
        {-100, 40}, {128, 40}, {-150, 110}, {118, 120}, {-60, 168}, {10, 172},
    };

    lv_obj_t* scene_ = nullptr;
    lv_obj_t* swing_seat_ = nullptr;
    lv_obj_t* chains_[2] = {};
    lv_point_precise_t chain_pts_[2][2] = {};
    lv_obj_t* plank_ = nullptr;
    lv_point_precise_t plank_pts_[2] = {};
    lv_obj_t* tramp_bed_ = nullptr;
    lv_obj_t* ball_ = nullptr;
    lv_obj_t* sand_[2] = {};

    float target_x_ = 0, target_y_ = kGroundY;
    int activity_ = -1;
    int pending_ = -1;
    bool send_ = false;
    bool sitting_ = false;
    bool wants_home_ = false;
    bool resting_ = false;
    uint32_t started_ = 0;
    uint32_t now_ = 0;

    static int Index(const std::string& place) {
        for (int i = 0; i < kCount; i++) {
            if (place == kNames[i]) return i;
        }
        if (place == "schommelen") return kSwing;
        if (place == "voetballen" || place == "bal") return kGoal;
        return -1;
    }

    // A line in a small box whose top-left corner is (ox, oy) in screen-centre
    // coordinates; its points are relative to that corner (see P). Keeping the
    // box small keeps redraws small when the line moves.
    static lv_obj_t* Line(lv_obj_t* parent, lv_point_precise_t* pts, int width, uint32_t color,
                          int ox, int oy, int w, int h) {
        lv_obj_t* line = lv_line_create(parent);
        lv_line_set_points(line, pts, 2);
        lv_obj_set_style_line_width(line, width, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_set_size(line, w, h);
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, ox + LV_HOR_RES / 2, oy + LV_VER_RES / 2);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
        return line;
    }

    // Screen-centre coordinates to a point relative to a line box at (ox, oy).
    static lv_point_precise_t P(float x, float y, int ox, int oy) {
        return {static_cast<lv_value_precise_t>(x - ox), static_cast<lv_value_precise_t>(y - oy)};
    }

    // Line boxes: swing chains, seesaw plank.
    static constexpr int kChainX = -170, kChainY = -84, kPlankX = -205, kPlankY = 60;

    void Hotspot(int item, int w, int h, int x, int y) {
        lv_obj_t* spot = lv_obj_create(scene_);
        mochi_draw::Plain(spot);
        lv_obj_set_style_bg_opa(spot, LV_OPA_TRANSP, 0);
        lv_obj_set_size(spot, w, h);
        lv_obj_align(spot, LV_ALIGN_CENTER, x, y);
        lv_obj_set_user_data(spot, reinterpret_cast<void*>(static_cast<intptr_t>(item)));
        lv_obj_add_flag(spot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(spot, &MochiPark::OnItem, LV_EVENT_CLICKED, this);
    }

    void BuildSwing() {
        using namespace mochi_draw;
        Box(scene_, 110, 8, -100, -82, 0xC0392B, 4);   // top bar
        Box(scene_, 8, 120, -152, -24, 0xC0392B, 3);   // posts
        Box(scene_, 8, 120, -48, -24, 0xC0392B, 3);
        for (int i = 0; i < 2; i++) {
            chain_pts_[i][0] = P(-112 + i * 24, -78, kChainX, kChainY);
            chain_pts_[i][1] = P(-112 + i * 24, 10, kChainX, kChainY);
            chains_[i] = Line(scene_, chain_pts_[i], 3, 0x7F8C8D, kChainX, kChainY, 140, 110);
        }
        swing_seat_ = Box(scene_, 40, 8, -100, 12, 0x5D3A1A, 3);
        Hotspot(kSwing, 120, 130, -100, -20);
    }

    void BuildSlide() {
        using namespace mochi_draw;
        Box(scene_, 6, 110, 112, -26, 0x2E86C1, 2);    // ladder rails
        Box(scene_, 6, 110, 140, -26, 0x2E86C1, 2);
        for (int i = 0; i < 5; i++) {
            Box(scene_, 28, 4, 126, -70 + i * 22, 0x2E86C1, 1);
        }
        Box(scene_, 52, 8, 108, -82, 0x2E86C1, 3);     // platform
        static lv_point_precise_t slide[2];
        slide[0] = P(88, -80, 10, -90);
        slide[1] = P(20, 22, 10, -90);
        Line(scene_, slide, 16, 0xF4D03F, 10, -90, 90, 125);
        Hotspot(kSlide, 140, 130, 90, -30);
    }

    void BuildSeesaw() {
        using namespace mochi_draw;
        Box(scene_, 26, 26, -130, 112, 0x8E44AD, 4);   // pivot
        plank_pts_[0] = P(-196, 98, kPlankX, kPlankY);
        plank_pts_[1] = P(-64, 98, kPlankX, kPlankY);
        plank_ = Line(scene_, plank_pts_, 9, 0xD35400, kPlankX, kPlankY, 150, 80);
        Hotspot(kSeesaw, 150, 70, -130, 100);
    }

    void BuildTrampoline() {
        using namespace mochi_draw;
        Box(scene_, 6, 26, 78, 136, 0x34495E, 2);      // legs
        Box(scene_, 6, 26, 158, 136, 0x34495E, 2);
        Box(scene_, 104, 22, 118, 122, 0x2C3E50, LV_RADIUS_CIRCLE);
        tramp_bed_ = Box(scene_, 88, 12, 118, 122, 0x5DADE2, LV_RADIUS_CIRCLE);
        Hotspot(kTrampoline, 120, 70, 118, 115);
    }

    void BuildSandpit() {
        using namespace mochi_draw;
        Box(scene_, 130, 46, -60, 176, 0x8B5A2B, 6);   // frame
        Box(scene_, 116, 34, -60, 176, 0xF4D03F, 4);   // sand
        Box(scene_, 16, 18, -96, 168, 0xE74C3C, 3);    // bucket
        Box(scene_, 4, 22, -24, 166, 0x2E86C1, 1);     // spade
        Hotspot(kSandpit, 140, 60, -60, 172);
    }

    void BuildGoal() {
        using namespace mochi_draw;
        Box(scene_, 74, 5, 80, 146, 0xFFFFFF, 1);      // crossbar
        Box(scene_, 5, 46, 45, 168, 0xFFFFFF, 1);      // posts
        Box(scene_, 5, 46, 115, 168, 0xFFFFFF, 1);
        lv_obj_t* net = Box(scene_, 64, 40, 80, 170, 0xFFFFFF, 0);
        lv_obj_set_style_bg_opa(net, LV_OPA_30, 0);
        Hotspot(kGoal, 120, 70, 70, 165);
    }

    static void OnItem(lv_event_t* e) {
        auto* self = static_cast<MochiPark*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        self->GoTo(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj))), true);
    }

    static void OnHome(lv_event_t* e) {
        static_cast<MochiPark*>(lv_event_get_user_data(e))->wants_home_ = true;
    }

    void GoTo(int item, bool send) {
        activity_ = -1;
        pending_ = item;
        send_ = send;
        target_x_ = kSpot[item][0];
        target_y_ = kSpot[item][1];
    }

    void Animate(uint32_t now, float& x, float& y, int size) {
        float t = (now - started_) / 1000.0f;
        float half = size / 2.0f;
        switch (activity_) {
            case kSwing: {
                float a = 0.45f * std::sin(t * 2.2f);  // radians
                float sx = -100 + 88 * std::sin(a);
                float sy = -78 + 88 * std::cos(a);
                lv_obj_align(swing_seat_, LV_ALIGN_CENTER, static_cast<int>(sx), static_cast<int>(sy) + 2);
                for (int i = 0; i < 2; i++) {
                    chain_pts_[i][1] = P(sx - 12 + i * 24, sy, kChainX, kChainY);
                    lv_line_set_points(chains_[i], chain_pts_[i], 2);
                }
                x = sx;
                y = sy - half + 4;
                sitting_ = true;
                break;
            }
            case kSlide: {
                float phase = std::fmod(t, 4.0f) / 4.0f;
                if (phase < 0.45f) {          // climb the ladder
                    x = 126;
                    y = 40 - (phase / 0.45f) * 125 - half + 30;
                } else if (phase < 0.6f) {    // on the platform
                    x = 100;
                    y = -82 - half;
                } else {                      // whee
                    float k = (phase - 0.6f) / 0.4f;
                    x = 88 + (20 - 88) * k;
                    y = -80 + (22 + 80) * k - half;
                    sitting_ = true;
                }
                break;
            }
            case kSeesaw: {
                float dy = 22 * std::sin(t * 2.5f);
                plank_pts_[0] = P(-196, 98 + dy, kPlankX, kPlankY);
                plank_pts_[1] = P(-64, 98 - dy, kPlankX, kPlankY);
                lv_line_set_points(plank_, plank_pts_, 2);
                x = -180;
                y = 98 + dy - half;
                sitting_ = true;
                break;
            }
            case kTrampoline: {
                float bounce = std::fabs(std::sin(t * 3.0f));
                lv_obj_set_height(tramp_bed_, bounce < 0.15f ? 18 : 12);
                x = 118;
                y = 116 - half - 90 * bounce;
                break;
            }
            case kSandpit:
                x = -60;
                y = 172 - half + 6;
                sitting_ = true;
                break;
            case kGoal:
                x = 10;
                y = 172 - half;
                break;
        }
    }

    // Put moving equipment back at rest.
    void RestEquipment() {
        if (!resting_) {
            lv_obj_align(swing_seat_, LV_ALIGN_CENTER, -100, 12);  // only once, not every frame
            resting_ = true;
            for (int i = 0; i < 2; i++) {
                chain_pts_[i][1] = P(-112 + i * 24, 10, kChainX, kChainY);
                lv_line_set_points(chains_[i], chain_pts_[i], 2);
            }
            plank_pts_[0] = P(-196, 98, kPlankX, kPlankY);
            plank_pts_[1] = P(-64, 98, kPlankX, kPlankY);
            lv_line_set_points(plank_, plank_pts_, 2);
            lv_obj_set_height(tramp_bed_, 12);
        }
    }

    void HideProps() {
        if (ball_ == nullptr) return;
        mochi_draw::Show(ball_, false);
        for (auto* g : sand_) mochi_draw::Show(g, false);
    }

    void UpdateProps(float mx, float my) {
        float t = (now_ - started_) / 1000.0f;
        bool football = activity_ == kGoal;
        mochi_draw::Show(ball_, football);
        if (football) {
            float k = std::fmod(t, 1.6f) / 1.6f;  // kick, roll into the goal, again
            lv_obj_align(ball_, LV_ALIGN_CENTER, static_cast<int>(mx + 30 + 50 * k),
                         static_cast<int>(176 - 30 * std::sin(k * 3.14159f)));
        }
        bool digging = activity_ == kSandpit;
        for (int i = 0; i < 2; i++) {
            mochi_draw::Show(sand_[i], digging);
            if (digging) {
                float k = std::fmod(t * 1.5f + i * 0.5f, 1.0f);
                lv_obj_align(sand_[i], LV_ALIGN_CENTER, static_cast<int>(mx + 30 + 20 * k + i * 6),
                             static_cast<int>(my + 10 - 40 * std::sin(k * 3.14159f)));
            }
        }
    }
};
