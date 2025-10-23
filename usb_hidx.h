#include "esphome.h"
#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char *TAG = "usb_hidx";

// Forward declarations
void update_keyboard_leds();
void led_control_callback(usb_transfer_t *transfer);
void setup_media_interface();
void setup_mouse_interface();
void set_switch_player_leds();
void ctrl_transfer_cb(usb_transfer_t *transfer);
void send_switch_command(uint8_t cmd, const uint8_t* data, uint8_t len);
void init_switch_controller();
void poll_switch_controller();

static usb_host_client_handle_t client_hdl;
static usb_device_handle_t dev_hdl;
static uint8_t switch_packet_counter = 0;
static bool is_official_switch = false;
static uint64_t last_switch_poll = 0;
static uint8_t rumble_data[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
static usb_transfer_t *active_transfers[3] = {nullptr, nullptr, nullptr};

// HID keyboard report structure
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} __attribute__((packed)) hid_keyboard_report_t;

// USB HID keyboard descriptor
static const uint8_t hid_keyboard_report_desc[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
};

// Convert HID keycode to ASCII
char hid_to_ascii(uint8_t keycode, bool shift) {
    if (keycode >= 0x04 && keycode <= 0x1D) {
        // Letters a-z
        char c = 'a' + (keycode - 0x04);
        // Apply caps lock XOR shift for letters
        bool make_uppercase = shift ^ id(caps_lock_state);
        return make_uppercase ? (c - 32) : c;
    } else if (keycode >= 0x1E && keycode <= 0x27) {
        // Numbers 1-0
        const char numbers[] = "1234567890";
        const char shifted[] = "!@#$%^&*()";
        return shift ? shifted[keycode - 0x1E] : numbers[keycode - 0x1E];
    } else {
        // Special characters and punctuation
        switch (keycode) {
            case 0x2C: return ' ';                    // Space
            case 0x28: return '\n';                   // Enter
            case 0x2A: return '\b';                   // Backspace
            case 0x2D: return shift ? '_' : '-';      // Minus/Underscore
            case 0x2E: return shift ? '+' : '=';      // Equal/Plus
            case 0x2F: return shift ? '{' : '[';      // Left Bracket
            case 0x30: return shift ? '}' : ']';      // Right Bracket
            case 0x31: return shift ? '|' : '\\';     // Backslash/Pipe
            case 0x33: return shift ? ':' : ';';      // Semicolon/Colon
            case 0x34: return shift ? '"' : '\'';    // Apostrophe/Quote
            case 0x35: return shift ? '~' : '`';      // Grave/Tilde
            case 0x36: return shift ? '<' : ',';      // Comma/Less
            case 0x37: return shift ? '>' : '.';      // Period/Greater
            case 0x38: return shift ? '?' : '/';      // Slash/Question
            case 0x2B: return '\t';                   // Tab
            
            // Numeric keypad (when Num Lock is ON)
            case 0x59: return id(num_lock_state) ? '1' : 0;  // Keypad 1/End
            case 0x5A: return id(num_lock_state) ? '2' : 0;  // Keypad 2/Down
            case 0x5B: return id(num_lock_state) ? '3' : 0;  // Keypad 3/PgDn
            case 0x5C: return id(num_lock_state) ? '4' : 0;  // Keypad 4/Left
            case 0x5D: return id(num_lock_state) ? '5' : 0;  // Keypad 5
            case 0x5E: return id(num_lock_state) ? '6' : 0;  // Keypad 6/Right
            case 0x5F: return id(num_lock_state) ? '7' : 0;  // Keypad 7/Home
            case 0x60: return id(num_lock_state) ? '8' : 0;  // Keypad 8/Up
            case 0x61: return id(num_lock_state) ? '9' : 0;  // Keypad 9/PgUp
            case 0x62: return id(num_lock_state) ? '0' : 0;  // Keypad 0/Insert
            case 0x63: return id(num_lock_state) ? '.' : 0;  // Keypad ./Delete
            case 0x54: return '/';                           // Keypad /
            case 0x55: return '*';                           // Keypad *
            case 0x56: return '-';                           // Keypad -
            case 0x57: return '+';                           // Keypad +
            case 0x58: return '\n';                          // Keypad Enter
            
            default: return 0;
        }
    }
}

