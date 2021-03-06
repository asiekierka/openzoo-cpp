#ifndef __DRIVER_PSP_H__
#define __DRIVER_PSP_H__

#include <cstdint>
#include <pspkerneltypes.h>
#include <pspctrl.h>
#include "driver.h"
#include "audio_simulator.h"
#include "user_interface_osk.h"

namespace ZZT {
    class PSPDriver: public Driver {
        friend SceUInt psp_timer_callback(SceUID uid, SceInt64 requested, SceInt64 actual, void *args);
        friend void psp_audio_callback(void *stream, unsigned int len, void *userdata);
        
    private:
        SceInt64 pit_clock;
        SceUID pit_timer_id;
        SceUID audio_mutex;
        int exit_thread_id;
        uint16_t hsecs;

        uint8_t screen_chars[80*25];
        uint8_t screen_colors[80*25];
        
        AudioSimulator<uint8_t> *soundSimulator;
        OnScreenKeyboard keyboard;

    public:
        bool running;

        PSPDriver();
        ~PSPDriver();

        void draw_frame(void);
        void update_joy(SceCtrlData &pad);

        void install(void);
        void uninstall(void);

		UserInterface *create_user_interface(Game &game, bool is_editor);

        // required (input)
        void update_input(void) override;
        void set_text_input(bool enabled, InputPromptMode mode) override;

        // required (sound)
        uint16_t get_hsecs(void) override;
        void delay(int ms) override;
        void idle(IdleMode mode) override;
        void sound_stop(void) override;
        void sound_lock(void) override;
        void sound_unlock(void) override;

        // required (video)
        void draw_char(int16_t x, int16_t y, uint8_t col, uint8_t chr) override;
        void read_char(int16_t x, int16_t y, uint8_t &col, uint8_t &chr) override;
    };
}

#endif
