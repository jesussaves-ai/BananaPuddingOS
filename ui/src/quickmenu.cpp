#include "quickmenu.h"
#include "soft.h"
#include <algorithm>
#include <cmath>

namespace banana
{
	// the palette's names are the engine's and the settings screen's (a user theme's colours come over them)
	QuickMenu::QuickMenu(const std::string& fontDir, int panelW, int panelH, float scale, bool l, bool c)
		: W(panelW), H(panelH), s(scale), light(l), clean(c)
	{
		auto load = [](const std::string& theme, const std::string& dir, const char* a, const char* b)
		{
			if (!theme.empty()) if (auto f = rnds::Font::load(theme)) return f;
			if (auto f = rnds::Font::load(dir + "/" + a)) return f;
			return rnds::Font::load(dir + "/" + b);
		};
		R = load(rnds::themeFiles.fontRegular, fontDir, "PixelifySans-Regular.ttf", "Inter-Regular.ttf");
		M = load(rnds::themeFiles.fontMedium, fontDir, "PixelifySans-Medium.ttf", "Inter-Medium.ttf");
	}

	QuickMenu::Pal QuickMenu::palette() const
	{
		Pal p;
		if (!light)
		{
			p.ink = rnds::Color::hex(0xf1ede2); p.muted = rnds::Color::hex(0xa39d8e); p.edge = rnds::Color::hex(0xe8e2d3);
			p.panelBand = rnds::Color::hex(0x3a3540); p.panelBody = rnds::Color::hex(0x17161c); p.panelLow = rnds::Color::hex(0x0b0a0d);
			p.blackish = rnds::Color::hex(0x0b0a0d);
		}
		else
		{
			rnds::Color sh = rnds::Color::hex(0x3a2e14);
			p.ink = rnds::Color::hex(0x2a2620); p.muted = rnds::Color::hex(0x7a7262); p.edge = rnds::Color::hex(0x3a342c);
			p.panelBand = rnds::Color::hex(0xffffff); p.panelBody = rnds::Color::hex(0xf6f2ea); p.panelLow = rnds::Color::hex(0xd8d0c0);
			p.blackish = rnds::Color(sh.r, sh.g, sh.b, 0.3f);
		}
		p.pick = rnds::Color::hex(rnds::accent.base);
		p.pickBand = rnds::Color::hex(rnds::accent.band);
		p.pickLow = rnds::Color::hex(rnds::accent.low);
		p.pickEdge = light ? rnds::Color::hex(rnds::accent.dark) : rnds::Color::hex(0xffffff);
		p.onPick = rnds::Color::hex(0x17161c);
		struct { const char* n; rnds::Color* m; } names[] = {
			{ "ink", &p.ink }, { "muted", &p.muted }, { "edge", &p.edge }, { "panelBand", &p.panelBand },
			{ "panelBody", &p.panelBody }, { "panelLow", &p.panelLow }, { "blackish", &p.blackish },
			{ "onPick", &p.onPick }
		};
		for (auto& e : names) rnds::themeColor(e.n, *e.m);
		return p;
	}

	void QuickMenu::setEnabled(bool on)
	{
		enabled = on;
		if (!on) closeNow();
	}

	void QuickMenu::open(double now)
	{
		target = 1;
		last = now;
		pressed = -1;
		if (progress <= 0.001f) progress = 0.001f;	// visible from the touch that opened it
	}

	void QuickMenu::close(double now)
	{
		target = 0;
		last = now;
		pressed = -1;
	}

	void QuickMenu::closeNow()
	{
		progress = target = 0;
		pressed = -1;
	}

	bool QuickMenu::update(double now)
	{
		if (progress == target) return false;
		double dt = std::max(0.0, std::min(50.0, now - last));
		last = now;
		float step = (float)(dt / 150.0);	// a 150 ms slide
		if (target > progress) progress = std::min(target, progress + step);
		else progress = std::max(target, progress - step);
		return true;
	}

	int QuickMenu::nextWake(double) const
	{
		return animating() ? 16 : -1;
	}

	const QuickMenu::Tile* QuickMenu::tiles() const
	{
		static const Tile menu[] = {
			{ HOME, "Home", "The system grid" },
			{ GAMES, "Games", "This system's library" },
			{ SEARCH, "Search", "Find a game" },
			{ SETTINGS, "Settings", "Display, sound, power" },
			{ CLOSE, "Close", "Back to the menu" },
		};
		static const Tile game[] = {
			{ SAVE_STATE, "Save", "A state now" },
			{ LOAD_STATE, "Load", "The last state" },
			{ RETROARCH_MENU, "Menu", "RetroArch's menu" },
			{ FAST_FORWARD, "Fast", "Fast forward" },
			{ QUIT_GAME, "Exit game", "Back to the library" },
			{ CLOSE, "Close", "Back to the game" },
		};
		return mode == GAME ? game : menu;
	}

