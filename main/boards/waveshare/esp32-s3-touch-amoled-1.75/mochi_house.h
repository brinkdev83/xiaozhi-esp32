#pragma once

// Mochi's house: four furnished rooms, each a full round screen, joined by
// doors (Woonkamer - Keuken - Badkamer - Slaapkamer; the Woonkamer's left door
// leads outside). Tapping a door switches rooms at once.
//
// Mochi only does things when asked: tap a piece of furniture (the server then
// applies the care and Mochi talks about it), or tell him by voice, which
// reaches the device as the MCP tool self.mochi.ga_naar. The one thing he does
// by himself is lie in bed while the server says he is asleep.

#include "application.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

inline lv_obj_t* Circle(lv_obj_t* parent, int d, int x, int y, uint32_t color) {
    return Box(parent, d, d, x, y, color, LV_RADIUS_CIRCLE);
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
        // Bathroom: the front of the tub, with a rubber duck on the rim.
        Box(fronts_[kBathroom], 176, 40, -45, 104, 0xFFFFFF, 18);
        Box(fronts_[kBathroom], 160, 6, -45, 88, 0xE8F4F8, 3);
        lv_obj_t* duck = Circle(fronts_[kBathroom], 20, -112, 78, 0xFFD93D);
        Circle(duck, 4, 5, -3, 0x322038);
        Box(fronts_[kBathroom], 10, 6, -99, 80, 0xFF9F43, 3);
        // Bedroom: the duvet over whoever lies in bed.
        lv_obj_t* duvet = Box(fronts_[kBedroom], 140, 40, -12, 92, 0x7FB3FF, 14);
        for (int i = 0; i < 4; i++) {
            Circle(duvet, 10, -45 + i * 30, 0, 0xB3D4FF);
        }

        // Activity props, positioned next to Mochi while in use.
        apple_ = Circle(parent, 22, 0, 0, 0xE74C3C);
        Circle(apple_, 7, 5, -11, 0x2ECC71);
        lv_obj_add_flag(apple_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        glass_ = Box(parent, 16, 24, 0, 0, 0xD6EAF8, 3);
        glass_water_ = Box(glass_, 12, 16, 0, 3, 0x5DADE2, 2);
        lv_obj_align(glass_water_, LV_ALIGN_BOTTOM_MID, 0, -2);
        pill_ = Box(parent, 18, 9, 0, 0, 0xFFFFFF, LV_RADIUS_CIRCLE);
        Box(pill_, 9, 9, -4, 0, 0xE74C3C, LV_RADIUS_CIRCLE);
        ball_ = Circle(parent, 26, 0, 0, 0xFF6B9D);
        Box(ball_, 26, 5, 0, 0, 0xFFFFFF, 2);
        for (auto*& bubble : bubbles_) {
            bubble = Circle(parent, 14, 0, 0, 0xE8F8FF);
            lv_obj_set_style_bg_opa(bubble, LV_OPA_80, 0);
        }
        HideAll();
    }

    // Entering through the front door: Mochi comes into the living room.
    void Enter(float& mochi_x, float& mochi_y) {
        SwitchRoom(kLivingRoom);
        mochi_x = kEntryX[0];
        mochi_y = kFloorY;
        target_x_ = 0;
        target_y_ = kFloorY;
        activity_ = Act::None;
        pose_ = Pose::Stand;
        pending_item_ = -1;
        wants_outside_ = false;
    }

    void Leave() { HideAll(); }

    // A voice command (MCP tool self.mochi.ga_naar): a room, a piece of
    // furniture or "buiten". Returns false for an unknown place. The server
    // already handled the care, so nothing is sent back.
    bool GoToPlace(const std::string& place) {
        static const struct { const char* name; int room; int item; } kPlaces[] = {
            {"woonkamer", kLivingRoom, -1}, {"keuken", kKitchen, -1},
            {"badkamer", kBathroom, -1},    {"slaapkamer", kBedroom, -1},
            {"tv", kLivingRoom, 0},         {"bank", kLivingRoom, 0},
            {"bal", kLivingRoom, 1},        {"speelgoed", kLivingRoom, 1},
            {"koelkast", kKitchen, 2},      {"fornuis", kKitchen, 2},
            {"tafel", kKitchen, 3},         {"eten", kKitchen, 3},
            {"bad", kBathroom, 4},          {"wastafel", kBathroom, 5},
            {"bed", kBedroom, kBedItem},
            {"drinken", kKitchen, 7},       {"glas", kKitchen, 7},
            {"water", kKitchen, 7},         {"medicijn", kBathroom, 8},
            {"medicijnkastje", kBathroom, 8},
        };
        if (place == "buiten") {
            wants_outside_ = true;
            return true;
        }
        for (const auto& p : kPlaces) {
            if (place == p.name) {
                if (p.item >= 0) {
                    GoTo(p.item, false);
                } else {
                    activity_ = Act::None;
                    pose_ = Pose::Stand;
                    pending_item_ = -1;
                    target_room_ = p.room;
                    target_x_ = 0;
                    target_y_ = kFloorY;
                }
                return true;
            }
        }
        return false;
    }

    // Called every frame while inside: room changes, activities, props.
    void Tick(uint32_t now, const std::string& stage, bool asleep, float& mochi_x, float& mochi_y) {
        now_ = now;
        bool egg = stage == "ei";

        // Asleep (because he was sent to bed): he stays in bed until awake.
        if (asleep && !egg && activity_ != Act::Sleep && pending_item_ != kBedItem) {
            GoTo(kBedItem, false);
        }
        if (!asleep && activity_ == Act::Sleep && now > activity_until_) {
            activity_ = Act::None;
            pose_ = Pose::Stand;
        }

        // Another room: switch at once, Mochi steps in at the nearest door.
        if (room_ != target_room_) {
            bool forward = target_room_ > room_;
            SwitchRoom(target_room_);
            mochi_x = forward ? kEntryX[0] : kEntryX[1];
            mochi_y = kFloorY;
        }
        if (teleport_) {
            mochi_x = teleport_x_;
            mochi_y = kFloorY;
            teleport_ = false;
        }

        bool arrived = std::fabs(mochi_x - target_x_) < 3 && std::fabs(mochi_y - target_y_) < 3;
        if (arrived && pending_item_ >= 0) {
            StartActivity(pending_item_, pending_send_);
            pending_item_ = -1;
        }
        if (activity_ != Act::None && activity_ != Act::Sleep && now > activity_until_) {
            activity_ = Act::None;
            pose_ = Pose::Stand;
        }

        UpdateProps(mochi_x, mochi_y);
    }

    float walk_x() const { return target_x_; }
    float walk_y() const { return target_y_; }
    Pose pose() const { return pose_; }
    bool wants_outside() const { return wants_outside_; }

private:
    enum Room { kLivingRoom = 0, kKitchen, kBathroom, kBedroom, kRooms };
    enum class Act { None, Eat, Sleep, Bath, Tv, Play, Wash, Drink, Medicine };
    static constexpr float kFloorY = 92;
    static constexpr int kDoorX[2] = {-172, 172};
    static constexpr float kEntryX[2] = {-120, 120};  // just inside the left/right door
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
    static constexpr int kItems = 9;
    static constexpr int kBedItem = 6;
    static constexpr Item kItemList[kItems] = {
        {kLivingRoom, Act::Tv, 72, 62, Pose::Sit, "[In huis: de gebruiker brengt Mochi naar de woonkamer]"},
        {kLivingRoom, Act::Play, -30, 96, Pose::Stand,
         "[In huis: de gebruiker pakt de bal uit de speelgoedkist om met Mochi te spelen]"},
        {kKitchen, Act::Eat, -45, 92, Pose::Stand, "[In huis: de gebruiker brengt Mochi naar de keuken]"},
        {kKitchen, Act::Eat, 72, 70, Pose::Sit, "[In huis: de gebruiker brengt Mochi naar de keuken]"},
        {kBathroom, Act::Bath, -45, 80, Pose::Bath, "[In huis: de gebruiker brengt Mochi naar de badkamer]"},
        {kBathroom, Act::Wash, 72, 92, Pose::Stand, nullptr},
        {kBedroom, Act::Sleep, -38, 74, Pose::Lie, "[In huis: de gebruiker brengt Mochi naar de slaapkamer]"},
        {kKitchen, Act::Drink, 100, 70, Pose::Sit, "[In huis: de gebruiker geeft Mochi een glas water]"},
        {kBathroom, Act::Medicine, -10, 92, Pose::Stand,
         "[In huis: de gebruiker geeft Mochi een medicijn uit het medicijnkastje]"},
    };

    lv_obj_t* rooms_[kRooms] = {};
    lv_obj_t* fronts_[kRooms] = {};
    lv_obj_t* tv_screen_ = nullptr;
    lv_obj_t* fridge_door_ = nullptr;
    lv_obj_t* lamp_light_ = nullptr;
    lv_obj_t* tap_water_ = nullptr;
    lv_obj_t* apple_ = nullptr;
    lv_obj_t* ball_ = nullptr;
    lv_obj_t* glass_ = nullptr;
    lv_obj_t* glass_water_ = nullptr;
    lv_obj_t* pill_ = nullptr;
    lv_obj_t* bubbles_[3] = {};

    int room_ = kLivingRoom;
    int target_room_ = kLivingRoom;
    float target_x_ = 0, target_y_ = kFloorY;
    bool teleport_ = false;
    float teleport_x_ = 0;
    int pending_item_ = -1;
    bool pending_send_ = false;
    Act activity_ = Act::None;
    Pose pose_ = Pose::Stand;
    uint32_t activity_start_ = 0;
    uint32_t activity_until_ = 0;
    uint32_t now_ = 0;
    bool wants_outside_ = false;
    bool fridge_open_ = false;
    bool lamp_dim_ = false;
    uint32_t tv_color_ = 0;

    // ---------- building ----------

    lv_obj_t* BuildRoom(lv_obj_t* parent, int r) {
        using namespace mochi_draw;
        lv_obj_t* room = Layer(parent);
        // Square on purpose: the panel is round already, and a screen-sized
        // circle mask makes every redraw behind Mochi expensive.
        Box(room, LV_HOR_RES, LV_VER_RES, 0, 0, kWall[r], 0);
        Box(room, LV_HOR_RES, 210, 0, 135, kFloor[r], 0);
        for (int i = 0; i < 4; i++) {  // floorboards / tiles
            lv_obj_t* line = Box(room, LV_HOR_RES, 2, 0, 62 + i * 40, 0x000000, 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_20, 0);
        }
        Box(room, LV_HOR_RES, 8, 0, 30, 0x2A2230, 0);  // skirting board
        Label(room, kRoomTitle[r], 0, -158, 0xFFFFFF);

        // Doors left and right, with where they lead.
        static const char* const kLeft[kRooms] = {"Buiten", "Woonkamer", "Keuken", "Badkamer"};
        static const char* const kRight[kRooms] = {"Keuken", "Badkamer", "Slaapkamer", nullptr};
        for (int side = 0; side < 2; side++) {
            const char* to = side == 0 ? kLeft[r] : kRight[r];
            if (to == nullptr) continue;
            lv_obj_t* door = Box(room, 52, 104, kDoorX[side], -20, 0x5D3A1A, 8);
            Box(door, 38, 40, 0, -22, 0x6E4524, 4);  // panels
            Box(door, 38, 34, 0, 24, 0x6E4524, 4);
            Circle(door, 8, side == 0 ? 15 : -15, 4, 0xFFD93D);
            lv_obj_set_user_data(door, reinterpret_cast<void*>(static_cast<intptr_t>(side)));
            lv_obj_set_ext_click_area(door, 34);  // easy to hit near the edge
            lv_obj_add_flag(door, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(door, &MochiHouse::OnDoor, LV_EVENT_CLICKED, this);
            Label(room, to, kDoorX[side] + (side == 0 ? 22 : -22), -88, 0xE0D8F0);
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

    void Window(lv_obj_t* room, int x, int y, uint32_t sky, bool curtains = true) {
        using namespace mochi_draw;
        lv_obj_t* frame = Box(room, 92, 66, x, y, 0xF5E6C8, 5);
        Box(frame, 82, 56, 0, 0, sky, 3);
        Box(frame, 5, 56, 0, 0, 0xF5E6C8, 0);
        Box(frame, 82, 5, 0, 0, 0xF5E6C8, 0);
        if (curtains) {
            Box(room, 22, 80, x - 52, y + 4, 0xE07A5F, 8);
            Box(room, 22, 80, x + 52, y + 4, 0xE07A5F, 8);
        }
    }

    void BuildLivingRoom(lv_obj_t* room) {
        using namespace mochi_draw;
        Window(room, -40, -100, 0x87CEEB);
        lv_obj_t* frame = Box(room, 64, 50, 78, -100, 0xD4A373, 4);  // painting
        Box(frame, 54, 40, 0, 0, 0xF6BD60, 2);
        Circle(frame, 18, -8, 4, 0x2ECC71);
        Circle(frame, 12, 14, -8, 0xFFFFFF);
        Box(room, 250, 44, 0, 136, 0xA44A3F, LV_RADIUS_CIRCLE);  // rug
        Box(room, 210, 30, 0, 136, 0xC0634F, LV_RADIUS_CIRCLE);
        Box(room, 116, 34, -75, 66, 0x5D3A1A, 5);                // tv cabinet
        Box(room, 50, 4, -75, 66, 0x3E2410, 2);
        Box(room, 104, 68, -75, 8, 0x222222, 6);                 // tv
        tv_screen_ = Box(room, 94, 58, -75, 8, 0x2C3E50, 4);
        Box(room, 12, 10, -75, 47, 0x222222, 2);                 // tv foot
        Box(room, 130, 30, 72, 44, 0xA93226, 12);                // couch back
        Box(room, 130, 34, 72, 74, 0xC0392B, 12);                // couch seat
        Box(room, 20, 48, 14, 70, 0x922B21, 8);                  // armrests
        Box(room, 20, 48, 130, 70, 0x922B21, 8);
        Box(room, 30, 22, 50, 54, 0xF4D03F, 8);                  // cushions
        Box(room, 30, 22, 96, 54, 0x5DADE2, 8);
        Box(room, 64, 36, -30, 128, 0xE67E22, 6);                // toy box
        Box(room, 64, 8, -30, 110, 0xCA6F1E, 3);
        Circle(room, 20, -42, 100, 0xFF6B9D);                    // ball and blocks
        Box(room, 14, 14, -20, 104, 0x5DADE2, 2);
        Hotspot(room, 0, 260, 110, 0, 40);                       // tv + couch
        Hotspot(room, 1, 90, 60, -30, 118);                      // toy box
    }

    void BuildKitchen(lv_obj_t* room) {
        using namespace mochi_draw;
        Window(room, 40, -100, 0x87CEEB);
        Box(room, 126, 40, -55, -62, 0x7E5A3C, 5);               // wall cupboards
        Box(room, 3, 34, -55, -62, 0x5D3A1A, 0);
        Circle(room, 6, -62, -62, 0xD4AC0D);
        Circle(room, 6, -48, -62, 0xD4AC0D);
        Box(room, 60, 136, -112, 28, 0xDDE6EA, 10);              // fridge
        fridge_door_ = Box(room, 56, 132, -112, 28, 0xF2F6F8, 10);
        Box(fridge_door_, 5, 28, 20, -30, 0x95A5A6, 2);
        Box(fridge_door_, 56, 3, 0, -20, 0xD5DBDB, 0);
        Box(room, 74, 64, -44, 62, 0x555555, 8);                 // stove
        Box(room, 74, 10, -44, 34, 0x333333, 4);
        Circle(room, 20, -58, 38, 0xE74C3C);
        Circle(room, 20, -30, 38, 0x888888);
        Box(room, 40, 12, -30, 26, 0x34495E, 5);                 // pan
        Box(room, 22, 4, -6, 24, 0x34495E, 2);
        Circle(room, 8, -44, 74, 0x222222);                      // oven window
        Box(room, 130, 12, 72, 52, 0xA0522D, 4);                 // table top
        Box(room, 10, 58, 18, 84, 0x8B4513, 3);                  // legs
        Box(room, 10, 58, 126, 84, 0x8B4513, 3);
        Box(room, 36, 10, 72, 44, 0xFFFFFF, 5);                  // plate + apple
        Circle(room, 18, 72, 34, 0xE74C3C);
        Box(room, 14, 22, 108, 36, 0x85C1E9, 3);                 // glass of water
        Hotspot(room, 2, 140, 150, -80, 30);                     // fridge + stove
        Hotspot(room, 3, 150, 90, 72, 60);                       // table
        Hotspot(room, 7, 50, 60, 108, 36);                       // glass of water
    }

    void BuildBathroom(lv_obj_t* room) {
        using namespace mochi_draw;
        for (int i = 0; i < 6; i++) {                            // tiles
            Box(room, 400, 2, 0, -130 + i * 30, 0x5DADC2, 0);
        }
        for (int i = 0; i < 7; i++) {
            Box(room, 2, 170, -150 + i * 50, -60, 0x5DADC2, 0);
        }
        Box(room, 70, 88, 72, -82, 0xBFE6F2, LV_RADIUS_CIRCLE);  // mirror
        Box(room, 60, 78, 72, -82, 0xDDF3FA, LV_RADIUS_CIRCLE);
        Box(room, 84, 22, 72, -14, 0xFFFFFF, 10);                // sink
        Box(room, 22, 58, 72, 24, 0xFFFFFF, 8);
        Box(room, 8, 14, 72, -30, 0x95A5A6, 3);                  // tap
        tap_water_ = Box(room, 4, 16, 72, -16, 0x6FD3FF, 2);
        Box(room, 176, 58, -45, 76, 0xF0F0F0, 26);               // tub back
        Box(room, 156, 18, -45, 64, 0x6FD3FF, 9);                // water
        Box(room, 8, 40, -128, 50, 0x95A5A6, 3);                 // tub tap
        Box(room, 6, 84, 136, 30, 0x95A5A6, 3);                  // towel rail
        Box(room, 34, 64, 128, 40, 0xF1948A, 6);                 // towel
        Box(room, 34, 8, 128, 30, 0xFFFFFF, 2);
        lv_obj_t* cabinet = Box(room, 54, 46, -30, -92, 0xFFFFFF, 6);  // medicine cabinet
        Box(cabinet, 8, 26, 0, 0, 0xE74C3C, 1);
        Box(cabinet, 26, 8, 0, 0, 0xE74C3C, 1);
        Hotspot(room, 4, 190, 90, -45, 80);                      // tub
        Hotspot(room, 8, 70, 60, -30, -92);                      // medicine cabinet
        Hotspot(room, 5, 100, 140, 72, -20);                     // sink
    }

    void BuildBedroom(lv_obj_t* room) {
        using namespace mochi_draw;
        Window(room, 60, -100, 0x1B2A49);
        Circle(room, 20, 78, -112, 0xF7DC6F);                    // moon
        Circle(room, 5, 40, -90, 0xFFFFFF);                      // stars
        Circle(room, 4, 56, -118, 0xFFFFFF);
        Box(room, 24, 92, -116, 46, 0x8B5A2B, 8);                // headboard
        Box(room, 204, 28, -20, 104, 0x8B5A2B, 8);               // frame
        Box(room, 192, 24, -20, 86, 0xF5F5F5, 10);               // mattress
        Box(room, 50, 22, -84, 70, 0xFFFFFF, LV_RADIUS_CIRCLE);  // pillow
        Box(room, 50, 50, 112, 86, 0xA0522D, 6);                 // nightstand
        Box(room, 50, 4, 112, 80, 0x7E3F1F, 2);
        Box(room, 8, 34, 112, 46, 0x555555, 3);                  // lamp
        lamp_light_ = Box(room, 40, 26, 112, 26, 0xF9E79F, 12);
        Box(room, 58, 10, -90, -40, 0x8B5A2B, 3);                // shelf with books
        Box(room, 10, 24, -110, -57, 0xE74C3C, 2);
        Box(room, 10, 28, -98, -59, 0x3498DB, 2);
        Box(room, 10, 22, -86, -56, 0xF1C40F, 2);
        Circle(room, 20, -68, -56, 0xD35400);                    // teddy
        Hotspot(room, 6, 220, 90, -20, 84);                      // bed
    }

    // ---------- behaviour ----------

    static void OnItem(lv_event_t* e) {
        auto* self = static_cast<MochiHouse*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        self->GoTo(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj))), true);
    }

    static void OnDoor(lv_event_t* e) {
        auto* self = static_cast<MochiHouse*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        int side = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
        self->activity_ = Act::None;
        self->pose_ = Pose::Stand;
        self->pending_item_ = -1;
        if (side == 0 && self->room_ == kLivingRoom) {
            self->wants_outside_ = true;
            return;
        }
        self->SwitchRoom(self->room_ + (side == 0 ? -1 : 1));
        self->teleport_ = true;  // Mochi is already through the door
        self->teleport_x_ = side == 0 ? kEntryX[1] : kEntryX[0];
        self->target_x_ = 0;
        self->target_y_ = kFloorY;
    }

    // Walk to a piece of furniture; `send` tells the server (a tap).
    void GoTo(int item, bool send) {
        const Item& it = kItemList[item];
        activity_ = Act::None;
        pose_ = Pose::Stand;
        target_room_ = it.room;
        target_x_ = it.x;
        target_y_ = it.y;
        pending_item_ = item;
        pending_send_ = send;
    }

    void StartActivity(int item, bool send) {
        const Item& it = kItemList[item];
        activity_ = it.act;
        pose_ = it.pose;
        activity_start_ = now_;
        uint32_t duration = 9000;
        if (activity_ == Act::Tv) duration = 15000;
        if (activity_ == Act::Sleep) duration = 8000;  // then only while the server says asleep
        activity_until_ = now_ + duration;
        if (send && it.say != nullptr) {
            Application::GetInstance().SendTextEvent(it.say);
        }
    }

    void SwitchRoom(int room) {
        room_ = room;
        target_room_ = room;
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
            mochi_draw::Show(glass_, false);
            mochi_draw::Show(pill_, false);
            for (auto* b : bubbles_) mochi_draw::Show(b, false);
        }
    }

    // Props and room details that follow the current activity.
    void UpdateProps(float mx, float my) {
        using mochi_draw::Show;
        float t = (now_ - activity_start_) / 1000.0f;

        bool eating = activity_ == Act::Eat;
        Show(apple_, eating);
        if (eating) {
            int size = std::max(5, 22 - static_cast<int>(t * 2));
            lv_obj_set_size(apple_, size, size);
            lv_obj_align(apple_, LV_ALIGN_CENTER, static_cast<int>(mx) + 34, static_cast<int>(my) + 10);
        }
        bool at_fridge = eating && room_ == kKitchen && target_x_ < 0;
        if (at_fridge != fridge_open_) {
            fridge_open_ = at_fridge;
            lv_obj_set_width(fridge_door_, at_fridge ? 16 : 56);
            lv_obj_align(fridge_door_, LV_ALIGN_CENTER, at_fridge ? -134 : -112, 28);
        }

        bool playing = activity_ == Act::Play;
        Show(ball_, playing);
        if (playing) {
            int bounce = static_cast<int>(50 * std::fabs(std::sin(t * 4.0f)));
            lv_obj_align(ball_, LV_ALIGN_CENTER, static_cast<int>(mx) + 52, static_cast<int>(my) + 24 - bounce);
        }

        bool bathing = activity_ == Act::Bath;
        for (int i = 0; i < 3; i++) {
            Show(bubbles_[i], bathing);
            if (bathing) {
                float phase = std::fmod(t * 0.7f + i * 0.33f, 1.0f);
                lv_obj_align(bubbles_[i], LV_ALIGN_CENTER, static_cast<int>(mx) - 36 + i * 36,
                             static_cast<int>(my) - 14 - static_cast<int>(phase * 80));
                lv_obj_set_style_bg_opa(bubbles_[i], static_cast<lv_opa_t>(255 * (1 - phase)), 0);
            }
        }

        Show(tap_water_, activity_ == Act::Wash);

        bool drinking = activity_ == Act::Drink;
        Show(glass_, drinking);
        if (drinking) {
            lv_obj_align(glass_, LV_ALIGN_CENTER, static_cast<int>(mx) + 30, static_cast<int>(my) + 4);
            lv_obj_set_height(glass_water_, std::max(1, 16 - static_cast<int>(t * 2)));
        }
        bool medicine = activity_ == Act::Medicine;
        Show(pill_, medicine && t < 4);
        if (medicine) {
            lv_obj_align(pill_, LV_ALIGN_CENTER, static_cast<int>(mx) + 26 - static_cast<int>(t * 5),
                         static_cast<int>(my) + 6);
        }

        static const uint32_t kTv[4] = {0x6FD3FF, 0xFF9F43, 0x2ECC71, 0xFF6B9D};
        uint32_t screen = activity_ == Act::Tv ? kTv[(now_ / 700) % 4] : 0x2C3E50;
        if (screen != tv_color_) {
            tv_color_ = screen;
            lv_obj_set_style_bg_color(tv_screen_, lv_color_hex(screen), 0);
        }

        bool sleeping = activity_ == Act::Sleep;
        if (sleeping != lamp_dim_) {
            lamp_dim_ = sleeping;
            lv_obj_set_style_bg_opa(lamp_light_, sleeping ? LV_OPA_20 : LV_OPA_COVER, 0);
        }
    }
};
