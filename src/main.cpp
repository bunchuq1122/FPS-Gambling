#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/cocos.hpp> // CallFuncExt
#include <random>
#include <unordered_map>
#include <chrono>

using namespace geode::prelude;

namespace {
    // ---------- util ----------
    int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }

    int randInt(int a, int b) {
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<int> dist(a, b);
        return dist(rng);
    }

    // ---------- per-PlayLayer state ----------
    struct RState {
        bool active = false;     // roulette currently running (overlay on screen)
        bool queued = false;     // queued to start next tick
        bool readyForDeath = true; // becomes false once a death has triggered; re-enabled after reset
        std::chrono::steady_clock::time_point cooldownUntil =
            std::chrono::steady_clock::time_point::min();
    };

    std::unordered_map<PlayLayer*, RState>& states() {
        static std::unordered_map<PlayLayer*, RState> s;
        return s;
    }

    void clearState(PlayLayer* pl) { states().erase(pl); }

    bool canQueue(PlayLayer* pl) {
        auto& st = states()[pl];
        auto now = std::chrono::steady_clock::now();

        // No reruns while running/queued, and don't allow "extra" triggers during reset pipeline
        if (st.active || st.queued) return false;

        // IMPORTANT: prevent the "second roulette" that happens during the auto-restart,
        // by requiring that the layer has been marked ready (set after reset).
        if (!st.readyForDeath) return false;

        if (now < st.cooldownUntil) return false;

        st.queued = true;
        st.readyForDeath = false; // consume this death event immediately
        return true;
    }

    void markStarted(PlayLayer* pl) {
        auto& st = states()[pl];
        st.queued = false;
        st.active = true;
    }

    void markFinished(PlayLayer* pl) {
        auto& st = states()[pl];
        st.active = false;
        st.queued = false;
        // short cooldown to ignore duplicate destroyPlayer calls around the same event
        st.cooldownUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
        // Do NOT set readyForDeath here. We only re-enable after resetLevel.
    }

    void setReadyForNextDeath(PlayLayer* pl) {
        auto& st = states()[pl];
        st.readyForDeath = true;
    }

    // ---------- overlay ----------
    class RouletteOverlay final : public CCLayerColor {
    public:
        static RouletteOverlay* create(PlayLayer* pl, int minV, int maxV, float speedMul, bool hideBg) {
            auto ret = new RouletteOverlay();
            if (ret && ret->init(pl, minV, maxV, speedMul, hideBg)) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; } // swallow touches

    private:
        PlayLayer* m_pl = nullptr;
        CCNode* m_strip = nullptr;
        CCLabelBMFont* m_top = nullptr;
        int m_pick = 60;

        bool init(PlayLayer* pl, int minV, int maxV, float speedMul, bool hideBg) {
            m_pl = pl;

            // sanitize range
            minV = clampInt(minV, 1, 360);
            maxV = clampInt(maxV, 1, 360);
            if (minV > maxV) std::swap(minV, maxV);

            speedMul = std::max(0.25f, std::min(speedMul, 4.0f));

            // Optional background (dim + panel). If hideBg=true, keep overlay fully transparent.
            if (!CCLayerColor::initWithColor(hideBg ? ccColor4B{0,0,0,0} : ccColor4B{0,0,0,140})) return false;

            this->setTouchEnabled(true);
            this->setTouchMode(kCCTouchesOneByOne);

            auto win = CCDirector::sharedDirector()->getWinSize();

            // panel + title (optional)
            if (!hideBg) {
                auto panel = CCScale9Sprite::create("GJ_square01.png");
                panel->setContentSize({ 420.f, 130.f });
                panel->setPosition({ win.width / 2, win.height / 2 });
                panel->setOpacity(235);
                this->addChild(panel, 1);
            }

            m_top = CCLabelBMFont::create("Rolling...", "goldFont.fnt");
            m_top->setScale(0.55f);
            m_top->setPosition({ win.width / 2, win.height / 2 + (hideBg ? 70.f : 52.f) });
            this->addChild(m_top, 2);

// clip window
            auto clip = CCClippingNode::create();
            clip->setPosition({ win.width / 2, win.height / 2 });
            this->addChild(clip, 5);

            auto stencil = CCNode::create();
            auto rect = CCLayerColor::create({ 255, 255, 255, 255 }, 360.f, 54.f);
            rect->setPosition({ -180.f, -27.f });
            stencil->addChild(rect);
            clip->setStencil(stencil);

            if (!hideBg) {
                auto frame = CCScale9Sprite::create("GJ_square02.png");
                frame->setContentSize({ 368.f, 62.f });
                frame->setPosition({ win.width / 2, win.height / 2 });
                frame->setOpacity(170);
                this->addChild(frame, 6);
            }

            // center pointer line
            auto pointer = CCLayerColor::create({ 255, 255, 255, 255 }, 2.f, 64.f);
            pointer->setPosition({ win.width / 2 - 1.f, win.height / 2 - 32.f });
            this->addChild(pointer, 7);

            // build strip
            m_strip = CCNode::create();
            clip->addChild(m_strip, 10);

            int count = (maxV - minV + 1);
            // More repeats = longer spin feel
            int repeats = 10;
            float step = 36.f;

            m_pick = randInt(minV, maxV);
            int targetIndexInCycle = (m_pick - minV);
            int targetIndex = (repeats - 1) * count + targetIndexInCycle;

            for (int r = 0; r < repeats; r++) {
                for (int i = 0; i < count; i++) {
                    int v = minV + i;
                    auto lbl = CCLabelBMFont::create(fmt::format("{}", v).c_str(), "bigFont.fnt");
                    lbl->setScale(0.52f);
                    lbl->setOpacity(235);
                    lbl->setPosition({ (r * count + i) * step, 0.f });
                    m_strip->addChild(lbl);
                }
            }

            // Final stop position (centered)
            float endX = -targetIndex * step;

            // Start far right for fast initial blur
            float startX = endX + (count * step * 6.0f);
            m_strip->setPosition({ startX, 0.f });

            // Stronger decel:
            // 1) very fast linear (short)
            // 2) medium ease out
            // 3) long heavy ease out to "crawl" at the end
            float mid1X = endX + (count * step * 2.2f);
            float mid2X = endX + (count * step * 0.55f);

            auto fast = CCMoveTo::create(0.14f / speedMul, { mid1X, 0.f });                       // rocket-fast
            auto mid  = CCEaseSineOut::create(CCMoveTo::create(0.70f / speedMul, { mid2X, 0.f })); // start slowing
            auto slow = CCEaseExponentialOut::create(CCMoveTo::create(1.90f / speedMul, { endX, 0.f })); // heavy slow end

            m_strip->runAction(CCSequence::create(
                fast,
                mid,
                slow,
                CallFuncExt::create([this] { this->finish(); }),
                nullptr
            ));

            return true;
        }

        void finish() {
            m_top->setString(fmt::format("FPS = {}", m_pick).c_str());

            int fps = clampInt(m_pick, 1, 360);
            CCDirector::sharedDirector()->setAnimationInterval(1.0 / (double)fps);

            // ✅ wait a few seconds BEFORE closing (user request)
            this->runAction(CCSequence::create(
                CCDelayTime::create(2.2f),
                CallFuncExt::create([this] { this->cleanupAndResume(); }),
                nullptr
            ));
        }

        void cleanupAndResume() {
            if (m_pl) {
                // resume gameplay (no pause menu)
                m_pl->resumeSchedulerAndActions();
                markFinished(m_pl);
            }
            this->removeFromParentAndCleanup(true);
        }
    };
}