// Process keyboard report
void process_keyboard_report(const hid_keyboard_report_t* report) {
    static uint8_t prev_keys[6] = {0};
    static bool prev_shift = false;
    bool shift = (report->modifier & 0x22) != 0; // Left or right shift
    
    // Log modifier keys for debugging
    if (report->modifier != 0) {
        ESP_LOGI(TAG, "Modifier keys: 0x%02X (LCtrl:%d LShift:%d LAlt:%d LGui:%d RCtrl:%d RShift:%d RAlt:%d RGui:%d)",
                 report->modifier,
                 (report->modifier & 0x01) ? 1 : 0, // Left Ctrl
                 (report->modifier & 0x02) ? 1 : 0, // Left Shift
                 (report->modifier & 0x04) ? 1 : 0, // Left Alt
                 (report->modifier & 0x08) ? 1 : 0, // Left GUI
                 (report->modifier & 0x10) ? 1 : 0, // Right Ctrl
                 (report->modifier & 0x20) ? 1 : 0, // Right Shift
                 (report->modifier & 0x40) ? 1 : 0, // Right Alt
                 (report->modifier & 0x80) ? 1 : 0  // Right GUI
        );
    }
    
    // Process each key in current report
    for (int i = 0; i < 6; i++) {
        if (report->keycode[i] != 0) {
            // Check if this key was NOT in the previous report (new press)
            bool was_pressed = false;
            for (int j = 0; j < 6; j++) {
                if (prev_keys[j] == report->keycode[i]) {
                    was_pressed = true;
                    break;
                }
            }
            
            // Only process if this is a new key press OR shift state changed
            if (!was_pressed || (shift != prev_shift)) {
                // Log ALL key presses for debugging
                ESP_LOGI(TAG, "Key detected: 0x%02X", report->keycode[i]);
                
                // Handle special keys FIRST (before ASCII conversion)
                if (report->keycode[i] == 0x39) { // Caps Lock
                    id(caps_lock_state) = !id(caps_lock_state);
                    ESP_LOGI(TAG, "Caps Lock pressed! State now: %s", id(caps_lock_state) ? "ON" : "OFF");
                    update_keyboard_leds();
                } else if (report->keycode[i] == 0x53) { // Num Lock
                    id(num_lock_state) = !id(num_lock_state);
                    ESP_LOGI(TAG, "Num Lock pressed! State now: %s", id(num_lock_state) ? "ON" : "OFF");
                    update_keyboard_leds();
                } else if (report->keycode[i] == 0x47) { // Scroll Lock
                    id(scroll_lock_state) = !id(scroll_lock_state);
                    ESP_LOGI(TAG, "Scroll Lock pressed! State now: %s", id(scroll_lock_state) ? "ON" : "OFF");
                    update_keyboard_leds();
                } else {
                    // Check for media keys first
                    const char* media_key = nullptr;
                    switch (report->keycode[i]) {
                        case 0x81: media_key = "Volume Up"; break;
                        case 0x82: media_key = "Volume Down"; break;
                        case 0x83: media_key = "Mute"; break;
                        case 0xB5: media_key = "Next Track"; break;
                        case 0xB6: media_key = "Previous Track"; break;
                        case 0xB7: media_key = "Stop"; break;
                        case 0xCD: media_key = "Play/Pause"; break;
                        case 0x65: media_key = "Menu"; break;
                        case 0x66: media_key = "Power"; break;
                        case 0x67: media_key = "Sleep"; break;
                        case 0x68: media_key = "Wake"; break;
                        case 0x8A: media_key = "Mail"; break;
                        case 0x94: media_key = "My Computer"; break;
                        case 0x92: media_key = "Calculator"; break;
                        case 0x40: media_key = "F13"; break;
                        case 0x41: media_key = "F14"; break;
                        case 0x42: media_key = "F15"; break;
                        case 0x43: media_key = "F16"; break;
                        case 0x44: media_key = "F17"; break;
                        case 0x45: media_key = "F18"; break;
                        case 0x46: media_key = "F19"; break;
                        case 0x47: media_key = "F20"; break;
                        case 0x48: media_key = "F21"; break;
                        case 0x49: media_key = "F22"; break;
                        case 0x4A: media_key = "F23"; break;
                        case 0x4B: media_key = "F24"; break;
                    }
                    
                    if (media_key) {
                        ESP_LOGI(TAG, "Media key pressed: %s (0x%02X)", media_key, report->keycode[i]);
                    } else {
                        // Check for ESC key
                        if (report->keycode[i] == 0x29) {
                            id(keyboard_esc_pressed) = true;
                            id(keyboard_esc_sensor).publish_state(true);
                        }
                        // Check for Enter key
                        else if (report->keycode[i] == 0x28) {
                            id(keyboard_enter_pressed) = true;
                            id(keyboard_enter_sensor).publish_state(true);
                        }
                        
                        // Handle regular keys with ASCII conversion
                        char ascii = hid_to_ascii(report->keycode[i], shift);
                        if (ascii != 0) {
                            std::string current = id(keyboard_buffer);
                            if (ascii == '\b') {
                                if (!current.empty()) {
                                    current.pop_back();
                                    id(keyboard_buffer) = current;
                                }
                            } else if (ascii == '\n') {
                                ESP_LOGI(TAG, "Keyboard input: %s", current.c_str());
                                id(keyboard_buffer) = "";
                            } else {
                                current += ascii;
                                id(keyboard_buffer) = current;
                            }
                            
                            // Update text sensor immediately
                            id(keyboard_input).publish_state(id(keyboard_buffer));
                        }
                    }
                }
            }
        }
    }
    
    // Reset Enter/ESC if keys released
    bool enter_still_pressed = false;
    bool esc_still_pressed = false;
    for (int i = 0; i < 6; i++) {
        if (report->keycode[i] == 0x28) enter_still_pressed = true;
        if (report->keycode[i] == 0x29) esc_still_pressed = true;
    }
    if (!enter_still_pressed && id(keyboard_enter_pressed)) {
        id(keyboard_enter_pressed) = false;
        id(keyboard_enter_sensor).publish_state(false);
    }
    if (!esc_still_pressed && id(keyboard_esc_pressed)) {
        id(keyboard_esc_pressed) = false;
        id(keyboard_esc_sensor).publish_state(false);
    }
    
    // Save current state for next comparison
    memcpy(prev_keys, report->keycode, 6);
    prev_shift = shift;
}

// Mouse callback (0x81) - for boot protocol mice
void mouse_transfer_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= 3) {
        uint8_t buttons = transfer->data_buffer[0];
        int8_t x_delta = (int8_t)transfer->data_buffer[1];
        int8_t y_delta = (int8_t)transfer->data_buffer[2];
        int8_t wheel = (transfer->actual_num_bytes >= 4) ? (int8_t)transfer->data_buffer[3] : 0;
        
        static uint8_t last_buttons = 0;
        
        if (buttons != last_buttons) {
            // Left button
            if ((buttons & 0x01) && !(last_buttons & 0x01)) {
                ESP_LOGI(TAG, "Mouse: Left Click");
                id(mouse_left_button) = true;
                id(mouse_left_sensor).publish_state(true);
            }
            if (!(buttons & 0x01) && (last_buttons & 0x01)) {
                ESP_LOGI(TAG, "Mouse: Left Release");
                id(mouse_left_button) = false;
                id(mouse_left_sensor).publish_state(false);
            }
            // Right button
            if ((buttons & 0x02) && !(last_buttons & 0x02)) {
                ESP_LOGI(TAG, "Mouse: Right Click");
                id(mouse_right_button) = true;
                id(mouse_right_sensor).publish_state(true);
            }
            if (!(buttons & 0x02) && (last_buttons & 0x02)) {
                ESP_LOGI(TAG, "Mouse: Right Release");
                id(mouse_right_button) = false;
                id(mouse_right_sensor).publish_state(false);
            }
            if ((buttons & 0x04) && !(last_buttons & 0x04)) ESP_LOGI(TAG, "Mouse: Middle Click");
            if (!(buttons & 0x04) && (last_buttons & 0x04)) ESP_LOGI(TAG, "Mouse: Middle Release");
            
            last_buttons = buttons;
        }
        
        if (x_delta != 0 || y_delta != 0) {
            ESP_LOGI(TAG, "Mouse: Movement X=%d Y=%d", x_delta, y_delta);
        }
        
        if (wheel != 0) {
            ESP_LOGI(TAG, "Mouse: Wheel %s", wheel > 0 ? "Up" : "Down");
        }
    }
    usb_host_transfer_submit(transfer);
}

// Set Switch controller rumble (freq: 0-1252Hz, amp: 0.0-1.0)
void set_switch_rumble(float freq_low, float amp_low, float freq_high, float amp_high) {
    if (!is_official_switch) return;
    
    // Encode rumble (simplified - uses fixed values for strong rumble)
    if (amp_low > 0 || amp_high > 0) {
        // Strong rumble
        rumble_data[0] = 0x28;
        rumble_data[1] = 0x88;
        rumble_data[2] = 0x60;
        rumble_data[3] = 0x61;
        rumble_data[4] = 0x28;
        rumble_data[5] = 0x88;
        rumble_data[6] = 0x60;
        rumble_data[7] = 0x61;
    } else {
        // No rumble
        rumble_data[0] = 0x00;
        rumble_data[1] = 0x01;
        rumble_data[2] = 0x40;
        rumble_data[3] = 0x40;
        rumble_data[4] = 0x00;
        rumble_data[5] = 0x01;
        rumble_data[6] = 0x40;
        rumble_data[7] = 0x40;
    }
}

