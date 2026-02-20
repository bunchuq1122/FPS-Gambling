#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace fpsg {
    void clearFor(PlayLayer* pl);
    void disarmForReset(PlayLayer* pl);
    void armAfterResetSoon(PlayLayer* pl, float delaySeconds);
    void armAfterEnterSoon(PlayLayer* pl, float delaySeconds);
    void tryStartOnDeath(PlayLayer* pl);
}

class $modify(FPSGamblePlayLayer, PlayLayer) {
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        fpsg::armAfterEnterSoon(this, 0.6f);
    }

    void onExit() {
        fpsg::clearFor(this);
        PlayLayer::onExit();
    }

    void resetLevel() {
        fpsg::disarmForReset(this);
        PlayLayer::resetLevel();
        fpsg::armAfterResetSoon(this, 0.2f);
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        PlayLayer::destroyPlayer(player, obj);
        fpsg::tryStartOnDeath(this);
    }
};
