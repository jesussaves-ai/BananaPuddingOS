#include "sound.h"
#include "../rnds/RndsUI.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef BANANA_ALSA
#include <alsa/asoundlib.h>
#include <sys/stat.h>
#endif

namespace banana
{
	// everything is mixed at this rate, so the chimes (written at 44100) play back without touching a resampler; a
	// music track at another rate is brought to it once, when it is loaded
	static const int SFX_RATE = 44100;
	// the music sits under the chimes (the one-shots keep their own level): about -7 dB, a bed, not the main event
	static const int MUSIC_GAIN_Q = 115;	// 0.45 in 1/256ths

	// 16-bit PCM WAV only (what rnds/gen_sounds.py writes)
	static bool loadWav(const std::string& path, std::vector<int16_t>& pcm, int& rate, int& channels)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f) return false;
		std::vector<uint8_t> b;
		uint8_t buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
		fclose(f);
		if (b.size() < 44 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) return false;
		size_t at = 12;
		int bits = 0;
		while (at + 8 <= b.size())
		{
			uint32_t len = b[at + 4] | (b[at + 5] << 8) | (b[at + 6] << 16) | ((uint32_t)b[at + 7] << 24);
			const uint8_t* c = b.data() + at + 8;
			if (!memcmp(b.data() + at, "fmt ", 4) && len >= 16)
			{
				channels = c[2] | (c[3] << 8);
				rate = c[4] | (c[5] << 8) | (c[6] << 16) | (c[7] << 24);
				bits = c[14] | (c[15] << 8);
			}
			else if (!memcmp(b.data() + at, "data", 4))
			{
				if (bits != 16 || channels < 1) return false;
				len = (uint32_t)std::min<size_t>(len, b.size() - at - 8);
				pcm.resize(len / 2);
				memcpy(pcm.data(), c, pcm.size() * 2);
				return true;
			}
			at += 8 + len + (len & 1);
		}
		return false;
	}

	// a music track at the mix rate, once, on the thread that loads it: linear interpolation is enough for a bed, and
	// it keeps the worker's per-sample work to a lookup (the chimes never hit this path)
	static void resampleTo(std::vector<int16_t>& pcm, int& rate, int channels, int to)
	{
		if (rate == to || channels < 1) return;
		size_t total = pcm.size() / (size_t)channels;
		if (!total) return;
		size_t n = (size_t)std::llround((double)total * to / rate);
		std::vector<int16_t> out(n * (size_t)channels);
		for (size_t i = 0; i < n; i++)
		{
			double sp = (double)i * rate / to;
			size_t s = (size_t)sp; if (s >= total) s = total - 1;
			size_t s2 = std::min(s + 1, total - 1);
			double f = sp - (double)s;
			for (int ch = 0; ch < channels; ch++)
			{
				double a = pcm[s * channels + ch], b = pcm[s2 * channels + ch];
				out[i * channels + ch] = (int16_t)std::lround(a + (b - a) * f);
			}
		}
		pcm.swap(out);
		rate = to;
	}

	Sound::Sound(const std::string& assetsSounds) : dir(assetsSounds)
	{
		for (const char* n : { "scroll", "select", "back", "fav", "ready", "insert" })
		{
			// a user theme's own sound for it (rnds::themeFiles; "off": none), else the theme's (one that doesn't
			// load: the theme's)
			auto own = rnds::themeFiles.sounds.find(n);
			if (own != rnds::themeFiles.sounds.end() && own->second == "off") continue;
			Clip c;
			if (own != rnds::themeFiles.sounds.end() && loadWav(own->second, c.pcm, c.rate, c.channels)) { clips[n] = std::move(c); continue; }
			if (loadWav(dir + "/" + n + ".wav", c.pcm, c.rate, c.channels)) clips[n] = std::move(c);
		}
		th = std::thread([this] { run(); });
	}

	Sound::~Sound()
	{
		{
			std::lock_guard<std::mutex> l(m);
			stop = true;
		}
		cv.notify_all();
		if (th.joinable()) th.join();
	}

	void Sound::play(const std::string& name)
	{
		if (!enabled || !clips.count(name)) return;
		{
			std::lock_guard<std::mutex> l(m);
			queued = name;
			paused = false;		// a game may have had the device; a sound means the menu is back
		}
		cv.notify_all();
	}

	void Sound::setMusicEnabled(bool on)
	{
		// a theme's own track first (that isn't "off"), else the one beside the sounds; a track that can't be read is
		// no music, not an error the menu has to mind
		std::string path;
		if (on)
		{
			auto own = rnds::themeFiles.sounds.find("music");
			if (own != rnds::themeFiles.sounds.end()) { if (own->second != "off") path = own->second; }
			else path = dir + "/music.wav";
		}
		if (path == musicPath) return;		// the same track: let it play on (changing other settings mustn't restart it)
		std::shared_ptr<const Clip> c;
		if (!path.empty())
		{
			Clip tmp;
			if (loadWav(path, tmp.pcm, tmp.rate, tmp.channels) && !tmp.pcm.empty())
			{
				resampleTo(tmp.pcm, tmp.rate, tmp.channels, SFX_RATE);
				c = std::make_shared<const Clip>(std::move(tmp));
			}
			else fprintf(stderr, "banana-ui: music: %s can't be read (no background music)\n", path.c_str());
		}
		{
			std::lock_guard<std::mutex> l(m);
			musicPath = path;
			music = c;
			musicChanged = true;
		}
		cv.notify_all();
	}

	void Sound::release()
	{
		{
			std::lock_guard<std::mutex> l(m);
			closeNow = true;
			paused = true;		// and don't reopen for the music until resume()
			queued.clear();
		}
		cv.notify_all();
		// wait until the device is closed
		for (int i = 0; i < 100; i++)
		{
			{
				std::lock_guard<std::mutex> l(m);
				if (!closeNow) return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

	void Sound::resume()
	{
		{
			std::lock_guard<std::mutex> l(m);
			paused = false;
		}
		cv.notify_all();
	}

	void Sound::run()
	{
#ifdef BANANA_ALSA
		// While the device is open the stream never stops: after a sound, silence follows, until QUIET with nothing to
		// play. The RG DS Plus's speaker amplifiers (aw88166) are started by their driver when the stream powers up,
		// and the first try fails because the I2S clock isn't running yet; the next try is ~0.4 s later and finds no
		// clock either if a short click has already ended, so an amplifier gave up for the whole time the device was
		// open (one speaker, or none). A running stream lets the retry find its clock. Writing 10 ms at a time also
		// lets a new sound cut the one playing (a held direction scrolls fast) within the 30 ms buffer, and lets the
		// music loop under it without either one holding the frames' thread.
		const auto QUIET = std::chrono::seconds(20);
		snd_pcm_t* pcm = nullptr;
		int openRate = 0, openCh = 0;
		std::vector<int16_t> mix;			// one 10 ms chunk: the music, then the chimes over it
		std::shared_ptr<const Clip> mus;	// the loop playing now
		double musPos = 0;
		const Clip* cur = nullptr;			// the one-shot sound playing now
		double curPos = 0;
		auto closeDev = [&] { if (pcm) { snd_pcm_drop(pcm); snd_pcm_close(pcm); pcm = nullptr; } };
		// Where sound plays (speakers or headphones) is bananad's /run/banana/asound.conf, which it replaces when the
		// jack changes. alsa-lib reads its configuration once per process (a file it loaded through a hook, as
		// /etc/asound.conf is, isn't looked at again), so a change means: close, forget the configuration, open again.
		auto routeId = [] { struct stat st; return stat("/run/banana/asound.conf", &st) ? 0LL : (long long)st.st_ino * 1000003LL + st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec; };
		long long routeLoaded = routeId();
		int routeCheck = 0;
		auto lastSound = std::chrono::steady_clock::now();

		// mixes one clip into the chunk, at its own rate and channel count, advancing pos in its own frames; a loop
		// wraps at the end (seamlessly: the track was written to), a one-shot just stops
		auto mixClip = [&](const Clip& c, double& pos, bool loop, int gainQ)
		{
			const int ch = c.channels;
			if (ch < 1) return;
			size_t total = c.pcm.size() / (size_t)ch;
			if (!total) return;
			size_t frames = mix.size() / (size_t)openCh;
			double step = (double)c.rate / openRate;
			for (size_t i = 0; i < frames; i++)
			{
				if (pos >= (double)total)
				{
					if (!loop) { pos = (double)total; break; }
					pos = std::fmod(pos, (double)total);
				}
				size_t s = (size_t)pos;
				if (s >= total) s = total - 1;
				const int16_t* fr = &c.pcm[s * (size_t)ch];
				int l = fr[0], r = ch > 1 ? fr[1] : fr[0];
				int16_t* d = &mix[i * (size_t)openCh];
				int a = d[0] + l * gainQ / 256;
				d[0] = (int16_t)(a < -32768 ? -32768 : a > 32767 ? 32767 : a);
				if (openCh > 1) { int b = d[1] + r * gainQ / 256; d[1] = (int16_t)(b < -32768 ? -32768 : b > 32767 ? 32767 : b); }
				pos += step;
			}
		};

		for (;;)
		{
			std::string name;
			{
				std::unique_lock<std::mutex> l(m);
				// closed: nothing to do until a sound, or the music (when not paused for a game)
				if (!pcm) cv.wait(l, [this] { return stop || closeNow || (!paused && (!queued.empty() || music)); });
				if (stop) break;
				if (closeNow) { closeDev(); cur = nullptr; closeNow = false; continue; }	// a game is starting
				if (musicChanged) { mus = music; musPos = 0; musicChanged = false; if (!mus) lastSound = std::chrono::steady_clock::now(); }
				name = queued;
				queued.clear();
			}
			auto now = std::chrono::steady_clock::now();
			if (!name.empty())
			{
				const Clip& c = clips[name];
				cur = &c;
				curPos = 0;
				lastSound = now;
			}
			const Clip* fmt = mus ? mus.get() : cur;
			if (fmt && (!pcm || openRate != fmt->rate || openCh != fmt->channels))
			{
				if (pcm) closeDev();
				if (long long r = routeId(); r != routeLoaded) { snd_config_update_free_global(); routeLoaded = r; }
				if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) { pcm = nullptr; std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
				if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, fmt->channels, fmt->rate, 1, 30000) < 0)
				{
					snd_pcm_close(pcm); pcm = nullptr;
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				openRate = fmt->rate; openCh = fmt->channels;
				mix.assign((size_t)(openRate / 100) * (size_t)openCh, 0);
			}
			if (!pcm) continue;
			// nothing playing: hold the stream open (the amplifiers) with silence until QUIET has passed
			if (!cur && !mus)
			{
				if (now - lastSound >= QUIET) { closeDev(); continue; }
				std::fill(mix.begin(), mix.end(), 0);
			}
			else
			{
				std::fill(mix.begin(), mix.end(), 0);
				if (mus) mixClip(*mus, musPos, true, MUSIC_GAIN_Q);
				if (cur)
				{
					size_t total = cur->pcm.size() / (size_t)cur->channels;
					mixClip(*cur, curPos, false, 256);
					if (!total || curPos >= (double)total) { cur = nullptr; lastSound = std::chrono::steady_clock::now(); }
				}
			}
			// the jack changed while the stream runs (every 250 ms): the next sound opens where sound plays now
			if (++routeCheck >= 25) { routeCheck = 0; if (routeId() != routeLoaded) { closeDev(); cur = nullptr; musPos = 0; continue; } }
			const int16_t* p = mix.data();
			snd_pcm_sframes_t frames = (snd_pcm_sframes_t)(mix.size() / (size_t)openCh);
			while (frames > 0)
			{
				snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, (snd_pcm_uframes_t)frames);	// blocks while the buffer is full
				if (w < 0) { if (snd_pcm_recover(pcm, (int)w, 1) < 0) { closeDev(); cur = nullptr; break; } continue; }
				frames -= w; p += w * openCh;
			}
		}
		closeDev();
#else
		std::unique_lock<std::mutex> l(m);
		for (;;)
		{
			cv.wait(l, [this] { return stop || closeNow || (!paused && (!queued.empty() || music)); });
			if (stop) break;
			closeNow = false;
			if (musicChanged) musicChanged = false;
			queued.clear();
		}
#endif
	}
}
