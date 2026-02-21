#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <random>
#include <unordered_map>
#include <chrono>
#include <algorithm>

using namespace geode::prelude;

namespace fpsg::settings
{
    int minFps();
    int maxFps();
    float speedMul();
    bool hideBg();
    float resultDelay();
    bool sfxEnabled();
    int cooldownMs();
}

namespace fpsg
{

    static int s_randInt(int a, int b) // beautiful random number generator
    {
        static std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist(a, b);
        return dist(rng);
    }

    struct State
    {
        bool isready = false;
        bool isrotating = false;
        bool queued = false;
        std::chrono::steady_clock::time_point cooldownUntil =
            std::chrono::steady_clock::time_point::min();
    };

    static std::unordered_map<PlayLayer *, State>& states()
    {
        static std::unordered_map<PlayLayer *, State> s;
        return s;
    }

    void clearFor(PlayLayer* pl) { states().erase(pl); }

    void disarmForReset(PlayLayer* pl)
    {
        auto& st = states()[pl];
        st.isready = false;
        st.queued = false;
    }

    static void armNow(PlayLayer* pl)
    {
        auto& st = states()[pl];
        st.isready = true;
        st.queued = false;
    }

    void armAfterResetSoon(PlayLayer* pl, float delaySeconds)
    {
        if (!pl)
            return;
        pl->runAction(CCSequence::create(
            CCDelayTime::create(delaySeconds),
            CallFuncExt::create([pl]
                                { if (pl) armNow(pl); }),
            nullptr));
    }

    void armAfterEnterSoon(PlayLayer* pl, float delaySeconds)
    {
        if (!pl)
            return;
        pl->runAction(CCSequence::create(
            CCDelayTime::create(delaySeconds),
            CallFuncExt::create([pl]
                                { if (pl) armNow(pl); }),
            nullptr));
    }