// Poll official Switch controller
void poll_switch_controller() {
    if (!is_official_switch) return;
    
    uint64_t now = esp_timer_get_time() / 1000;
    if (now - last_switch_poll < 15) return;  // Poll every 15ms
    last_switch_poll = now;
    
    // Send request for input report (empty command keeps connection alive)
    send_switch_command(0x00, nullptr, 0);
}

// Gamepad callback - Switch Pro Controller
void gamepad_transfer_cb(usb_transfer_t *transfer) {
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        usb_host_transfer_submit(transfer);
        return;
    }
    
    // Poll official controller
    poll_switch_controller();
    
    // Official controller: 64 bytes with report ID 0x30 or 0x21 (standard full mode)
    // Third-party: 8 bytes, no report ID
    bool is_official = (transfer->actual_num_bytes == 64 && (transfer->data_buffer[0] == 0x30 || transfer->data_buffer[0] == 0x21));
    int offset = is_official ? 3 : 0;  // Official: [report_id, timer, battery_conn, buttons...]
    
    if (transfer->actual_num_bytes >= (offset + 6)) {
        static uint8_t last_buttons[3] = {0};
        
        // Official: buttons at offset 3,4,5 | Third-party: 0,1,2
        uint8_t btn_right = transfer->data_buffer[offset];     // Y,X,B,A,R,ZR
        uint8_t btn_shared = transfer->data_buffer[offset + 1]; // Minus,Plus,RStick,LStick,Home,Capture
        uint8_t btn_left = transfer->data_buffer[offset + 2];   // Down,Up,Right,Left,L,ZL
        
        // Extract D-pad from left buttons (bits 0-3)
        uint8_t dpad = 0x0F;
        if (btn_left & 0x01) dpad = 4;      // Down
        else if (btn_left & 0x02) dpad = 0; // Up
        if (btn_left & 0x04) dpad = 2;      // Right
        else if (btn_left & 0x08) dpad = 6; // Left
        if ((btn_left & 0x01) && (btn_left & 0x04)) dpad = 3; // Down-Right
        if ((btn_left & 0x01) && (btn_left & 0x08)) dpad = 5; // Down-Left
        if ((btn_left & 0x02) && (btn_left & 0x04)) dpad = 1; // Up-Right
        if ((btn_left & 0x02) && (btn_left & 0x08)) dpad = 7; // Up-Left
        
        // Analog sticks (official uses 12-bit values across 3 bytes per stick)
        uint16_t lx, ly, rx, ry;
        if (is_official) {
            // Left stick: bytes 6-8 contain 12-bit X and Y
            lx = (transfer->data_buffer[6] | ((transfer->data_buffer[7] & 0x0F) << 8));
            ly = ((transfer->data_buffer[7] >> 4) | (transfer->data_buffer[8] << 4));
            // Right stick: bytes 9-11 contain 12-bit X and Y
            rx = (transfer->data_buffer[9] | ((transfer->data_buffer[10] & 0x0F) << 8));
            ry = ((transfer->data_buffer[10] >> 4) | (transfer->data_buffer[11] << 4));
        } else {
            lx = transfer->data_buffer[offset + 3];
            ly = transfer->data_buffer[offset + 4];
            rx = transfer->data_buffer[offset + 5];
            ry = transfer->data_buffer[offset + 6];
        }
        
        // D-Pad
        static uint8_t last_dpad = 0x0F;
        if (dpad != last_dpad && dpad != 0x0F) {
            const char* dir[] = {"Up", "Up-Right", "Right", "Down-Right", "Down", "Down-Left", "Left", "Up-Left"};
            if (dpad < 8) ESP_LOGI(TAG, "D-Pad: %s", dir[dpad]);
            last_dpad = dpad;
        } else if (dpad == 0x0F && last_dpad != 0x0F) {
            last_dpad = 0x0F;
        }
        
        // Right buttons (Y,X,B,A,R,ZR)
        if (btn_right != last_buttons[0]) {
            if ((btn_right & 0x01) && !(last_buttons[0] & 0x01)) ESP_LOGI(TAG, "Button: Y");
            if ((btn_right & 0x02) && !(last_buttons[0] & 0x02)) ESP_LOGI(TAG, "Button: X");
            // Button B
            if ((btn_right & 0x04) && !(last_buttons[0] & 0x04)) {
                ESP_LOGI(TAG, "Button: B");
                id(gamepad_button_b) = true;
                id(gamepad_b_sensor).publish_state(true);
            }
            if (!(btn_right & 0x04) && (last_buttons[0] & 0x04)) {
                id(gamepad_button_b) = false;
                id(gamepad_b_sensor).publish_state(false);
            }
            // Button A
            if ((btn_right & 0x08) && !(last_buttons[0] & 0x08)) {
                ESP_LOGI(TAG, "Button: A");
                id(gamepad_button_a) = true;
                id(gamepad_a_sensor).publish_state(true);
            }
            if (!(btn_right & 0x08) && (last_buttons[0] & 0x08)) {
                id(gamepad_button_a) = false;
                id(gamepad_a_sensor).publish_state(false);
            }
            if ((btn_right & 0x40) && !(last_buttons[0] & 0x40)) ESP_LOGI(TAG, "Button: R");
            if ((btn_right & 0x80) && !(last_buttons[0] & 0x80)) ESP_LOGI(TAG, "Button: ZR");
            last_buttons[0] = btn_right;
        }
        
        // Shared buttons
        if (btn_shared != last_buttons[1]) {
            if ((btn_shared & 0x01) && !(last_buttons[1] & 0x01)) ESP_LOGI(TAG, "Button: Minus");
            if ((btn_shared & 0x02) && !(last_buttons[1] & 0x02)) ESP_LOGI(TAG, "Button: Plus");
            if ((btn_shared & 0x04) && !(last_buttons[1] & 0x04)) ESP_LOGI(TAG, "Button: R-Stick");
            if ((btn_shared & 0x08) && !(last_buttons[1] & 0x08)) ESP_LOGI(TAG, "Button: L-Stick");
            // Button Home
            if ((btn_shared & 0x10) && !(last_buttons[1] & 0x10)) {
                ESP_LOGI(TAG, "Button: Home - Rumble ON");
                id(gamepad_button_home) = true;
                id(gamepad_home_sensor).publish_state(true);
                set_switch_rumble(160, 1.0, 320, 1.0);
            }
            if (!(btn_shared & 0x10) && (last_buttons[1] & 0x10)) {
                ESP_LOGI(TAG, "Button: Home Released - Rumble OFF");
                id(gamepad_button_home) = false;
                id(gamepad_home_sensor).publish_state(false);
                set_switch_rumble(0, 0, 0, 0);
            }
            if ((btn_shared & 0x20) && !(last_buttons[1] & 0x20)) ESP_LOGI(TAG, "Button: Capture");
            last_buttons[1] = btn_shared;
        }
        
        // Left buttons (L, ZL)
        if (btn_left != last_buttons[2]) {
            if ((btn_left & 0x40) && !(last_buttons[2] & 0x40)) ESP_LOGI(TAG, "Button: L");
            if ((btn_left & 0x80) && !(last_buttons[2] & 0x80)) ESP_LOGI(TAG, "Button: ZL");
            last_buttons[2] = btn_left;
        }
        
        // Analog sticks with proper 12-bit parsing and deadzone
        static uint16_t last_lx = 2048, last_ly = 2048, last_rx = 2048, last_ry = 2048;
        static bool first_read = true;
        
        if (first_read) {
            last_lx = lx;
            last_ly = ly;
            last_rx = rx;
            last_ry = ry;
            first_read = false;
            ESP_LOGI(TAG, "Stick center: L(%d,%d) R(%d,%d)", lx, ly, rx, ry);
        }
        
        // Only log significant movements (>300 units from last position)
        if (abs((int)lx - (int)last_lx) > 300 || abs((int)ly - (int)last_ly) > 300) {
            ESP_LOGI(TAG, "Left Stick: X=%d Y=%d", lx, ly);
            last_lx = lx;
            last_ly = ly;
        }
        if (abs((int)rx - (int)last_rx) > 300 || abs((int)ry - (int)last_ry) > 300) {
            ESP_LOGI(TAG, "Right Stick: X=%d Y=%d", rx, ry);
            last_rx = rx;
            last_ry = ry;
        }
    }
    usb_host_transfer_submit(transfer);
}

