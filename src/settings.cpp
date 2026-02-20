#include <Geode/Geode.hpp>
#include <algorithm>

// i tramslated all comments using 'Translate Comments' extension.

using namespace geode::prelude;

namespace fpsg::settings
{

    static int clampInt(int v, int lo, int hi)
    {
        return std::max(lo, std::min(v, hi));
    }

    int minFps()
    {
        int v = 1;
        if (auto mod = Mod::get())
            v = (int)mod->getSettingValue<int64_t>("roulette-min");
        return clampInt(v, 1, 360);
    }

    int maxFps()
    {
        int v = 360;
        if (auto mod = Mod::get())
            v = (int)mod->getSettingValue<int64_t>("roulette-max");
        return clampInt(v, 1, 360);
    }

    float speedMul()
    {
        double v = 1.0;
        if (auto mod = Mod::get())
            v = mod->getSettingValue<double>("roulette-speed");

        float f = (float)v;
        return std::max(0.25f, std::min(f, 4.0f));
    }

    bool hideBg()
    {
        bool v = false;
        if (auto mod = Mod::get())
            v = mod->getSettingValue<bool>("roulette-hide-bg");
        return v;
    }

    //  Wait (seconds) after roulette stops
    float resultDelay()
    {
        double v = 0.8;
        if (auto mod = Mod::get())
            v = mod->getSettingValue<double>("roulette-delay");

        float f = (float)v;
        return std::max(0.f, std::min(f, 10.f)); // 0~10초 제한
    }

    // Sound effect on/off
    bool sfxEnabled()
    {
        bool v = true;
        if (auto mod = Mod::get())
            v = mod->getSettingValue<bool>("roulette-sfx");
        return v;
    }

    int cooldownMs()
    {
        return 1200;
    }
}


// Clang Format