// =======================
// PlayLayer hookss
// =======================
class $modify(FPSGamblePlayLayer, PlayLayer) {
    struct Fields {
        bool graceActive = true; // ✅ cannot add members directly; use Fields
    };

    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        m_fields->graceActive = true;

        // Enable death roulette after a short grace (avoids "spins on create")
        this->scheduleOnce(schedule_selector(FPSGamblePlayLayer::endGrace), 0.6f);
    }

    void endGrace(float) {
        m_fields->graceActive = false;
        setReadyForNextDeath(this);
    }

    void onExit() {
        clearState(this);
        PlayLayer::onExit();
    }
//uh
    void resetLevel() {
        // During reset pipeline, do not allow roulette triggers
        auto& st = states()[this];
        st.readyForDeath = false;
        st.queued = false;

        PlayLayer::resetLevel();

        // Re-enable shortly after reset completes
        this->scheduleOnce(schedule_selector(FPSGamblePlayLayer::armAfterReset), 0.2f);
    }

    void armAfterReset(float) {
        if (!m_fields->graceActive) {
            setReadyForNextDeath(this);
        }
    }

    // scheduleOnce target (no lambdas; works on SEL_SCHEDULE signature)
    void runDeathRoulette(float) {
        // if somethin happen, do nothin
        if (m_fields->graceActive) {
            auto& st = states()[this];
            st.queued = false;
            return;
        }

        auto& st = states()[this];
        st.queued = false;

        if (st.active) return;
        if (std::chrono::steady_clock::now() < st.cooldownUntil) return;
        if (!this->m_player1) return;

        // settings
        int minV = 1, maxV = 360;
        if (auto mod = Mod::get()) {
            minV = (int)mod->getSettingValue<int64_t>("roulette-min");
            maxV = (int)mod->getSettingValue<int64_t>("roulette-max");
        }

        markStarted(this);

        // Put overlay on Scene so it keeps animating while PlayLayer is paused
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        float speedMul = 1.0f;
            bool hideBg = false;
            if (auto mod = Mod::get()) {
                speedMul = (float)mod->getSettingValue<double>("roulette-speed");
                hideBg = mod->getSettingValue<bool>("roulette-hide-bg");
            }

            auto overlay = RouletteOverlay::create(this, minV, maxV, speedMul, hideBg);
        scene->addChild(overlay, 9999999);

        // Freeze gameplay (no pause menu)
        this->pauseSchedulerAndActions();
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        PlayLayer::destroyPlayer(player, obj);

        if (m_fields->graceActive) return;
        if (!this->m_player1) return;

        // Trigger exactly once per death (re-armed after resetLevel)
        if (!canQueue(this)) return;

        // delayed
        this->scheduleOnce(schedule_selector(FPSGamblePlayLayer::runDeathRoulette), 0.0001f);
    }
};