// Keyboard callback (0x81)
void keyboard_transfer_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= sizeof(hid_keyboard_report_t)) {
        hid_keyboard_report_t* report = (hid_keyboard_report_t*)transfer->data_buffer;
        process_keyboard_report(report);
    }
    usb_host_transfer_submit(transfer);
}

// Media/Touchpad callback (0x82) - handles both
void media_transfer_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
        uint8_t report_id = transfer->data_buffer[0];
        
        // Debug: log all reports (uncomment for troubleshooting)
        // if (transfer->actual_num_bytes <= 8) {
        //     ESP_LOGI(TAG, "0x82 Report: ID=0x%02X len=%d [%02X %02X %02X %02X %02X %02X %02X %02X]",
        //             report_id, transfer->actual_num_bytes,
        //             transfer->data_buffer[0], transfer->data_buffer[1], transfer->data_buffer[2], transfer->data_buffer[3],
        //             transfer->data_buffer[4], transfer->data_buffer[5], transfer->data_buffer[6], transfer->data_buffer[7]);
        // }
        
        // Touchpad: Report ID = button state (0x00=none, 0x01=left, 0x02=right)
        // Byte 1 = X delta, Byte 2 = Y delta (both relative movement)
        if (transfer->actual_num_bytes >= 4) {
            static uint8_t last_report_id = 0;
            int8_t x_delta = (int8_t)transfer->data_buffer[1];
            int8_t y_delta = (int8_t)transfer->data_buffer[2];
            
            // Handle button state changes
            if (report_id != last_report_id) {
                if (report_id == 0x01) {
                    ESP_LOGI(TAG, "Touchpad: Left Click");
                    id(touchpad_clicked) = true;
                    id(touchpad_click_sensor).publish_state(true);
                } else if (last_report_id == 0x01) {
                    ESP_LOGI(TAG, "Touchpad: Left Release");
                    id(touchpad_clicked) = false;
                    id(touchpad_click_sensor).publish_state(false);
                }
                if (report_id == 0x02) ESP_LOGI(TAG, "Touchpad: Right Click");
                if (last_report_id == 0x02) ESP_LOGI(TAG, "Touchpad: Right Release");
                last_report_id = report_id;
            }
            
            // Update position with deltas
            if (x_delta != 0 || y_delta != 0) {
                id(touchpad_x) += x_delta;
                id(touchpad_y) += y_delta;
                ESP_LOGI(TAG, "Touchpad: X=%d Y=%d (delta X=%d Y=%d)", (int)id(touchpad_x), (int)id(touchpad_y), x_delta, y_delta);
            }
        } else if (report_id == 0x02 && transfer->actual_num_bytes >= 8) {
            uint8_t buttons = transfer->data_buffer[1];
            uint16_t x_raw = (uint16_t)transfer->data_buffer[2] | ((uint16_t)transfer->data_buffer[3] << 8);
            uint16_t y_raw = (uint16_t)transfer->data_buffer[4] | ((uint16_t)transfer->data_buffer[5] << 8);
            
            uint16_t x_coord = x_raw & 0x0FFF;
            uint16_t y_coord = y_raw & 0x0FFF;
            
            static uint8_t last_buttons = 0;
            static uint16_t last_x = 0;
            static uint16_t last_y = 0;
            static uint16_t click_x = 0;
            static uint16_t click_y = 0;
            
            // Track position when finger is on touchpad (not 0,0)
            if (x_coord != 0 || y_coord != 0) {
                click_x = x_coord;
                click_y = y_coord;
            }
            
            if (buttons != last_buttons) {
                if ((buttons & 0x01) && !(last_buttons & 0x01)) {
                    ESP_LOGI(TAG, "Touchpad: Left Click at X=%d Y=%d", click_x, click_y);
                    id(touchpad_clicked) = true;
                    id(touchpad_click_sensor).publish_state(true);
                }
                if (!(buttons & 0x01) && (last_buttons & 0x01)) {
                    ESP_LOGI(TAG, "Touchpad: Left Release");
                    id(touchpad_clicked) = false;
                    id(touchpad_click_sensor).publish_state(false);
                }
                if ((buttons & 0x02) && !(last_buttons & 0x02)) ESP_LOGI(TAG, "Touchpad: Right Click at X=%d Y=%d", click_x, click_y);
                if (!(buttons & 0x02) && (last_buttons & 0x02)) ESP_LOGI(TAG, "Touchpad: Right Release");
                if ((buttons & 0x04) && !(last_buttons & 0x04)) ESP_LOGI(TAG, "Touchpad: Middle Click at X=%d Y=%d", click_x, click_y);
                if (!(buttons & 0x04) && (last_buttons & 0x04)) ESP_LOGI(TAG, "Touchpad: Middle Release");
                
                last_buttons = buttons;
            }
            
            if ((x_coord != 0 || y_coord != 0) && (abs((int)x_coord - (int)last_x) > 200 || abs((int)y_coord - (int)last_y) > 200)) {
                ESP_LOGI(TAG, "Touchpad: Position X=%d Y=%d", x_coord, y_coord);
                id(touchpad_x) = x_coord;
                id(touchpad_y) = y_coord;
                last_x = x_coord;
                last_y = y_coord;
            }
        } else if (report_id == 0x03) {
            // Media keys (Report ID 0x03)
            for (int i = 1; i < transfer->actual_num_bytes; i++) {  // Start at byte 1 (skip report ID)
                uint8_t key = transfer->data_buffer[i];
                if (key == 0) continue; // Skip empty bytes
                
                const char* media_name = nullptr;
                switch (key) {
                    case 0xE9: media_name = "Volume Up"; break;
                    case 0xEA: media_name = "Volume Down"; break;
                    case 0xE2: media_name = "Mute"; break;
                    case 0xCD: media_name = "Play/Pause"; break;
                    case 0xB5: media_name = "Next Track"; break;
                    case 0xB6: media_name = "Previous Track"; break;
                    case 0xB7: media_name = "Stop"; break;
                    case 0x8A: media_name = "Mail"; break;
                    case 0x92: media_name = "Calculator"; break;
                    case 0x94: media_name = "My Computer"; break;
                    case 0x23: media_name = "WWW Home"; break;
                    case 0x21: media_name = "WWW Search"; break;
                    case 0x24: media_name = "WWW Back"; break;
                    case 0x25: media_name = "WWW Forward"; break;
                    default: media_name = "Unknown"; break;
                }
                ESP_LOGI(TAG, "Media key: %s (0x%02X)", media_name, key);
            }
        }
        // Silently ignore unknown report IDs (uncomment for troubleshooting)
        // else {
        //     ESP_LOGI(TAG, "Unknown report ID 0x%02X, %d bytes", report_id, transfer->actual_num_bytes);
        // }
    }
    usb_host_transfer_submit(transfer);
}