	int QuickMenu::tileCount() const
	{
		return mode == GAME ? 6 : 5;
	}

	void QuickMenu::tileRect(int i, float& x, float& y, float& w, float& h) const
	{
		const int n = tileCount(), row = i / 3, col = i % 3;
		const int inRow = std::min(3, n - row * 3);
		const float tw = 184, th = 112, gap = 18;
		const float rowW = inRow * tw + (inRow - 1) * gap;
		w = tw; h = th;
		x = (640 - rowW) / 2 + col * (tw + gap);
		y = 176 + row * (th + 20);
	}

	bool QuickMenu::hitGrabber(float x, float y) const
	{
		// a little wider than the bar: a finger isn't a pixel
		return x >= 250 && x < 390 && y >= 448 && y < 480;
	}

	int QuickMenu::hitTile(float x, float y) const
	{
		for (int i = 0; i < tileCount(); i++)
		{
			float tx, ty, tw, th;
			tileRect(i, tx, ty, tw, th);
			if (x >= tx && x < tx + tw && y >= ty && y < ty + th) return i;
		}
		return -1;
	}

	bool QuickMenu::touch(int screen, float x, float y, bool down, double now, Action& action)
	{
		if (!enabled || screen != panel) return false;
		float lx = x / s, ly = y / s;
		if (!isOpen())
		{
			if (down && hitGrabber(lx, ly)) { open(now); return true; }
			return false;
		}
		if (animating())
		{
			if (down) pressed = -1;
			return true;	// the sheet's position is moving: let it settle first
		}
		if (down)
		{
			pressed = hitTile(lx, ly);
			return true;
		}
		int was = pressed;
		pressed = -1;
		if (was >= 0 && hitTile(lx, ly) == was)
		{
			Action a = tiles()[was].action;
			if (a == GAMES && !canGames) { close(now); return true; }
			action = a;
			close(now);
			return true;
		}
		// a tap on the dimmed screen, outside the sheet
		if (ly < 96 || ly > 468 || lx < 14 || lx > 626) close(now);
		return true;
	}

	bool QuickMenu::touchMove(int screen, float, float)
	{
		return enabled && screen == panel && isOpen();
	}

	rnds::Color QuickMenu::foreground(const Pal& p, Action a) const
	{
		if (a == QUIT_GAME) return rnds::Color::hex(0xff8a96);
		if (a == CLOSE) return p.muted;
		return p.ink;
	}

	void QuickMenu::box(SoftBackend& be, const Pal& p, const char* kind, bool on, float x, float y, float w, float h)
	{
		int dw = (int)std::lround(X(w)), dh = (int)std::lround(X(h));
		std::string key = std::string("box:") + kind + (on ? "1" : "0") + ":" + std::to_string(dw) + "x" + std::to_string(dh);
		auto it = cache.find(key);
		if (it == cache.end())
		{
			rnds::BoxStyle st;
			auto inset = [](float dx, float dy, float sp, rnds::Color c) { return rnds::Shadow{ dx, dy, sp, c, true }; };
			auto outer = [](float dx, float dy, rnds::Color c) { return rnds::Shadow{ dx, dy, 0, c, false }; };
			float b3 = s == 1 ? 3 : std::max(1.0f, std::floor(3 * s));
			if (std::string(kind) == "sheet")
			{
				st.radius = X(18); st.border = b3; st.borderColor = p.edge; st.bg = p.panelBody;
				st.bands = { rnds::Band{ 0, X(10), p.panelBand }, rnds::Band{ X(10), -1, p.panelBody } };
				st.shadows = { inset(0, -X(6), 0, p.panelLow), outer(X(4), X(4), p.blackish) };
			}
			else if (std::string(kind) == "pick")
			{
				st.radius = X(14); st.border = b3; st.borderColor = p.pickEdge; st.bg = p.pick;
				st.bands = { rnds::Band{ 0, X(8), p.pickBand }, rnds::Band{ X(8), -1, p.pick } };
				st.shadows = { inset(0, -X(6), 0, p.pickLow), outer(X(3), X(3), p.blackish) };
			}
			else if (std::string(kind) == "grabber")
			{
				st.radius = X(6); st.border = std::max(1.0f, std::round(s)); st.borderColor = rnds::Color::hex(rnds::accent.base);
				st.bg = rnds::Color::hex(rnds::accent.low);
			}
			else
			{
				st.radius = X(14); st.border = b3; st.borderColor = p.edge; st.bg = p.panelBody;
				st.bands = { rnds::Band{ 0, X(8), p.panelBand }, rnds::Band{ X(8), -1, p.panelBody } };
				st.shadows = { inset(0, -X(6), 0, p.panelLow), outer(X(3), X(3), p.blackish) };
			}
			if (clean)
			{
				// Carts: flat panels, a hairline edge, no bands or hard shadows
				rnds::BoxStyle c;
				float hair = std::max(1.0f, std::round(s));
				if (std::string(kind) == "grabber") { c.radius = X(6); c.bg = p.pick; }
				else if (on) { c.radius = X(12); c.bg = p.pick; }
				else
				{
					c.radius = X(14);
					c.bg = rnds::Color::hex(0x1f2126);
					c.border = hair;
					c.borderColor = rnds::Color(1, 1, 1, 0.08f);
				}
				st = c;
			}
			rnds::PaintedBox pb = rnds::paintBox(st, dw, dh);
			Cached c;
			c.tex = be.upload(pb.bmp, true);
			c.ox = pb.originX; c.oy = pb.originY;
			it = cache.emplace(key, c).first;
		}
		const Cached& c = it->second;
		be.draw(c.tex, std::round(X(x)) - c.ox, std::round(X(y)) - c.oy, (float)c.tex->w, (float)c.tex->h, rnds::Color(1, 1, 1, 1));
	}

