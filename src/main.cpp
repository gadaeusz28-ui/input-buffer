#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

static constexpr int kWindows[] = {0, 4, 8, 16};
static constexpr int kWindowCount = 4;
static constexpr float kStep = 1.f / 240.f;

static bool g_sim = false;        // trwa symulacja
static bool g_simDied = false;    // gracz zginal w symulacji
static bool g_bypass = false;     // nasze wlasne wstrzykniecia klikow
static bool g_wantSim = false;    // zapamietany klik czeka na symulacje
static bool g_userHeld = false;   // gracz fizycznie trzyma przycisk
static int g_pending = -1;        // ile krokow do opoznionego skoku
static int g_releaseWait = 0;     // ile krokow trzymac sztuczny klik
static int g_tap = 4;             // dlugosc "klikniecia" w krokach

static int getWindow() {
	return Mod::get()->getSavedValue<int>("window", 0);
}

static bool isHoldMode(PlayerObject* p) {
	return p->m_isShip || p->m_isDart || p->m_isRobot;
}

static void injectDown(GJBaseGameLayer* l) {
	g_bypass = true;
	l->handleButton(true, 1, true);
	g_bypass = false;
}

static void injectUp(GJBaseGameLayer* l) {
	g_bypass = true;
	l->handleButton(false, 1, true);
	g_bypass = false;
}

// Symuluje `horizon` krokow z klikiem po `delay` krokach. true = przezyjemy.
static bool simulate(PlayLayer* pl, int delay, int tap, int horizon) {
	g_simDied = false;
	for (int s = 0; s < horizon; ++s) {
		if (s == delay) pl->handleButton(true, 1, true);
		if (s == delay + tap) pl->handleButton(false, 1, true);
		pl->processCommands(kStep, false, true);
		if (g_simDied) return false;
	}
	return true;
}

// 0 = nie opoznaj, >0 = opoznienie w krokach.
static int chooseDelay(PlayLayer* pl, int window, int tap) {
	Ref<CheckpointObject> cp = pl->createCheckpoint();
	if (!cp) return 0;

	int horizon = window + tap + 14;
	int result = 0;
	g_sim = true;
	for (int d = 0; d <= window; ++d) {
		bool ok = simulate(pl, d, tap, horizon);
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
		g_wantSim = false;
		PlayLayer::resetLevel();
	}

	// Symulacja odpala sie tutaj, poza petla fizyki.
	void update(float dt) {
		if (g_wantSim && !g_sim) {
			g_wantSim = false;
			int window = getWindow();
			bool alive = m_player1 && !m_player1->m_isDead;

			int d = 0;
			if (window > 0 && alive) {
				g_tap = isHoldMode(m_player1) ? 10 : 4;
				d = chooseDelay(this, window, g_tap);
			}

			if (d > 0) {
				g_pending = d;
			} else {
				injectDown(this);
				if (!g_userHeld) g_releaseWait = g_tap;
			}
		}
		PlayLayer::update(dt);
	}
};

class $modify(BufferGJBGL, GJBaseGameLayer) {
	void handleButton(bool down, int button, bool isPlayer1) {
		auto pl = PlayLayer::get();
		bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == this;

		if (g_sim || g_bypass || button != 1 || !isPlayer1 || !mine ||
		    getWindow() <= 0 || !m_player1 || m_player1->m_isDead) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		if (down) {
			g_userHeld = true;
			g_pending = -1;
			if (g_releaseWait > 0) {
				g_releaseWait = 0;
				injectUp(this);
			}
			g_wantSim = true; // klik poczeka na symulacje w PlayLayer::update
			return;
		}

		g_userHeld = false;
		if (g_wantSim || g_pending >= 0 || g_releaseWait > 0) return; // puscimy go sami
		GJBaseGameLayer::handleButton(false, button, isPlayer1);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		auto pl = PlayLayer::get();
		bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == this;

		if (mine && !g_sim) {
			if (!m_player1 || m_player1->m_isDead) {
				g_pending = -1;
				g_releaseWait = 0;
				g_wantSim = false;
			} else if (g_pending > 0) {
				g_pending--;
			} else if (g_pending == 0) {
				g_pending = -1;
				injectDown(this);
				if (!g_userHeld) g_releaseWait = g_tap;
			} else if (g_releaseWait > 0) {
				if (--g_releaseWait == 0 && !g_userHeld) {
					injectUp(this);
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
