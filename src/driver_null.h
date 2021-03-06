#ifndef __DRIVER_NULL_H__
#define __DRIVER_NULL_H__

#include <cstdint>
#include "driver.h"

namespace ZZT {
    class NullDriver: public Driver {
    private:
        uint16_t hsecs;
        
    public:
        NullDriver();

        void install(void);
        void uninstall(void);

        // required (input)
        void update_input(void) override;

        // required (sound)
        uint16_t get_hsecs(void) override;
        void delay(int ms) override;
        void idle(IdleMode mode) override;
        void sound_stop(void) override;

        // required (video)
        void draw_char(int16_t x, int16_t y, uint8_t col, uint8_t chr) override;
        void read_char(int16_t x, int16_t y, uint8_t &col, uint8_t &chr) override;
    };
}

#endif