// Touchpad callback (0x83)
void touchpad_transfer_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= 8) {
        uint8_t buttons = transfer->data_buffer[0];
        uint16_t x_coord = (uint16_t)((transfer->data_buffer[4] << 8) | transfer->data_buffer[3]);
        
        static uint8_t last_buttons = 0;
        static uint16_t last_x = 0;
        
        if (buttons != last_buttons) {
            if ((buttons & 0x02) && !(last_buttons & 0x02)) {
                ESP_LOGI(TAG, "Touchpad: Click");
                id(touchpad_clicked) = true;
                id(touchpad_click_sensor).publish_state(true);
            }
            if (!(buttons & 0x02) && (last_buttons & 0x02)) {
                ESP_LOGI(TAG, "Touchpad: Release");
                id(touchpad_clicked) = false;
                id(touchpad_click_sensor).publish_state(false);
            }
            last_buttons = buttons;
        }
        
        if (abs((int)x_coord - (int)last_x) > 1000) {
            ESP_LOGI(TAG, "Touchpad: Movement X=%d", x_coord);
            id(touchpad_x) = x_coord;
            last_x = x_coord;
        }
    }
    usb_host_transfer_submit(transfer);
}

// USB client event callback
void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV: {
            ESP_LOGI(TAG, "New USB device detected (address: %d)", event_msg->new_dev.address);
            
            // Make sure previous device is cleaned up
            if (dev_hdl) {
                ESP_LOGW(TAG, "Previous device still open, cleaning up first");
                usb_host_interface_release(client_hdl, dev_hdl, 0);
                usb_host_interface_release(client_hdl, dev_hdl, 1);
                usb_host_interface_release(client_hdl, dev_hdl, 2);
                usb_host_device_close(client_hdl, dev_hdl);
                dev_hdl = NULL;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // Open new device
            esp_err_t err = usb_host_device_open(client_hdl, event_msg->new_dev.address, &dev_hdl);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
                return;
            }
            
            // Get device descriptor
            const usb_device_desc_t *dev_desc;
            err = usb_host_get_device_descriptor(dev_hdl, &dev_desc);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get device descriptor: %s", esp_err_to_name(err));
                return;
            }
            
            ESP_LOGI(TAG, "Device VID:PID = %04X:%04X", dev_desc->idVendor, dev_desc->idProduct);
            ESP_LOGI(TAG, "Device Class: 0x%02X, SubClass: 0x%02X, Protocol: 0x%02X", 
                     dev_desc->bDeviceClass, dev_desc->bDeviceSubClass, dev_desc->bDeviceProtocol);
            
            // Check if it's a keyboard (HID class, boot interface subclass, keyboard protocol)
            if (dev_desc->bDeviceClass == 0x03 || dev_desc->bDeviceClass == 0x00) {
                ESP_LOGI(TAG, "HID device detected, setting up keyboard monitoring");
                
                // Get configuration descriptor
                const usb_config_desc_t *config_desc;
                err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to get config descriptor: %s", esp_err_to_name(err));
                    return;
                }
                
                // Find the HID interface and interrupt endpoint
                const usb_intf_desc_t *intf_desc = NULL;
                const usb_ep_desc_t *ep_desc = NULL;
                int offset = 0;
                
                // Parse configuration descriptor to find ALL HID interfaces
                ESP_LOGI(TAG, "Enumerating all interfaces in device:");
                while (offset < config_desc->wTotalLength) {
                    const usb_standard_desc_t *desc = (const usb_standard_desc_t *)((uint8_t *)config_desc + offset);
                    
                    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                        const usb_intf_desc_t *temp_intf = (const usb_intf_desc_t *)desc;
                        ESP_LOGI(TAG, "Interface %d: Class=0x%02X, SubClass=0x%02X, Protocol=0x%02X", 
                                temp_intf->bInterfaceNumber, temp_intf->bInterfaceClass, 
                                temp_intf->bInterfaceSubClass, temp_intf->bInterfaceProtocol);
                        
                        if (temp_intf->bInterfaceClass == 0x03) { // HID class
                            // Check for keyboard (protocol 0x01), mouse (0x02), or gamepad (0x00)
                            if (temp_intf->bInterfaceNumber == 0) {
                                intf_desc = temp_intf;
                                if (temp_intf->bInterfaceProtocol == 0x02) {
                                    ESP_LOGI(TAG, "Selected HID interface %d as mouse", intf_desc->bInterfaceNumber);
                                } else if (temp_intf->bInterfaceProtocol == 0x01) {
                                    ESP_LOGI(TAG, "Selected HID interface %d as keyboard", intf_desc->bInterfaceNumber);
                                } else {
                                    ESP_LOGI(TAG, "Selected HID interface %d as gamepad/generic HID", intf_desc->bInterfaceNumber);
                                }
                                break;
                            }
                        }
                    }
                    offset += desc->bLength;
                }
                
                if (!intf_desc) {
                    ESP_LOGE(TAG, "No suitable HID interface found");
                    // Let's try to use ANY HID interface as fallback
                    offset = 0;
                    while (offset < config_desc->wTotalLength) {
                        const usb_standard_desc_t *desc = (const usb_standard_desc_t *)((uint8_t *)config_desc + offset);
                        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                            const usb_intf_desc_t *temp_intf = (const usb_intf_desc_t *)desc;
                            if (temp_intf->bInterfaceClass == 0x03) {
                                intf_desc = temp_intf;
                                ESP_LOGI(TAG, "Using fallback HID interface %d", intf_desc->bInterfaceNumber);
                                break;
                            }
                        }
                        offset += desc->bLength;
                    }
                    
                    if (!intf_desc) {
                        ESP_LOGE(TAG, "No HID interface found at all");
                        return;
                    }
                }
                
                // Find interrupt IN endpoint
                offset = (uint8_t *)intf_desc - (uint8_t *)config_desc + intf_desc->bLength;
                while (offset < config_desc->wTotalLength) {
                    const usb_standard_desc_t *desc = (const usb_standard_desc_t *)((uint8_t *)config_desc + offset);
                    
                    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                        ep_desc = (const usb_ep_desc_t *)desc;
                        if ((ep_desc->bEndpointAddress & 0x80) && // IN endpoint
                            ((ep_desc->bmAttributes & 0x03) == 0x03)) { // Interrupt transfer
                            ESP_LOGI(TAG, "Found interrupt IN endpoint: 0x%02X", ep_desc->bEndpointAddress);
                            break;
                        }
                    } else if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                        break; // Next interface, stop looking
                    }
                    offset += desc->bLength;
                }
                
                if (!ep_desc) {
                    ESP_LOGE(TAG, "No interrupt IN endpoint found");
                    return;
                }
                
                // Claim the HID interface before accessing endpoints
                err = usb_host_interface_claim(client_hdl, dev_hdl, intf_desc->bInterfaceNumber, 0);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to claim interface %d: %s", intf_desc->bInterfaceNumber, esp_err_to_name(err));
                    return;
                }
                ESP_LOGI(TAG, "Successfully claimed HID interface %d", intf_desc->bInterfaceNumber);
                
                // Only send boot protocol commands to actual boot protocol devices
                // Protocol 0x01 = keyboard, 0x02 = mouse, 0x00 = none/report protocol
                bool is_boot_device = (intf_desc->bInterfaceProtocol == 0x01 || intf_desc->bInterfaceProtocol == 0x02);
                
                if (is_boot_device) {
                    // SET_IDLE for boot protocol devices
                    usb_transfer_t *ctrl_transfer;
                    err = usb_host_transfer_alloc(8, 0, &ctrl_transfer);
                    if (err == ESP_OK) {
                        usb_setup_packet_t idle_pkt = {
                            .bmRequestType = 0x21,
                            .bRequest = 0x0A,
                            .wValue = 0x0000,
                            .wIndex = intf_desc->bInterfaceNumber,
                            .wLength = 0
                        };
                        
                        ctrl_transfer->device_handle = dev_hdl;
                        ctrl_transfer->callback = ctrl_transfer_cb;
                        ctrl_transfer->context = NULL;
                        memcpy(ctrl_transfer->data_buffer, &idle_pkt, sizeof(usb_setup_packet_t));
                        ctrl_transfer->num_bytes = sizeof(usb_setup_packet_t);
                        
                        err = usb_host_transfer_submit_control(client_hdl, ctrl_transfer);
                        if (err != ESP_OK) {
                            usb_host_transfer_free(ctrl_transfer);
                        }
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    
                    // SET_PROTOCOL to boot mode
                    err = usb_host_transfer_alloc(8, 0, &ctrl_transfer);
                    if (err == ESP_OK) {
                        usb_setup_packet_t setup_pkt = {
                            .bmRequestType = 0x21,
                            .bRequest = 0x0B,
                            .wValue = 0x0000,
                            .wIndex = intf_desc->bInterfaceNumber,
                            .wLength = 0
                        };
                        
                        ctrl_transfer->device_handle = dev_hdl;
                        ctrl_transfer->callback = ctrl_transfer_cb;
                        ctrl_transfer->context = NULL;
                        memcpy(ctrl_transfer->data_buffer, &setup_pkt, sizeof(usb_setup_packet_t));
                        ctrl_transfer->num_bytes = sizeof(usb_setup_packet_t);
                        
                        err = usb_host_transfer_submit_control(client_hdl, ctrl_transfer);
                        if (err != ESP_OK) {
                            usb_host_transfer_free(ctrl_transfer);
                        }
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                }
                
                // Allocate transfer for the found endpoint
                usb_transfer_t *transfer;
                err = usb_host_transfer_alloc(ep_desc->wMaxPacketSize, 0, &transfer);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to allocate transfer: %s", esp_err_to_name(err));
                    return;
                }
                
                // Configure transfer based on device type
                transfer->device_handle = dev_hdl;
                transfer->bEndpointAddress = ep_desc->bEndpointAddress;
                if (intf_desc->bInterfaceProtocol == 0x02) {
                    transfer->callback = mouse_transfer_cb;
                } else if (intf_desc->bInterfaceProtocol == 0x01) {
                    transfer->callback = keyboard_transfer_cb;
                } else {
                    transfer->callback = gamepad_transfer_cb;
                }
                transfer->context = NULL;
                transfer->num_bytes = ep_desc->wMaxPacketSize;
                
                // Submit initial transfer
                err = usb_host_transfer_submit(transfer);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to submit transfer: %s", esp_err_to_name(err));
                    usb_host_transfer_free(transfer);
                } else {
                    active_transfers[0] = transfer;
                    if (intf_desc->bInterfaceProtocol == 0x02) {
                        ESP_LOGI(TAG, "Mouse monitoring started on endpoint 0x%02X", ep_desc->bEndpointAddress);
                    } else if (intf_desc->bInterfaceProtocol == 0x01) {
                        ESP_LOGI(TAG, "Keyboard monitoring started on endpoint 0x%02X", ep_desc->bEndpointAddress);
                        
                        // Initialize keyboard LED state (don't send command yet - let device settle)
                        id(caps_lock_state) = false;
                        id(num_lock_state) = false;
                        id(scroll_lock_state) = false;
                        ESP_LOGI(TAG, "Keyboard LED state initialized to OFF");
                    } else {
                        ESP_LOGI(TAG, "Gamepad monitoring started on endpoint 0x%02X", ep_desc->bEndpointAddress);
                        
                        // Check if official Switch controller (057E:2009)
                        const usb_device_desc_t *dev_desc;
                        if (usb_host_get_device_descriptor(dev_hdl, &dev_desc) == ESP_OK) {
                            if (dev_desc->idVendor == 0x057E && dev_desc->idProduct == 0x2009) {
                                is_official_switch = true;
                                vTaskDelay(pdMS_TO_TICKS(50));
                                init_switch_controller();
                            }
                        }
                    }
                    
                    // Try to set up media keys/touchpad interface (interface 1) if it exists
                    vTaskDelay(pdMS_TO_TICKS(50));
                    setup_media_interface();
                }
            }
            break;
        }
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "USB device disconnected - cleaning up");
            if (dev_hdl) {
                // Cancel and free active transfers first
                for (int i = 0; i < 3; i++) {
                    if (active_transfers[i]) {
                        usb_host_endpoint_halt(dev_hdl, active_transfers[i]->bEndpointAddress);
                        usb_host_endpoint_flush(dev_hdl, active_transfers[i]->bEndpointAddress);
                        vTaskDelay(pdMS_TO_TICKS(10));
                        active_transfers[i] = nullptr;
                    }
                }
                
                // Release all interfaces
                usb_host_interface_release(client_hdl, dev_hdl, 0);
                usb_host_interface_release(client_hdl, dev_hdl, 1);
                usb_host_interface_release(client_hdl, dev_hdl, 2);
                
                // Close device
                usb_host_device_close(client_hdl, dev_hdl);
                dev_hdl = NULL;
                is_official_switch = false;
                
                ESP_LOGI(TAG, "Device cleanup complete - ready for new device");
            }
            break;
        default:
            break;
    }
}

