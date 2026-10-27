#include "eaglenest.h"

static SDL_Gamepad* g_gamepad = NULL;
static SDL_JoystickID g_gamepad_id = 0;
static bool g_gamepad_start_prev = false;

#define STICK_DEADZONE 8000  // left-stick dead zone (SDL axes span +-32767)

// Open the first controller SDL recognises as a gamepad, unless one is already
// open. Gamepads get a consistent button/axis mapping, unlike raw joysticks.
static void open_first_available_gamepad(void) {
    if (g_gamepad) return;

    int num_joysticks = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&num_joysticks);
    if (!joysticks) return;

    for (int i = 0; i < num_joysticks; i++) {
        if (SDL_IsGamepad(joysticks[i])) {
            g_gamepad = SDL_OpenGamepad(joysticks[i]);
            if (g_gamepad) {
                g_gamepad_id = joysticks[i];
                break;
            }
        }
    }
    SDL_free(joysticks);
}

void input_init(void) {
    memset(&g_input, 0, sizeof(Input));
    g_gamepad = NULL;
    g_gamepad_start_prev = false;
    open_first_available_gamepad();
}

void input_shutdown(void) {
    if (g_gamepad) {
        SDL_CloseGamepad(g_gamepad);
        g_gamepad = NULL;
    }
}

// Hot-plug. SDL closes a disconnected gamepad itself, so on removal we only
// drop our (now dangling) handle and look for another one.
void input_gamepad_added(void) {
    open_first_available_gamepad();
}

void input_gamepad_removed(u32 instance_id) {
    if (g_gamepad && instance_id == g_gamepad_id) {
        g_gamepad = NULL;
        g_gamepad_start_prev = false;
    }
    open_first_available_gamepad();
}

void input_poll(void) {
    g_input.joy_val = 0;

    SDL_PumpEvents();

    const bool* keys = SDL_GetKeyboardState(NULL);

    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) g_input.joy_val |= JOY_UP;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) g_input.joy_val |= JOY_DOWN;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) g_input.joy_val |= JOY_LEFT;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) g_input.joy_val |= JOY_RIGHT;
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) g_input.joy_val |= JOY_FIRE;

    if (g_gamepad) {
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) g_input.joy_val |= JOY_UP;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) g_input.joy_val |= JOY_DOWN;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) g_input.joy_val |= JOY_LEFT;
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) g_input.joy_val |= JOY_RIGHT;

        // Left analog stick drives the same 4-way directions past a dead zone.
        int lx = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        int ly = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        if (lx <= -STICK_DEADZONE) g_input.joy_val |= JOY_LEFT;
        if (lx >= STICK_DEADZONE) g_input.joy_val |= JOY_RIGHT;
        if (ly <= -STICK_DEADZONE) g_input.joy_val |= JOY_UP;
        if (ly >= STICK_DEADZONE) g_input.joy_val |= JOY_DOWN;

        // Face buttons, shoulders and triggers all fire.
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_SOUTH) ||
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_WEST) ||
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_EAST) ||
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_NORTH) ||
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ||
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ||
            SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000 ||
            SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000) {
            g_input.joy_val |= JOY_FIRE;
        }

        // START pauses, on the rising edge only.
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_START)) {
            if (!g_gamepad_start_prev) g_input.pause_requested = true;
            g_gamepad_start_prev = true;
        } else {
            g_gamepad_start_prev = false;
        }
    }
}

bool input_is_pressed(u8 mask) {
    return (g_input.joy_val & mask) != 0;
}

u8 input_get_joyval(void) {
    return g_input.joy_val;
}
