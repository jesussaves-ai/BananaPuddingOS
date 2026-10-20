#pragma once
// banana-ui: the theme's menu sounds (rnds/sounds/*.wav) and its background music, through ALSA. One worker thread
// opens the device on the first sound, mixes a looping music track under the one-shot sounds (about 7 dB below them),
// keeps the stream running through the silences between sounds (so the RG DS Plus's amplifiers stay started)
// and closes it after 20 quiet seconds, and at once when a game starts (release()), so nothing holds the audio device
// while a game wants it. Nothing here touches the frames' thread: play()/setMusicEnabled() only hand the worker a
// name and wake it, so menu sounds and music cost the emulator and the renderer nothing but a lock and a notify.
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace banana
{
	class Sound
	{
	public:
		explicit Sound(const std::string& dir);
		~Sound();
		void play(const std::string& name);
		// the menu's background music: a track looped under everything while on. Resolved from the theme's own
		// sounds.music, else <dir>/music.wav; a track that isn't there is simply none. Loading happens here, on the
		// caller's thread (the worker only mixes what it is handed).
		void setMusicEnabled(bool on);
		void release();		// close the device now (a game is starting)
		void resume();		// a game ended: the music (if any) plays again
		bool enabled = true;	// menu sounds on (Settings); music has its own switch

	private:
		struct Clip { std::vector<int16_t> pcm; int rate = 44100, channels = 2; };
		std::string dir;
		std::map<std::string, Clip> clips;
		std::shared_ptr<const Clip> music;	// the loop to play, or none
		std::string musicPath;				// what music was loaded from (unchanged: don't restart the loop)
		std::thread th;
		std::mutex m;
		std::condition_variable cv;
		std::string queued;					// the next one-shot sound
		bool musicChanged = false;
		bool paused = false;				// a game has the device; don't reopen for the music until resume()
		bool stop = false, closeNow = false;
		void run();
	};
}
