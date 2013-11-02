#include "s3mfile.h"
#include "s3mplayer.h"

#include <SDL.h>
#include <SDL_audio.h>
#include <cstdio>
#include <cassert>

class SDLOut {
	S3M::Player *_player;

	static void callback(void* void_self, Uint8* s, int len) {
		SDLOut* self = (SDLOut*)void_self;
		self->_player->synth_stereo((float*)s, len/sizeof(float));
	}
public:
	void load(S3M::Player *player) {
		_player = player;
		_player->set_sample_rate(44100);
	}

	void play() {
		assert(_player);

		SDL_AudioSpec want, have;
		SDL_AudioDeviceID dev;
		SDL_zero(want);
		want.freq = 44100;
		want.format = AUDIO_F32;
		want.channels = 2;
		want.samples = 2048;
		want.callback = callback;
		want.userdata = this;
		dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
		assert(dev);

		SDL_PauseAudioDevice(dev, 0);
		while(!_player->is_finished())
			SDL_Delay(1);

		SDL_CloseAudioDevice(dev);
	}
};

int main(int argc, char* argv[]) {
	if(argc == 1) {
		printf("Please specify an s3m file to play\n");
		return -1;
	};

	S3M::File s3m;
	S3M::Player player;
	SDLOut out;
	out.load(&player);

	SDL_Init(SDL_INIT_AUDIO);

	s3m.load(argv[1]);
	//s3m.load("../SATELL.S3M");
	//s3m.load("../aryx.s3m");
	//s3m.load("../2ND_PM.S3M");
	//s3m.load("../pod.s3m");
	//s3m.load("../CTGOBLIN.S3M");
	//s3m.load("../ascent.s3m");
	//s3m.load("../MECH8.S3M");
	//s3m.load("../A94FINAL.S3M");
	//s3m.load("../CLICK.S3M");
	//s3m.load("../2nd_reality.s3m");
	//s3m.load("../backwards-sdrawkcab.s3m");

	player.load(&s3m);
	player.print();

	out.play();

	return 0;
}