// USB host library task
void usb_host_lib_task(void *arg) {
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "All devices freed");
        }
    }
}

// Initialize USB keyboard capture
void setup_usb_keyboard() {
    ESP_LOGI(TAG, "=== SETUP_USB_KEYBOARD CALLED ===");
    ESP_LOGI(TAG, "Using existing USB host, registering keyboard client");
    
    // USB host is already installed by ESPHome, just register our client
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        }
    };
    
    esp_err_t err = usb_host_client_register(&client_config, &client_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Client register failed: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "USB HID keyboard client registered successfully");
}

// LED control transfer callback
void led_control_callback(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGI(TAG, "LED command completed successfully");
    } else {
        ESP_LOGW(TAG, "LED command failed with status: %d", transfer->status);
    }
    usb_host_transfer_free(transfer);
}

// Send LED status to keyboard
void update_keyboard_leds() {
    if (!dev_hdl || !client_hdl) {
        ESP_LOGW(TAG, "Cannot update LEDs - device or client not available");
        return;
    }
    
    // Create LED report: bit 0=Num Lock, bit 1=Caps Lock, bit 2=Scroll Lock
    uint8_t led_report = 0;
    if (id(num_lock_state)) led_report |= 0x01;
    if (id(caps_lock_state)) led_report |= 0x02;
    if (id(scroll_lock_state)) led_report |= 0x04;
    
    ESP_LOGI(TAG, "Updating keyboard LEDs: 0x%02X (Caps:%s Num:%s Scroll:%s)", 
             led_report, 
             id(caps_lock_state) ? "ON" : "OFF",
             id(num_lock_state) ? "ON" : "OFF",
             id(scroll_lock_state) ? "ON" : "OFF");
    
    usb_transfer_t *ctrl_transfer;
    esp_err_t err = usb_host_transfer_alloc(16, 0, &ctrl_transfer);
    if (err == ESP_OK) {
        usb_setup_packet_t setup_pkt = {
            .bmRequestType = 0x21, // Host-to-device, Class, Interface
            .bRequest = 0x09,      // SET_REPORT
            .wValue = 0x0200,      // Output report, Report ID 0
            .wIndex = 0,           // Interface 0
            .wLength = 1
        };
        
        ctrl_transfer->device_handle = dev_hdl;
        ctrl_transfer->callback = led_control_callback;
        ctrl_transfer->context = NULL;
        memcpy(ctrl_transfer->data_buffer, &setup_pkt, sizeof(usb_setup_packet_t));
        ctrl_transfer->data_buffer[sizeof(usb_setup_packet_t)] = led_report;
        ctrl_transfer->num_bytes = sizeof(usb_setup_packet_t) + 1;
        
        err = usb_host_transfer_submit_control(client_hdl, ctrl_transfer);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "LED command submitted successfully");
            // Don't free here - callback will handle it
        } else {
            ESP_LOGW(TAG, "LED command failed: %s", esp_err_to_name(err));
            usb_host_transfer_free(ctrl_transfer);
        }
    } else {
        ESP_LOGE(TAG, "Failed to allocate transfer for LED update: %s", esp_err_to_name(err));
    }
}



