#pragma once
// banana-ui: the bottom panel's quick-menu sheet. A touch on the grabber at the panel's bottom edge opens a small
// grid of the things that would otherwise need the buttons (the system grid, a system's games, Search, Settings),
// or, while a game runs and banana-ui holds the other panel, RetroArch's own quick actions (save, load, menu, fast
// forward, quit). It is drawn through SoftBackend::overlay, so it is part of the screen's recorded draw list: the
// backend's damage tracking redraws only the sheet's rectangle while it slides, and nothing at all once it rests.
// No frame is asked for while it is closed (the grabber is the same draws every frame: the backend skips them).
#include "../rnds/RndsUI.h"
#include <map>
#include <memory>
#include <string>

namespace banana
{
	class SoftBackend;

	class QuickMenu
	{
	public:
		enum Action
		{
			NONE,
			HOME,			// the system grid (Menu)
			GAMES,			// the current system's library (Menu)
			SEARCH,			// the games search (Menu)
			SETTINGS,		// Settings (Menu)
			SAVE_STATE,		// (Game)
			LOAD_STATE,
			RETROARCH_MENU,
			FAST_FORWARD,
			QUIT_GAME,		// back to the library
			CLOSE
		};
		enum Mode { MENU, GAME };

		// fontDir: the menu's fonts (Pixelify Sans, or Inter for Carts); themeFiles' fonts win when a user theme has
		// them. light: the theme's palette; clean: Carts' flat boxes (no bands or hard shadows)
		QuickMenu(const std::string& fontDir, int panelW, int panelH, float scale, bool light, bool clean);

		void setMode(Mode m) { mode = m; }
		void setScreen(int screen) { panel = screen; }
		// menu context: whether there is a system to open, and whether the sheet may show at all (not over Settings
		// or the launch animation)
		void setCanGames(bool on) { canGames = on; }
		void setEnabled(bool on);
		// a touch on screen 1 (or the game panel's screen): true when the sheet took it; action is set on the tap
		// that chose one (the sheet then closes itself)
		bool touch(int screen, float x, float y, bool down, double now, Action& action);
		bool touchMove(int screen, float x, float y);

		void open(double now);
		void close(double now);
		void closeNow();
		bool isOpen() const { return progress > 0.001f; }
		bool animating() const { return progress != target; }
		// ms until the next step of the slide, -1 at rest
		int nextWake(double now) const;
		// true when the slide moved (the caller wants a frame)
		bool update(double now);

		// called between a screen's beginScreen and endScreen (SoftBackend::overlay); screen is the panel it draws on
		void draw(SoftBackend& be, int screen, double now);

	private:
		struct Pal
		{
			rnds::Color ink, muted, edge, panelBand, panelBody, panelLow, blackish;
			rnds::Color pick, pickBand, pickLow, pickEdge, onPick;
		};
		struct Cached { rnds::TexPtr tex; int ox = 0, oy = 0; float w = 0; };
		struct Tile { Action action; const char* label; const char* hint; };

		const Tile* tiles() const;
		int tileCount() const;
		Pal palette() const;
		rnds::Color foreground(const Pal& p, Action a) const;
		void box(SoftBackend& be, const Pal& p, const char* kind, bool on, float x, float y, float w, float h);
		float text(SoftBackend& be, const std::string& t, bool medium, float size, float x, float top,
			const rnds::Color& c, bool measure = false);
		void grabber(SoftBackend& be, const Pal& p);
		void tileRect(int i, float& x, float& y, float& w, float& h) const;
		bool hitGrabber(float x, float y) const;
		int hitTile(float x, float y) const;

		int W, H, panel = 1;
		float s;
		bool light, clean;
		Mode mode = MENU;
		bool enabled = true, canGames = true;
		float progress = 0, target = 0;
		double last = 0;
		int pressed = -1;
		std::shared_ptr<rnds::Font> R, M;
		std::map<std::string, Cached> cache;
		float X(float v) const { return v * s; }
	};
}
