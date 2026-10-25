#pragma once

// Mochi's house: four furnished rooms, each a full round screen, joined by
// doors (Woonkamer - Keuken - Badkamer - Slaapkamer; the Woonkamer's left door
// leads outside). Mochi walks from room to room and the view follows him.
//
// Tapping a piece of furniture sends Mochi there to do something (eat, bathe,
// watch tv, play, sleep); the server applies the care and Mochi talks about it.
// Left alone, he wanders around by himself and heads for the room that fits a
// low need, but only taps change his needs.

#include "application.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <esp_random.h>
#include <lvgl.h>
#include <string>

namespace mochi_draw {

inline void Plain(lv_obj_t* obj) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

inline lv_obj_t* Box(lv_obj_t* parent, int w, int h, int x, int y, uint32_t color, int radius = 6) {
    lv_obj_t* obj = lv_obj_create(parent);
    Plain(obj);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_size(obj, w, h);
    lv_obj_align(obj, LV_ALIGN_CENTER, x, y);
    return obj;
}

inline lv_obj_t* Layer(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    Plain(obj);
    lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    return obj;
}

// Only touches the flag on a real change (LVGL redraws on every HIDDEN write).
inline void Show(lv_obj_t* obj, bool visible) {
    bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (visible && hidden) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else if (!visible && !hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

inline lv_obj_t* Label(lv_obj_t* parent, const char* text, int x, int y, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text_static(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y);
    return label;
}

}  // namespace mochi_draw

class MochiHouse {
public:
    enum class Pose { Stand, Sit, Lie, Bath };

    // Background (rooms) goes behind Mochi, foreground (blanket, tub front,
    // props) in front of him; call BuildForeground after Mochi is created.
    void BuildBackground(lv_obj_t* parent) {
        for (int r = 0; r < kRooms; r++) {
            rooms_[r] = BuildRoom(parent, r);
        }
    }

    void BuildForeground(lv_obj_t* parent) {
        using namespace mochi_draw;
        for (int r = 0; r < kRooms; r++) {
            fronts_[r] = Layer(parent);
        }
        // Bathroom: the front of the tub and a rubber duck.
        Box(fronts_[kBathroom], 150, 34, -50, 98, 0xFFFFFF, 16);
        Box(fronts_[kBathroom], 18, 14, -95, 76, 0xFFD93D, LV_RADIUS_CIRCLE);
        // Bedroom: the duvet over whoever lies in bed.
        Box(fronts_[kBedroom], 118, 34, -12, 86, 0x7FB3FF, 12);

        // Activity props, positioned next to Mochi while in use.
        apple_ = Box(parent, 18, 18, 0, 0, 0xE74C3C, LV_RADIUS_CIRCLE);
        Box(apple_, 6, 6, 4, -9, 0x2ECC71, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(apple_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        ball_ = Box(parent, 22, 22, 0, 0, 0xFF6B9D, LV_RADIUS_CIRCLE);
        for (auto*& bubble : bubbles_) {
            bubble = Box(parent, 12, 12, 0, 0, 0xE8F8FF, LV_RADIUS_CIRCLE);
            lv_obj_set_style_bg_opa(bubble, LV_OPA_80, 0);
        }
        HideAll();
    }

    // Entering through the front door: Mochi comes into the living room.
    void Enter(float& mochi_x, float& mochi_y) {
        room_ = kLivingRoom;
        mochi_x = kDoorX[0];
        mochi_y = kFloorY;
        target_room_ = kLivingRoom;
        target_x_ = 0;
        target_y_ = kFloorY;
        activity_ = Act::None;
        pending_item_ = -1;
        last_user_ = lv_tick_get();
        wants_outside_ = false;
        ShowRoom();
    }

    void Leave() { HideAll(); }

    // Called every frame while inside. Moves Mochi between rooms (the view
    // follows), runs activities and decides what he does when left alone.
    void Tick(uint32_t now, const std::string& stage, const std::string& mood, bool asleep,
              float& mochi_x, float& mochi_y) {
        now_ = now;
        bool egg = stage == "ei";

        // Asleep: in bed, whatever else was going on.
        if (asleep && !egg && activity_ != Act::Sleep) {
            GoTo(kBedItem, false);
            pending_sleep_ = true;
        }
        if (!asleep && activity_ == Act::Sleep && now > activity_until_) {
            activity_ = Act::None;  // woke up (or never fell asleep)
        }

        // Walking: to the target room door by door, then to the spot.
        bool arrived = std::fabs(mochi_x - target_x_) < 4 && std::fabs(mochi_y - target_y_) < 4;
        if (room_ != target_room_) {
            int dir = target_room_ > room_ ? 1 : -1;
            float door_x = dir > 0 ? kDoorX[1] : kDoorX[0];
            walk_x_ = door_x;
            walk_y_ = kFloorY;
            if (std::fabs(mochi_x - door_x) < 6) {
                room_ += dir;  // through the door: appear at the opposite one
                mochi_x = dir > 0 ? kDoorX[0] : kDoorX[1];
                mochi_y = kFloorY;
                ShowRoom();
            }
        } else {
            walk_x_ = target_x_;
            walk_y_ = target_y_;
            if (arrived && pending_item_ >= 0) {
                StartActivity(pending_item_, pending_by_user_);
                pending_item_ = -1;
            }
        }

        if (activity_ != Act::None && activity_ != Act::Sleep && now > activity_until_) {
            activity_ = Act::None;
        }

        // Left alone: wander, or go and wait where a low need is solved.
        if (!egg && activity_ == Act::None && pending_item_ < 0 && now - last_user_ > 15000 &&
            now > next_wander_) {
            int need_item = ItemForMood(mood);
            if (need_item >= 0) {
                GoTo(need_item, false);
                autonomous_wait_ = true;
            } else {
                GoTo(static_cast<int>(esp_random() % kItems), false);
            }
            next_wander_ = now + 15000 + esp_random() % 15000;
        }

        UpdateProps(mochi_x, mochi_y);
    }

    float walk_x() const { return walk_x_; }
    float walk_y() const { return walk_y_; }
    Pose pose() const { return pose_; }
    bool wants_outside() const { return wants_outside_; }

private:
    enum Room { kLivingRoom = 0, kKitchen, kBathroom, kBedroom, kRooms };
    enum class Act { None, Eat, Sleep, Bath, Tv, Play, Look };
    static constexpr float kFloorY = 88;
    static constexpr int kDoorX[2] = {-168, 168};
    static constexpr const char* kRoomTitle[kRooms] = {"Woonkamer", "Keuken", "Badkamer", "Slaapkamer"};
    static constexpr uint32_t kWall[kRooms] = {0x6B4E71, 0x3E6B55, 0x3D7A8F, 0x3B3A6B};
    static constexpr uint32_t kFloor[kRooms] = {0x8E5B3E, 0xB08A5A, 0xCFD8DC, 0x6D4C41};

    struct Item {
        int room;
        Act act;
        float x, y;        // where Mochi goes
        Pose pose;
        const char* say;   // sent to the server when the user taps it
    };
    static constexpr int kItems = 7;
    static constexpr int kBedItem = 6;
    static constexpr Item kItemList[kItems] = {
        {kLivingRoom, Act::Tv, 92, 66, Pose::Sit, "[In huis: de gebruiker brengt Mochi naar de woonkamer]"},
        {kLivingRoom, Act::Play, 10, 92, Pose::Stand,
         "[In huis: de gebruiker pakt de bal uit de speelgoedkist om met Mochi te spelen]"},
        {kKitchen, Act::Eat, -55, 88, Pose::Stand, "[In huis: de gebruiker brengt Mochi naar de keuken]"},
        {kKitchen, Act::Eat, 70, 70, Pose::Sit, "[In huis: de gebruiker brengt Mochi naar de keuken]"},
        {kBathroom, Act::Bath, -50, 74, Pose::Bath, "[In huis: de gebruiker brengt Mochi naar de badkamer]"},
        {kBathroom, Act::Look, 60, 88, Pose::Stand, nullptr},
        {kBedroom, Act::Sleep, -40, 70, Pose::Lie, "[In huis: de gebruiker brengt Mochi naar de slaapkamer]"},
    };

    lv_obj_t* rooms_[kRooms] = {};
    lv_obj_t* fronts_[kRooms] = {};
    lv_obj_t* tv_screen_ = nullptr;
    lv_obj_t* fridge_door_ = nullptr;
    lv_obj_t* lamp_light_ = nullptr;
    lv_obj_t* apple_ = nullptr;
    lv_obj_t* ball_ = nullptr;
    lv_obj_t* bubbles_[3] = {};

    int room_ = kLivingRoom;
    int target_room_ = kLivingRoom;
    float target_x_ = 0, target_y_ = kFloorY;
    float walk_x_ = 0, walk_y_ = kFloorY;
    int pending_item_ = -1;
    bool pending_by_user_ = false;
    bool autonomous_wait_ = false;
    bool pending_sleep_ = false;
    Act activity_ = Act::None;
    Pose pose_ = Pose::Stand;
    uint32_t activity_start_ = 0;
    uint32_t activity_until_ = 0;
    uint32_t last_user_ = 0;
    uint32_t next_wander_ = 0;
    uint32_t now_ = 0;
    bool wants_outside_ = false;

    // ---------- building ----------

    lv_obj_t* BuildRoom(lv_obj_t* parent, int r) {
        using namespace mochi_draw;
        lv_obj_t* room = Layer(parent);
        lv_obj_t* wall = Box(room, LV_HOR_RES, LV_VER_RES, 0, 0, kWall[r], LV_RADIUS_CIRCLE);
        (void)wall;
        Box(room, LV_HOR_RES, 200, 0, 140, kFloor[r], 0);
        Box(room, LV_HOR_RES, 6, 0, 40, 0x2A2230, 0);  // skirting board
        Label(room, kRoomTitle[r], 0, -150, 0xFFFFFF);

        // Doors left and right, with where they lead.
        static const char* const kLeft[kRooms] = {"Buiten", "Woonkamer", "Keuken", "Badkamer"};
        static const char* const kRight[kRooms] = {"Keuken", "Badkamer", "Slaapkamer", nullptr};
        for (int side = 0; side < 2; side++) {
            const char* to = side == 0 ? kLeft[r] : kRight[r];
            if (to == nullptr) continue;
            lv_obj_t* door = Box(room, 46, 86, kDoorX[side], -3, 0x5D3A1A, 6);
            Box(door, 6, 6, side == 0 ? 12 : -12, 4, 0xFFD93D, LV_RADIUS_CIRCLE);
            lv_obj_set_user_data(door, reinterpret_cast<void*>(static_cast<intptr_t>(side)));
            lv_obj_add_flag(door, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(door, &MochiHouse::OnDoor, LV_EVENT_CLICKED, this);
            Label(room, to, kDoorX[side] + (side == 0 ? 18 : -18), -62, 0xE0D8F0);
        }

        switch (r) {
            case kLivingRoom: BuildLivingRoom(room); break;
            case kKitchen: BuildKitchen(room); break;
            case kBathroom: BuildBathroom(room); break;
            case kBedroom: BuildBedroom(room); break;
        }
        return room;
    }

    // An invisible tap target over a piece of furniture.
    void Hotspot(lv_obj_t* room, int item, int w, int h, int x, int y) {
        lv_obj_t* spot = lv_obj_create(room);
        mochi_draw::Plain(spot);
        lv_obj_set_style_bg_opa(spot, LV_OPA_TRANSP, 0);
        lv_obj_set_size(spot, w, h);
        lv_obj_align(spot, LV_ALIGN_CENTER, x, y);
        lv_obj_set_user_data(spot, reinterpret_cast<void*>(static_cast<intptr_t>(item)));
        lv_obj_add_flag(spot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(spot, &MochiHouse::OnItem, LV_EVENT_CLICKED, this);
    }

    void Window(lv_obj_t* room, int x, int y, uint32_t sky) {
        using namespace mochi_draw;
        lv_obj_t* frame = Box(room, 74, 54, x, y, 0xF5E6C8, 4);
        Box(frame, 66, 46, 0, 0, sky, 2);
        Box(frame, 4, 46, 0, 0, 0xF5E6C8, 0);
        Box(frame, 66, 4, 0, 0, 0xF5E6C8, 0);
    }

    void BuildLivingRoom(lv_obj_t* room) {
        using namespace mochi_draw;
        Window(room, -55, -85, 0x87CEEB);
        lv_obj_t* frame = Box(room, 46, 36, 60, -88, 0xD4A373, 3);  // painting
        Box(frame, 38, 28, 0, 0, 0xF6BD60, 2);
        Box(frame, 14, 14, -6, 3, 0x2ECC71, LV_RADIUS_CIRCLE);
        Box(room, 200, 34, 0, 128, 0xA44A3F, LV_RADIUS_CIRCLE);  // rug
        Box(room, 92, 26, -95, 70, 0x5D3A1A, 4);                // tv cabinet
        Box(room, 80, 52, -95, 32, 0x222222, 5);                // tv
        tv_screen_ = Box(room, 70, 42, -95, 32, 0x2C3E50, 3);
        Box(room, 120, 22, 92, 58, 0xA93226, 10);               // couch back
        Box(room, 120, 26, 92, 80, 0xC0392B, 10);               // couch seat
        Box(room, 14, 34, 38, 76, 0x922B21, 6);                 // armrests
        Box(room, 14, 34, 146, 76, 0x922B21, 6);
        Box(room, 50, 30, 10, 112, 0xE67E22, 5);                // toy box
        Box(room, 50, 6, 10, 98, 0xCA6F1E, 2);
        Box(room, 16, 16, 18, 92, 0xFF6B9D, LV_RADIUS_CIRCLE);  // ball on top
        Hotspot(room, 0, 170, 90, 30, 50);                      // tv + couch
        Hotspot(room, 1, 70, 50, 10, 105);                      // toy box
    }

    void BuildKitchen(lv_obj_t* room) {
        using namespace mochi_draw;
        Window(room, 40, -88, 0x87CEEB);
        Box(room, 110, 34, -45, -55, 0x7E5A3C, 4);              // wall cupboards
        Box(room, 2, 30, -45, -55, 0x5D3A1A, 0);
        Box(room, 50, 112, -118, 32, 0xDDE6EA, 8);              // fridge
        fridge_door_ = Box(room, 46, 108, -118, 32, 0xF2F6F8, 8);
        Box(fridge_door_, 4, 22, 16, -20, 0x95A5A6, 2);
        Box(room, 62, 52, -50, 62, 0x555555, 6);                // stove
        Box(room, 16, 16, -62, 42, 0xE74C3C, LV_RADIUS_CIRCLE);
        Box(room, 16, 16, -38, 42, 0x888888, LV_RADIUS_CIRCLE);
        Box(room, 30, 10, -38, 32, 0x34495E, 4);                // pan
        Box(room, 110, 10, 75, 52, 0xA0522D, 3);                // table top
        Box(room, 8, 50, 30, 80, 0x8B4513, 2);                  // legs
        Box(room, 8, 50, 120, 80, 0x8B4513, 2);
        Box(room, 28, 8, 75, 44, 0xFFFFFF, 4);                  // plate
        Box(room, 14, 14, 75, 36, 0xE74C3C, LV_RADIUS_CIRCLE);  // apple
        Hotspot(room, 2, 120, 120, -90, 35);                    // fridge + stove
        Hotspot(room, 3, 120, 80, 75, 60);                      // table
    }

    void BuildBathroom(lv_obj_t* room) {
        using namespace mochi_draw;
        for (int i = 0; i < 5; i++) {                           // tiles
            Box(room, 360, 2, 0, -100 + i * 28, 0x5DADC2, 0);
        }
        Box(room, 56, 70, 60, -80, 0xBFE6F2, LV_RADIUS_CIRCLE); // mirror
        Box(room, 66, 18, 60, -14, 0xFFFFFF, 8);                // sink
        Box(room, 18, 50, 60, 18, 0xFFFFFF, 6);
        Box(room, 150, 50, -50, 72, 0xF0F0F0, 22);              // tub back
        Box(room, 132, 16, -50, 64, 0x6FD3FF, 8);               // water
        Box(room, 6, 70, 128, 40, 0x95A5A6, 2);                 // towel rail
        Box(room, 30, 54, 120, 50, 0xF1948A, 4);                // towel
        Hotspot(room, 4, 160, 80, -50, 75);                     // tub
        Hotspot(room, 5, 80, 120, 60, -20);                     // sink
    }

    void BuildBedroom(lv_obj_t* room) {
        using namespace mochi_draw;
        Window(room, 55, -88, 0x1B2A49);
        Box(room, 16, 16, 70, -96, 0xF7DC6F, LV_RADIUS_CIRCLE); // moon
        Box(room, 20, 74, -104, 52, 0x8B5A2B, 6);               // headboard
        Box(room, 176, 24, -24, 96, 0x8B5A2B, 6);               // frame
        Box(room, 166, 20, -24, 80, 0xF5F5F5, 8);               // mattress
        Box(room, 40, 18, -80, 66, 0xFFFFFF, LV_RADIUS_CIRCLE); // pillow
        Box(room, 42, 44, 96, 82, 0xA0522D, 4);                 // nightstand
        Box(room, 6, 26, 96, 48, 0x555555, 2);                  // lamp
        lamp_light_ = Box(room, 30, 20, 96, 32, 0xF9E79F, 10);
        Hotspot(room, 6, 190, 80, -24, 78);                     // bed
    }

    // ---------- behaviour ----------

    static void OnItem(lv_event_t* e) {
        auto* self = static_cast<MochiHouse*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        int item = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
        self->last_user_ = lv_tick_get();
        self->GoTo(item, true);
    }

    static void OnDoor(lv_event_t* e) {
        auto* self = static_cast<MochiHouse*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        int side = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
        self->last_user_ = lv_tick_get();
        self->activity_ = Act::None;
        self->pending_item_ = -1;
        if (side == 0 && self->room_ == kLivingRoom) {
            self->wants_outside_ = true;
            return;
        }
        int to = self->room_ + (side == 0 ? -1 : 1);
        self->target_room_ = to;
        self->target_x_ = 0;
        self->target_y_ = kFloorY;
    }

    void GoTo(int item, bool by_user) {
        const Item& it = kItemList[item];
        if (activity_ == Act::Sleep && !by_user) return;
        activity_ = Act::None;
        pose_ = Pose::Stand;
        target_room_ = it.room;
        target_x_ = it.x;
        target_y_ = it.y;
        pending_item_ = item;
        pending_by_user_ = by_user;
        autonomous_wait_ = false;
        pending_sleep_ = false;
    }

    void StartActivity(int item, bool by_user) {
        const Item& it = kItemList[item];
        activity_ = it.act;
        pose_ = it.pose;
        // On his own Mochi only looks around: eating, bathing and napping are
        // real care and come from a tap (or, for sleep, from the server).
        bool pretend = !by_user && !(pending_sleep_ && it.act == Act::Sleep) &&
                       (autonomous_wait_ || it.act == Act::Eat || it.act == Act::Bath ||
                        it.act == Act::Sleep);
        if (pretend) {
            activity_ = Act::Look;
            pose_ = Pose::Stand;
        }
        pending_sleep_ = false;
        activity_start_ = now_;
        uint32_t duration = by_user ? 9000 : 6000;
        if (activity_ == Act::Tv) duration = by_user ? 15000 : 10000;
        if (activity_ == Act::Sleep) duration = 8000;  // then only while the server says asleep
        activity_until_ = now_ + duration;
        if (by_user && it.say != nullptr) {
            Application::GetInstance().SendTextEvent(it.say);
        }
    }

    static int ItemForMood(const std::string& mood) {
        if (mood == "honger") return 2;
        if (mood == "moe") return kBedItem;
        if (mood == "vies") return 4;
        if (mood == "verveling") return 1;
        return -1;
    }

    void ShowRoom() {
        for (int r = 0; r < kRooms; r++) {
            mochi_draw::Show(rooms_[r], r == room_);
            if (fronts_[r] != nullptr) mochi_draw::Show(fronts_[r], r == room_);
        }
    }

    void HideAll() {
        for (int r = 0; r < kRooms; r++) {
            if (rooms_[r] != nullptr) mochi_draw::Show(rooms_[r], false);
            if (fronts_[r] != nullptr) mochi_draw::Show(fronts_[r], false);
        }
        if (apple_ != nullptr) {
            mochi_draw::Show(apple_, false);
            mochi_draw::Show(ball_, false);
            for (auto* b : bubbles_) mochi_draw::Show(b, false);
        }
    }

    // Props and room details that follow the current activity.
    void UpdateProps(float mx, float my) {
        using mochi_draw::Show;
        float t = (now_ - activity_start_) / 1000.0f;
        bool here = room_ == target_room_;

        bool eating = here && activity_ == Act::Eat;
        Show(apple_, eating);
        if (eating) {
            int size = std::max(4, 18 - static_cast<int>(t * 2));
            lv_obj_set_size(apple_, size, size);
            lv_obj_align(apple_, LV_ALIGN_CENTER, static_cast<int>(mx) + 26, static_cast<int>(my) + 8);
        }
        bool at_fridge = here && room_ == kKitchen && (activity_ == Act::Eat || activity_ == Act::Look) &&
                         target_x_ < 0;
        lv_obj_set_width(fridge_door_, at_fridge ? 14 : 46);
        lv_obj_align(fridge_door_, LV_ALIGN_CENTER, at_fridge ? -136 : -118, 32);

        bool playing = here && activity_ == Act::Play;
        Show(ball_, playing);
        if (playing) {
            int bounce = static_cast<int>(40 * std::fabs(std::sin(t * 4.0f)));
            lv_obj_align(ball_, LV_ALIGN_CENTER, static_cast<int>(mx) + 40, static_cast<int>(my) + 20 - bounce);
        }

        bool bathing = here && activity_ == Act::Bath;
        for (int i = 0; i < 3; i++) {
            Show(bubbles_[i], bathing);
            if (bathing) {
                float phase = std::fmod(t * 0.7f + i * 0.33f, 1.0f);
                lv_obj_align(bubbles_[i], LV_ALIGN_CENTER, static_cast<int>(mx) - 30 + i * 30,
                             static_cast<int>(my) - 10 - static_cast<int>(phase * 70));
                lv_obj_set_style_bg_opa(bubbles_[i], static_cast<lv_opa_t>(255 * (1 - phase)), 0);
            }
        }

        bool watching = here && room_ == kLivingRoom && (activity_ == Act::Tv || activity_ == Act::Look) &&
                        target_x_ > 50;
        static const uint32_t kTv[4] = {0x6FD3FF, 0xFF9F43, 0x2ECC71, 0xFF6B9D};
        lv_obj_set_style_bg_color(tv_screen_, lv_color_hex(watching ? kTv[(now_ / 700) % 4] : 0x2C3E50), 0);

        bool sleeping = here && activity_ == Act::Sleep;
        lv_obj_set_style_bg_opa(lamp_light_, sleeping ? LV_OPA_20 : LV_OPA_COVER, 0);

        pose_ = (activity_ == Act::None || !here) ? Pose::Stand : pose_;
    }
};