// Setup media keys interface (0x82)
void setup_media_interface() {
    if (!dev_hdl || !client_hdl) return;
    
    // Check if interface 1 exists
    const usb_config_desc_t *config_desc;
    if (usb_host_get_active_config_descriptor(dev_hdl, &config_desc) != ESP_OK) return;
    
    bool has_interface_1 = false;
    int offset = 0;
    while (offset < config_desc->wTotalLength) {
        const usb_standard_desc_t *desc = (const usb_standard_desc_t *)((uint8_t *)config_desc + offset);
        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)desc;
            if (intf->bInterfaceNumber == 1) {
                has_interface_1 = true;
                break;
            }
        }
        offset += desc->bLength;
    }
    
    if (!has_interface_1) {
        ESP_LOGI(TAG, "Interface 1 not found, skipping media setup");
        return;
    }
    
    ESP_LOGI(TAG, "Interface 1 found, attempting to claim...");
    esp_err_t err = usb_host_interface_claim(client_hdl, dev_hdl, 1, 0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Interface 1 claimed successfully");
        usb_transfer_t *transfer;
        err = usb_host_transfer_alloc(64, 0, &transfer);
        if (err == ESP_OK) {
            transfer->device_handle = dev_hdl;
            transfer->bEndpointAddress = 0x82;
            transfer->callback = media_transfer_cb;
            transfer->num_bytes = 8;
            
            err = usb_host_transfer_submit(transfer);
            if (err == ESP_OK) {
                active_transfers[1] = transfer;
                ESP_LOGI(TAG, "Media keys monitoring on 0x82");
            } else {
                ESP_LOGE(TAG, "Failed to submit transfer on 0x82: %s", esp_err_to_name(err));
                usb_host_transfer_free(transfer);
            }
        } else {
            ESP_LOGE(TAG, "Failed to allocate transfer for 0x82: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Failed to claim interface 1: %s", esp_err_to_name(err));
    }
}

// Control transfer callback
void ctrl_transfer_cb(usb_transfer_t *transfer) {
    usb_host_transfer_free(transfer);
}

