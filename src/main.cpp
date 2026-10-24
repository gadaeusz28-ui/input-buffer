#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

static constexpr int kWindows[] = {0, 4, 8, 16};
static constexpr int kWindowCount = 4;
static constexpr int kHorizon = 48;     // ile krokow symulujemy do przodu
static constexpr int kTapLength = 4;    // zakladamy krotkie klikniecie
static constexpr float kStep = 1.f / 240.f;

static bool g_sim = false;
static bool g_simDied = false;
static int g_pending = -1;      // ile krokow do opoznionego skoku (-1 = brak)
static bool g_userHeld = false;
static int g_releaseWait = 0;

static int getWindow() {
	return Mod::get()->getSavedValue<int>("window", 0);
}

static bool isCube(PlayerObject* p) {
	return p && !p->m_isShip && !p->m_isBall && !p->m_isBird &&
	       !p->m_isDart && !p->m_isRobot && !p->m_isSpider && !p->m_isSwing;
}

// Symuluje kilka krokow z klikiem po `delay` krokach. true = przezyjemy.
static bool simulate(PlayLayer* pl, int delay) {
	g_simDied = false;
	for (int s = 0; s < kHorizon; ++s) {
		if (s == delay) pl->handleButton(true, 1, true);
		if (s == delay + kTapLength) pl->handleButton(false, 1, true);
		pl->processCommands(kStep, false, true);
		if (g_simDied) return false;
	}
	return true;
}

// Zwraca 0 = nie opoznaj, >0 = opoznienie w krokach.
static int chooseDelay(PlayLayer* pl, int window) {
	Ref<CheckpointObject> cp = pl->createCheckpoint();
	if (!cp) return 0;

	g_sim = true;
	int result = 0;
	for (int d = 0; d <= window; ++d) {
		bool ok = simulate(pl, d);
		pl->loadFromCheckpoint(cp);
		pl->handleButton(false, 1, true);
		if (ok) {
			result = d;
			break;
		}
	}
	g_sim = false;
	return result;
}

class $modify(SimPlayLayer, PlayLayer) {
	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		if (g_sim) {
			g_simDied = true; // w symulacji tylko zapamietujemy smierc
			return;
		}
		PlayLayer::destroyPlayer(player, obj);
	}

	void resetLevel() {
		g_pending = -1;
		g_releaseWait = 0;
		g_userHeld = false;
		PlayLayer::resetLevel();
	}
};

class $modify(BufferGJBGL, GJBaseGameLayer) {
	void handleButton(bool down, int button, bool isPlayer1) {
		int window = getWindow();
		auto pl = PlayLayer::get();
		bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == static_cast<GJBaseGameLayer*>(this);

		if (g_sim || window <= 0 || button != 1 || !isPlayer1 || !mine ||
		    !m_player1 || m_player1->m_isDead) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		if (down) {
			g_userHeld = true;
			g_pending = -1;
			if (isCube(m_player1)) {
				int d = chooseDelay(pl, window);
				if (d > 0) {
					g_pending = d; // polykamy klik, odpalimy go pozniej
					return;
				}
			}
			GJBaseGameLayer::handleButton(true, button, isPlayer1);
			return;
		}

		g_userHeld = false;
		if (g_pending >= 0) return; // klik jeszcze nie poszedl, puscimy go po skoku
		GJBaseGameLayer::handleButton(false, button, isPlayer1);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		if (!g_sim) {
			if (m_player1 && m_player1->m_isDead) {
				g_pending = -1;
				g_releaseWait = 0;
			}
			if (g_pending > 0) {
				g_pending--;
			} else if (g_pending == 0) {
				g_pending = -1;
				GJBaseGameLayer::handleButton(true, 1, true);
				if (!g_userHeld) g_releaseWait = kTapLength;
			} else if (g_releaseWait > 0) {
				if (--g_releaseWait == 0 && !g_userHeld) {
					GJBaseGameLayer::handleButton(false, 1, true);
				}
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