	float QuickMenu::text(SoftBackend& be, const std::string& t, bool medium, float size, float x, float top,
		const rnds::Color& c, bool measure)
	{
		if (t.empty()) return 0;
		rnds::TextStyle st;
		st.font = medium ? M : R;
		st.size = size;
		if (!st.font) return 0;
		std::string key = std::string("txt:") + (medium ? "m" : "r") + std::to_string(size) + ":" + t;
		auto it = cache.find(key);
		if (it == cache.end())
		{
			rnds::TextLine l = rnds::layoutLine(rnds::utf8to32(t), st);
			float asc = st.font->ascent(size), desc = st.font->descent(size), lh = asc + desc;
			rnds::TextBitmap tb = rnds::renderLine(l, st, s, asc + std::floor((lh - (asc + desc)) / 2));
			Cached cc;
			if (!tb.bmp.empty()) cc.tex = be.upload(tb.bmp, true);
			cc.ox = tb.padX; cc.oy = tb.padY; cc.w = l.width;
			it = cache.emplace(key, cc).first;
		}
		const Cached& cc = it->second;
		if (!measure && cc.tex)
			be.draw(cc.tex, std::floor(X(x)) - cc.ox, std::floor(X(top)) - cc.oy, (float)cc.tex->w, (float)cc.tex->h, c);
		return cc.w;
	}

	void QuickMenu::grabber(SoftBackend& be, const Pal& p)
	{
		box(be, p, "grabber", false, 268, 463, 104, 13);
		for (int i = 0; i < 3; i++)
			be.fill(std::round(X(302 + i * 12)), std::round(X(468)), std::round(X(4)), std::round(X(3)), p.ink);
	}

	void QuickMenu::draw(SoftBackend& be, int screen, double)
	{
		if (!enabled || screen != panel) return;
		Pal p = palette();
		if (progress <= 0.001f) { grabber(be, p); return; }

		float e = progress * progress * (3 - 2 * progress);	// ease the slide, keep the dimming smooth
		be.fill(0, 0, (float)W, (float)H, rnds::Color(0, 0, 0, 0.45f * e));

		const float dy = (1 - e) * X(76);
		box(be, p, "sheet", false, 14, 96 + dy / s, 612, 372);

		text(be, "Quick menu", true, 23, 38, 106 + dy / s, p.ink);
		text(be, mode == GAME ? "While the game runs" : "Menu actions", false, 14, 38, 140 + dy / s, p.muted);

		for (int i = 0; i < tileCount(); i++)
		{
			float x, y, w, h;
			tileRect(i, x, y, w, h);
			y += dy / s;
			const Tile& t = tiles()[i];
			bool on = pressed == i;
			bool off = t.action == GAMES && !canGames;
			box(be, p, on ? "pick" : "tile", on, x, y, w, h);
			rnds::Color fg = off ? p.muted : (on ? p.onPick : foreground(p, t.action));
			float lw = text(be, t.label, true, 20, x + w / 2, y + 18, fg, true);
			text(be, t.label, true, 20, x + w / 2 - lw / 2, y + 18, fg);
			rnds::Color sub = on ? p.onPick : p.muted;
			float hw = text(be, t.hint, false, 13, x + w / 2, y + 52, sub, true);
			text(be, t.hint, false, 13, x + w / 2 - hw / 2, y + 52, sub);
		}
	}
}
