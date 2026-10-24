#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

// Dostepne okna bufora (w krokach fizyki). 0 = wylaczone.
static constexpr int kWindows[] = {0, 4, 8, 16};
static constexpr int kWindowCount = 4;

static int getWindow() {
	return Mod::get()->getSavedValue<int>("window", 0);
}

// Bufor ma sens tylko w trybie kostki.
static bool isCube(PlayerObject* p) {
	return p && !p->m_isShip && !p->m_isBall && !p->m_isBird &&
	       !p->m_isDart && !p->m_isRobot && !p->m_isSpider && !p->m_isSwing;
}

class $modify(BufferGJBGL, GJBaseGameLayer) {
	struct Fields {
		bool pending[2] = {false, false};
		int timer[2] = {0, 0};
		int groundSteps[2] = {0, 0};
	};

	void handleButton(bool down, int button, bool isPlayer1) {
		int window = getWindow();
		if (window <= 0 || button != 1) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		auto f = m_fields.self();
		int i = isPlayer1 ? 0 : 1;
		auto* player = isPlayer1 ? m_player1 : m_player2;

		if (down) {
			if (f->pending[i]) {
				f->pending[i] = false;
				f->groundSteps[i] = 0;
				GJBaseGameLayer::handleButton(false, button, isPlayer1);
			}
			GJBaseGameLayer::handleButton(true, button, isPlayer1);
			return;
		}

		if (player && isCube(player) && !player->m_isOnGround) {
			f->pending[i] = true;
			f->timer[i] = window;
			f->groundSteps[i] = 0;
			return;
		}

		GJBaseGameLayer::handleButton(false, button, isPlayer1);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		auto f = m_fields.self();
		for (int i = 0; i < 2; ++i) {
			if (!f->pending[i]) continue;
			auto* player = i == 0 ? m_player1 : m_player2;

			if (!player || player->m_isDead) {
				f->pending[i] = false;
				f->groundSteps[i] = 0;
				continue;
			}

			if (player->m_isOnGround) f->groundSteps[i]++;
			f->timer[i]--;

			if (f->groundSteps[i] >= 2 || f->timer[i] <= 0) {
				f->pending[i] = false;
				f->groundSteps[i] = 0;
				GJBaseGameLayer::handleButton(false, 1, i == 0);
			}
		}
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
	}
};

$on_mod(Loaded) {
	listenForKeybindSettingPresses("cycle-key", [](Keybind const&, bool down, bool repeat, double) {
		if (!down || repeat) return;

		int current = getWindow();
		int idx = 0;
		for (int k = 0; k < kWindowCount; ++k) {
			if (kWindows[k] == current) idx = k;
		}
		int next = kWindows[(idx + 1) % kWindowCount];
		Mod::get()->setSavedValue<int>("window", next);

		std::string text = next == 0
			? "Input Buffer: OFF"
			: fmt::format("Input Buffer: {} krokow", next);
		Notification::create(text, NotificationIcon::None, 1.f)->show();
	});
}
