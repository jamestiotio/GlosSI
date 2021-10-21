/*
Copyright 2021 Peter Repukat - FlatspotSoftware

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "InputRedirector.h"

#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>
#include <spdlog/spdlog.h>

InputRedirector::InputRedirector()
{
#ifdef _WIN32
    driver_ = vigem_alloc();
    vigem_connected_ = VIGEM_SUCCESS(vigem_connect(driver_));
    if (vigem_connected_) {
        spdlog::debug("Connected to ViGEm");
    }
    else {
        spdlog::error("Error initializing ViGEm");
        // TODO: setup some mechanic to draw to window...
    }
#endif
}

InputRedirector::~InputRedirector()
{
#ifdef _WIN32
    if (controller_thread_.joinable())
        controller_thread_.join();
    vigem_disconnect(driver_);
    vigem_free(driver_);
#endif
}

void InputRedirector::run()
{
    run_ = vigem_connected_;
    controller_thread_ = std::thread(&InputRedirector::runLoop, this);
}

void InputRedirector::stop()
{
    run_ = false;
    if (vigem_connected_) {
        for (const auto& target : vt_x360_) {
            vigem_target_remove(driver_, target);
        }
    }
}

void InputRedirector::runLoop()
{
    // wait for steam to do all of it's hooking
    const sf::Clock clock;
    while (clock.getElapsedTime().asMilliseconds() < start_delay_ms_) {
        sf::sleep(sf::milliseconds(20));
    }
    while (run_) {
#ifdef _WIN32

        for (int i = 0; i < XUSER_MAX_COUNT; i++) {
            XINPUT_STATE state{};
            if (XInputGetState(i, &state) == ERROR_SUCCESS) {
                if (vt_x360_[i] != nullptr) {
                    vigem_target_x360_update(driver_, vt_x360_[i], *reinterpret_cast<XUSB_REPORT*>(&state.Gamepad));
                }
                else {
                    vt_x360_[i] = vigem_target_x360_alloc();
                    // By using VID and PID of Valve's Emulated Controller
                    // ( https://partner.steamgames.com/doc/features/steam_controller/steam_input_gamepad_emulation_bestpractices )
                    // Steam doesn't give us ANOTHER "fake" XInput device
                    // -> Leading to endless pain and suffering.
                    // Or really, leading to plugging in one virtual controller after another and mirroring inputs
                    // Also annoying the shit out of the user when they open the overlay as steam prompts to setup new XInput devices
                    // Also avoiding any fake inputs from Valve's default controller profile
                    // -> Leading to endless pain and suffering
                    //
                    // Additonaly, Steam does pick up the emulated controller this way, and Hides it from our application
                    // but Steam ONLY does this if it is configured to support X360 controller rebinding!!!
                    // Otherwise, this application (GloSC/GlosSI) will pickup the emulated controller as well!
                    vigem_target_set_vid(vt_x360_[i], 0x28de); //VALVE_DIRECTINPUT_GAMEPAD_VID
                    vigem_target_set_pid(vt_x360_[i], 0x11FF); //VALVE_DIRECTINPUT_GAMEPAD_PID
                    // TODO: MAYBE!: In a future version, use something like OpenXInput
                    //and filter out emulated controllers to support a greater amount of controllers simultaneously

                    const int target_add_res = vigem_target_add(driver_, vt_x360_[i]);
                    if (target_add_res == VIGEM_ERROR_TARGET_UNINITIALIZED) {
                        vt_x360_[i] = vigem_target_x360_alloc();
                    }
                    if (target_add_res == VIGEM_ERROR_NONE) {
                        spdlog::info("Plugged in controller {}, {}", i, vigem_target_get_index(vt_x360_[i]));
                        const auto callback_register_res = vigem_target_x360_register_notification(
                            driver_,
                            vt_x360_[i],
                            &InputRedirector::controllerCallback,
                            nullptr);
                        if (!VIGEM_SUCCESS(callback_register_res)) {
                            spdlog::error("Registering controller {}, {} for notification failed with error code: {:#x}", i, vigem_target_get_index(vt_x360_[i]), callback_register_res);
                        }
                    }
                }
            }
            else {
                if (vt_x360_[i] != nullptr) {
                    if (VIGEM_SUCCESS(vigem_target_remove(driver_, vt_x360_[i]))) {
                        spdlog::info("Unplugged controller {}, {}", i, vigem_target_get_index(vt_x360_[i]));
                        vt_x360_[i] = nullptr;
                    }
                }
            }
        }
        sf::sleep(sf::milliseconds(1));

#endif
    }
}

#ifdef _WIN32
void InputRedirector::controllerCallback(PVIGEM_CLIENT client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, UCHAR LedNumber, LPVOID UserData)
{
    XINPUT_VIBRATION vibration;
    ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
    vibration.wLeftMotorSpeed = LargeMotor * 0xff;  //Controllers only use 1 byte, XInput-API uses two, ViGEm also only uses one, like the hardware does, so we have to multiply
    vibration.wRightMotorSpeed = SmallMotor * 0xff; //Yeah yeah I do know about bitshifting and the multiplication not being 100% correct...

    XInputSetState(vigem_target_get_index(Target) - 1, &vibration);
}
#endif