// Send output report to Switch controller
void send_switch_command(uint8_t cmd, const uint8_t* data, uint8_t len) {
    if (!dev_hdl || !client_hdl) return;
    
    usb_transfer_t *ctrl_transfer;
    if (usb_host_transfer_alloc(128, 0, &ctrl_transfer) == ESP_OK) {
        uint8_t report[64] = {0};
        report[0] = 0x01;  // Output report ID
        report[1] = switch_packet_counter++;
        memcpy(&report[2], rumble_data, 8);  // Rumble data
        report[10] = cmd;
        if (data && len > 0) {
            memcpy(&report[11], data, len);
        }
        
        usb_setup_packet_t setup_pkt = {
            .bmRequestType = 0x21,
            .bRequest = 0x09,
            .wValue = 0x0301,
            .wIndex = 0,
            .wLength = 64
        };
        
        ctrl_transfer->device_handle = dev_hdl;
        ctrl_transfer->callback = ctrl_transfer_cb;
        ctrl_transfer->context = NULL;
        memcpy(ctrl_transfer->data_buffer, &setup_pkt, sizeof(usb_setup_packet_t));
        memcpy(ctrl_transfer->data_buffer + sizeof(usb_setup_packet_t), report, 64);
        ctrl_transfer->num_bytes = sizeof(usb_setup_packet_t) + 64;
        
        if (usb_host_transfer_submit_control(client_hdl, ctrl_transfer) != ESP_OK) {
            usb_host_transfer_free(ctrl_transfer);
        }
    }
}

// Initialize official Switch Pro Controller
void init_switch_controller() {
    if (!dev_hdl || !client_hdl) return;
    
    ESP_LOGI(TAG, "Initializing official Switch Pro Controller");
    
    // Handshake
    usb_transfer_t *ctrl_transfer;
    if (usb_host_transfer_alloc(32, 0, &ctrl_transfer) == ESP_OK) {
        uint8_t handshake[] = {0x80, 0x02};
        usb_setup_packet_t setup_pkt = {
            .bmRequestType = 0x21,
            .bRequest = 0x09,
            .wValue = 0x0380,
            .wIndex = 0,
            .wLength = 2
        };
        
        ctrl_transfer->device_handle = dev_hdl;
        ctrl_transfer->callback = ctrl_transfer_cb;
        ctrl_transfer->context = NULL;
        memcpy(ctrl_transfer->data_buffer, &setup_pkt, sizeof(usb_setup_packet_t));
        memcpy(ctrl_transfer->data_buffer + sizeof(usb_setup_packet_t), handshake, 2);
        ctrl_transfer->num_bytes = sizeof(usb_setup_packet_t) + 2;
        
        if (usb_host_transfer_submit_control(client_hdl, ctrl_transfer) != ESP_OK) {
            usb_host_transfer_free(ctrl_transfer);
            return;
        }
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Set input report mode to 0x30 (standard full mode)
    uint8_t mode_data[] = {0x30};
    send_switch_command(0x03, mode_data, 1);
    
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Enable IMU (optional, but part of init)
    uint8_t imu_data[] = {0x01};
    send_switch_command(0x40, imu_data, 1);
    
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Set player LEDs to player 1
    uint8_t led_data[] = {0x01};
    send_switch_command(0x30, led_data, 1);
    
    ESP_LOGI(TAG, "Switch controller initialization complete");
}

// Set Switch Pro Controller player LEDs (player 1)
void set_switch_player_leds() {
    if (!dev_hdl || !client_hdl) return;
    
    ESP_LOGI(TAG, "Setting Switch controller to Player 1");
    usb_transfer_t *ctrl_transfer;
    esp_err_t err = usb_host_transfer_alloc(16, 0, &ctrl_transfer);
    if (err == ESP_OK) {
        usb_setup_packet_t setup_pkt = {
            .bmRequestType = 0x21,
            .bRequest = 0x09,
            .wValue = 0x0301,
            .wIndex = 0,
            .wLength = 1
        };
        
        ctrl_transfer->device_handle = dev_hdl;
        ctrl_transfer->callback = ctrl_transfer_cb;
        ctrl_transfer->context = NULL;
        memcpy(ctrl_transfer->data_buffer, &setup_pkt, sizeof(usb_setup_packet_t));
        ctrl_transfer->data_buffer[sizeof(usb_setup_packet_t)] = 0x01;
        ctrl_transfer->num_bytes = sizeof(usb_setup_packet_t) + 1;
        
        err = usb_host_transfer_submit_control(client_hdl, ctrl_transfer);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Player LED set to 1");
        } else {
            ESP_LOGW(TAG, "Failed to set player LED: %s", esp_err_to_name(err));
            usb_host_transfer_free(ctrl_transfer);
        }
    }
}

// Setup touchpad interface - find actual endpoint
void setup_mouse_interface() {
    if (!dev_hdl || !client_hdl) return;
    
    const usb_config_desc_t *config_desc;
    if (usb_host_get_active_config_descriptor(dev_hdl, &config_desc) != ESP_OK) return;
    
    // Find interface 2 and its endpoint
    const usb_intf_desc_t *intf_desc = nullptr;
    const usb_ep_desc_t *ep_desc = nullptr;
    int offset = 0;
    
    while (offset < config_desc->wTotalLength) {
        const usb_standard_desc_t *desc = (const usb_standard_desc_t *)((uint8_t *)config_desc + offset);
        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *temp_intf = (const usb_intf_desc_t *)desc;
            if (temp_intf->bInterfaceNumber == 2) {
                intf_desc = temp_intf;
                ESP_LOGI(TAG, "Interface 2 found: Class=0x%02X, SubClass=0x%02X, Protocol=0x%02X",
                        intf_desc->bInterfaceClass, intf_desc->bInterfaceSubClass, intf_desc->bInterfaceProtocol);
            }
        } else if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && intf_desc) {
            ep_desc = (const usb_ep_desc_t *)desc;
            if ((ep_desc->bEndpointAddress & 0x80) && ((ep_desc->bmAttributes & 0x03) == 0x03)) {
                ESP_LOGI(TAG, "Found endpoint for interface 2: 0x%02X", ep_desc->bEndpointAddress);
                break;
            }
        }
        offset += desc->bLength;
    }
    
    if (!intf_desc || !ep_desc) {
        ESP_LOGI(TAG, "Interface 2 or endpoint not found");
        return;
    }
    
    esp_err_t err = usb_host_interface_claim(client_hdl, dev_hdl, 2, 0);
    if (err == ESP_OK) {
        usb_transfer_t *transfer;
        err = usb_host_transfer_alloc(64, 0, &transfer);
        if (err == ESP_OK) {
            transfer->device_handle = dev_hdl;
            transfer->bEndpointAddress = ep_desc->bEndpointAddress;
            transfer->callback = (ep_desc->bEndpointAddress == 0x82) ? media_transfer_cb : touchpad_transfer_cb;
            transfer->num_bytes = ep_desc->wMaxPacketSize;
            
            err = usb_host_transfer_submit(transfer);
            if (err == ESP_OK) {
                active_transfers[2] = transfer;
                ESP_LOGI(TAG, "Touchpad monitoring on 0x%02X", ep_desc->bEndpointAddress);
            } else {
                ESP_LOGE(TAG, "Failed to submit transfer: %s", esp_err_to_name(err));
                usb_host_transfer_free(transfer);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to claim interface 2: %s", esp_err_to_name(err));
    }
}

// Fast USB event processing
void process_usb_events() {
    if (client_hdl) {
        usb_host_client_handle_events(client_hdl, 0);
    }
    poll_switch_controller();
}