    class RouletteOverlay final : public CCLayerColor
    {
    public:
        static RouletteOverlay* create(PlayLayer* pl, int minV, int maxV, float speedMul, bool hideBg)
        {
            auto ret = new RouletteOverlay();
            if (ret && ret->init(pl, minV, maxV, speedMul, hideBg))
            {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool ccTouchBegan(CCTouch *, CCEvent *) override { return true; }

    private:
        PlayLayer *m_pl = nullptr;
        CCNode* m_strip = nullptr;
        CCLabelBMFont* m_resultfps = nullptr;
        int m_pick = 60;
        float m_endX = 0.f;

        bool init(PlayLayer* pl, int minV, int maxV, float speedMul, bool hideBg)
        {
            m_pl = pl;

            minV = std::max(1, std::min(minV, 360));
            maxV = std::max(1, std::min(maxV, 360));
            if (minV > maxV)
                std::swap(minV, maxV);
            speedMul = std::max(0.25f, std::min(speedMul, 4.0f));

            if (!CCLayerColor::initWithColor(hideBg ? ccColor4B{0, 0, 0, 0} : ccColor4B{0, 0, 0, 140}))
                return false;

            this->setTouchEnabled(true);
            this->setTouchMode(kCCTouchesOneByOne);

            auto win = CCDirector::sharedDirector()->getWinSize();
            auto center = CCPoint{win.width / 2.f, win.height / 2.f};

            if (!hideBg)
            {
                auto panel = CCScale9Sprite::create("GJ_square01.png");
                panel->setContentSize({420.f, 100.f});
                panel->setPosition(center);
                panel->setOpacity(235);
                this->addChild(panel, 1);

                auto frame = CCScale9Sprite::create("GJ_square02.png");
                frame->setContentSize({368.f, 64.f});
                frame->setPosition(center);
                frame->setOpacity(170);
                this->addChild(frame, 6);
            }

            auto pointer = CCLayerColor::create({255, 255, 255, 255}, 2.f, 64.f);
            pointer->setPosition({center.x - 1.f, center.y - 32.f});
            this->addChild(pointer, 7);

            auto clip = CCClippingNode::create();
            clip->setPosition(center);
            this->addChild(clip, 5);

            auto stencil = CCNode::create();
            auto rect = CCLayerColor::create({255, 255, 255, 255}, 360.f, 54.f);
            rect->setPosition({-180.f, -27.f});
            stencil->addChild(rect);
            clip->setStencil(stencil);

            m_strip = CCNode::create();
            clip->addChild(m_strip, 10);

            const int count = (maxV - minV + 1);
            const int repeats = 12;
            const float step = 36.f;

            m_pick = s_randInt(minV, maxV);

            const int targetIndexInCycle = (m_pick - minV);
            const int targetIndex = (repeats - 2) * count + targetIndexInCycle;

            for (int r = 0; r < repeats; r++)
            {
                for (int i = 0; i < count; i++)
                {
                    int v = minV + i;
                    auto lbl = CCLabelBMFont::create(fmt::format("{}", v).c_str(), "bigFont.fnt");
                    lbl->setScale(0.52f);
                    lbl->setOpacity(235);
                    lbl->setPosition({(r * count + i) * step, 0.f});
                    m_strip->addChild(lbl);
                }
            }

            m_endX = -targetIndex * step;
            const float startX = m_endX + (count * step * 7.0f);
            m_strip->setPosition({startX, 0.f});

            const float mid1X = m_endX + (count * step * 2.4f);
            const float mid2X = m_endX + (count * step * 0.6f);

            // to-do: update sfx system
            // auto p = (Mod::get()->getResourcesDir() / "sfx/chestClick.ogg").string();
            // FMODAudioEngine::sharedEngine()->playEffect(p.c_str());

            auto fast = CCMoveTo::create(0.18f / speedMul, {mid1X, 0.f});
            auto mid = CCEaseSineOut::create(CCMoveTo::create(0.75f / speedMul, {mid2X, 0.f}));
            auto slow = CCEaseExponentialOut::create(CCMoveTo::create(1.95f / speedMul, {m_endX, 0.f}));

            auto overshoot = CCMoveBy::create(0.06f, {-12.f, 0.f});
            auto bounceBack = CCEaseBackOut::create(CCMoveBy::create(0.14f, {+12.f, 0.f}));

            m_strip->runAction(CCSequence::create(
                fast, mid, slow,
                overshoot, bounceBack,
                CallFuncExt::create([this]
                                    { this->finish(); }),
                nullptr));

            return true;
        }

        void finish()
        {
            // to-do: update sfx system
            // auto p = (Mod::get()->getResourcesDir() / "sfx/reward01.ogg").string();
            // FMODAudioEngine::sharedEngine()->playEffect(p.c_str());

            auto w = CCDirector::sharedDirector()->getWinSize();
            auto c = CCPoint{w.width / 2.f, w.height / 2.f};

            m_resultfps = CCLabelBMFont::create(fmt::format("{} FPS", m_pick).c_str(), "goldFont.fnt");
            m_resultfps->setScale(1.3f);
            m_resultfps->setPosition({c.x, c.y + 100.f});
            this->addChild(m_resultfps, 50);

            const float delay = settings::resultDelay();
            const int fps = std::max(1, std::min(m_pick, 360));

            this->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CallFuncExt::create([this, fps]
                                    {
                CCDirector::sharedDirector()->setAnimationInterval(1.0 / (double)fps);

                if (m_pl) {
                    m_pl->resumeSchedulerAndActions();
                    auto& st = states()[m_pl];
                    st.isrotating = false;
                    st.queued = false;
                    st.cooldownUntil =
                        std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(settings::cooldownMs());
                }

                this->removeFromParentAndCleanup(true); }),
                nullptr));
        }
    };

    static void startRoulette(PlayLayer* pl)
    {
        auto& st = states()[pl];
        st.queued = false;
        st.isrotating = true;
        st.isready = false;

        int minV = settings::minFps();
        int maxV = settings::maxFps();
        float speed = settings::speedMul();
        bool hideBg = settings::hideBg();

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        scene->addChild(RouletteOverlay::create(pl, minV, maxV, speed, hideBg), 2);

        pl->pauseSchedulerAndActions();
    }

    void tryStartOnDeath(PlayLayer* pl)
    {
        if (!pl)
            return;

        auto& st = states()[pl];
        const auto now = std::chrono::steady_clock::now();

        if (!st.isready)
            return;
        if (st.isrotating || st.queued)
            return;
        if (now < st.cooldownUntil)
            return;

        st.queued = true;

        pl->runAction(CCSequence::create(
            CCDelayTime::create(0.f),
            CallFuncExt::create([pl]
                                {
            if (!pl) return;
            auto& st2 = states()[pl];
            if (!st2.isrotating) startRoulette(pl); }),
            nullptr));
    }

}
