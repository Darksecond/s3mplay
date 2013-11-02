#include "s3mfile.h"

#include <SDL.h>
#include <SDL_audio.h>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <atomic>

struct S3MPlayer {
	S3M::File* s3m; //s3m file to play
	std::atomic<int> finished; //how many times has player looped through the song? i.e. 0==not done playing yet, >0==done playing, at least once
	int sample_rate; //Sample rate in Hz
	int tick_length; //length of a tick in samples
	int tick_offset; //offset into the current row, in samples
	int tempo; //tempo in BPM
	int speed; //amount of ticks in a row
	int global_volume; //Vxx
	int current_tick; //amount of ticks into a row (offset);
	int row; //current row
	int order; //current order
	int pattern; //current pattern

	int pattern_jump; //part of Bxx command

	struct Channel {
		bool active;
		int instrument;
		int base_note;
		double period;
		double slide_period; //period to slide to
		double sample_offset;
		int volume;
		int note_on;
		int note_off;

		int volume_slide;
		int last_volume_slide;

		int portamento;
		int last_portamento;

		int new_sample_offset;

		Channel() : active(false), instrument(0), base_note(-1), period(0), slide_period(0), sample_offset(0), volume(64), note_on(0), note_off(999)
			    , volume_slide(0), last_volume_slide(0), portamento(0), last_portamento(0), new_sample_offset(0) {
		}
	} channels[32];

	S3MPlayer() : s3m(nullptr), sample_rate(0) {
	}

	void set_sample_rate(int sr) {
		sample_rate = sr;
	}

	void load(S3M::File* file) {
		s3m = file;
		reset();
	}

	void set_tempo(int new_tempo) {
		tempo = new_tempo;
		tick_length = 2.5 * sample_rate / tempo;
	}

	void reset() {
		assert(s3m);
		assert(sample_rate);

		set_tempo(s3m->header.initial_tempo);
		speed = s3m->header.initial_speed;
		global_volume = s3m->header.global_volume;

		finished = 0;
		tick_offset = 0;
		current_tick = speed;
		row = 0;
		order = 0;
		pattern_jump = 0;

		pattern = s3m->orders[order];
	}

	S3M::Row& current_row() {
		return s3m->patterns[pattern].rows[row];
	}

	double resample(Channel& channel) {
		double sampler_add = (14317056.0 / sample_rate) / channel.period;
		auto& ins = s3m->instruments[channel.instrument];

		if(channel.sample_offset >= ins.header.length) {
			channel.active = false;
			return 0;
		}

		double sample = ins.sample_data[(unsigned)channel.sample_offset] - 128.0; 
		channel.sample_offset += sampler_add;
		if((ins.header.flags & 1) && channel.sample_offset >= ins.header.loop_end) /* loop? */
			channel.sample_offset = ins.header.loop_begin + fmod(channel.sample_offset - ins.header.loop_begin, ins.header.loop_end - ins.header.loop_begin);

		return sample/128.0;
	}

	void note_on(S3M::Slot& slot) {
		Channel& channel = channels[slot.channel];
		if((slot.note != 255 && slot.note != 254) || slot.instrument != 0) {
			if(slot.note != 255 && slot.note != 254) {
				channel.base_note = slot.base_note();
				channel.active = true;
				channel.sample_offset = channel.new_sample_offset;
			}
			if(slot.instrument) {
				channel.instrument = slot.instrument-1;
			}
			if(slot.volume == 255 && slot.instrument)
				channel.volume = s3m->instruments[channel.instrument].header.volume;

			const int c4spd = s3m->instruments[channel.instrument].header.c4spd;
			channel.slide_period = 8362*16*1712/pow(2,(channel.base_note/12.0)) / c4spd;
		}
		if(slot.volume != 255) {
			channel.volume = slot.volume;
		}

		if(!channel.portamento)
			channel.period = channel.slide_period;
	}

	void volume_slide(Channel& channel) {
		//volume slide
		channel.volume += channel.volume_slide;
		if(channel.volume < 0)
			channel.volume = 0;
		if(channel.volume > 64)
			channel.volume = 64;
	}

	void portamento(Channel& channel) {
		//portamento
		if(channel.portamento) {
			if(channel.period < channel.slide_period) {
				channel.period += channel.portamento;
				if(channel.period > channel.slide_period)
					channel.period = channel.slide_period;
			} else if(channel.period > channel.slide_period) {
				channel.period -= channel.portamento;
				if(channel.period < channel.slide_period)
					channel.period = channel.slide_period;
			}
		}
	}

	void update_row() {
		printf("O%02uP%02uR%02u ",order,pattern,row);
		current_row().print();
		//We cache the current row, because row might change in this for loop. This way we don't suddenly switch over to another row.
		S3M::Row& crow = current_row();
		for(S3M::Slot& slot : crow) {
			Channel& channel = channels[slot.channel];
			channel.note_on = 0;
			channel.note_off = 999;
			channel.volume_slide = 0;
			channel.portamento = 0;
			channel.new_sample_offset = 0;

			switch(slot.command+64) {
				case 'A': //Axx, Set speed
					speed = slot.infobyte;
					break;
				case 'B': //Bxx, Pattern Jump
					pattern_jump = slot.infobyte;
					//We can change row here because we've cached the current row in row.
					row = 0;
					break;
				case 'C': //Cxx, Pattern break
					if(!pattern_jump) {
						pattern_jump = order + 1;
					}
					row = (slot.infobyte >> 4)*10 + (slot.infobyte & 15);
					break;
				case 'T': //Txx, Set tempo
					set_tempo(slot.infobyte);
					break;
				case 'V': //Vxx, Set global volume
					global_volume = slot.infobyte;
					break;
				case 'O': //Oxx, Set sample offset
					channel.new_sample_offset = slot.infobyte * 0x100;
					break;
				case 'G': //Gxx, Tone portamento
					if(slot.infobyte) {
						channel.last_portamento = slot.infobyte * 4;
					}
					channel.portamento = channel.last_portamento;
					break;
				case 'D': //Dxy, Volume slide
					if(slot.infobyte) { //xy != 0x00
						int x = (slot.infobyte & 0xF0)>>4;
						int y = slot.infobyte & 0x0F;
						if(x>0)
							channel.last_volume_slide = x;
						else if(y>0)
							channel.last_volume_slide = -y;
					}
					channel.volume_slide = channel.last_volume_slide;
					break;
				case 'E': //Exy, Portamento down
					//TODO
					break;
				case 'F': //Fxy, Portamento up
					//TODO
					break;
				case 'S': //Special
					switch(slot.infobyte & 0xF0)
					{
						case '0xC0': //Notecut
							channel.note_off = slot.infobyte & 0x0F;
							break;
						case '0xD0': //Notedelay
							channel.note_on = slot.infobyte & 0x0F;
							break;
						case '0xE0': //Patterndelay
							current_tick = -slot.infobyte * speed;
							break;
					}
					break;
			}

			if(channel.note_on == current_tick) note_on(slot);
			if((s3m->header.version == 0x1300) || (s3m->header.flags & 0x40)) volume_slide(channel);
		}
	}

	//TODO Proper end-of-song detection
	bool set_order(int new_order) {
		bool done = false;
		//if(new_order <= order) done = true; //is an option
		order = new_order;
		while(s3m->orders[order] == 254 || s3m->orders[order] == 255 || order >= s3m->header.num_orders) {
			if(s3m->orders[order] == 255) done = true;
			++order;
			if(order >= s3m->header.num_orders) {
				order = 0;
				done = true;
			}
		}
		pattern = s3m->orders[order];
		return done;
	}

	//TODO end-of-song detection in pattern jump
	void tick_row() {
		update_row();
		
		if(pattern_jump) {
			if(set_order(pattern_jump)) {
				set_order(0);
			}
			pattern_jump = 0;
		} else {
			++row;
			if(row >= 64) {
				row = 0;
				if(set_order(order+1))
					++finished;
			}
		}
	}

	void channel_tick() {
		for(S3M::Slot& slot : current_row()) {
			Channel& channel = channels[slot.channel];
			if(channel.note_on == current_tick) note_on(slot);
			if(channel.note_off == current_tick) channel.active = false;

			volume_slide(channel);
			portamento(channel);
		}
	}

	void tick() {
		++current_tick;
		if(current_tick >= speed) {
			current_tick = 0;
			tick_row(); //next row
		} else {
			channel_tick();
		}
	}

	void synth(float* buffer, int samples) {
		for(int i=0;i<samples;++i){buffer[i]=0;}

		int offset = 0;
		while(samples > 0) {
			int remain = tick_length - tick_offset;
			if(remain > samples) remain = samples;
			tick_offset += remain;
			if(tick_offset == tick_length) {
				tick();
				tick_offset = 0;
			}
			for(int i=0;i<remain;++i) {
				double sound = 0;
				for(int c=0;c<32;++c) {
					if(channels[c].active) {
						sound += channels[c].volume * resample(channels[c]);
					}
				}
				sound *= (s3m->header.master_volume & 127) * global_volume / (64.0*512.0*32.0); //32 channels, 512 == 2^7*2^8/64
				buffer[offset+i] = sound;
			}
			offset += remain;
			samples -= remain;
		}
	}

	void print() {
		printf("Song: %s\n",s3m->header.name);
		printf("\n");
	}
};

static S3M::File s3m;
static S3MPlayer player;

void audio_callback(void*, Uint8* s, int len) {
	float* stream = (float*)s;
	len = len/sizeof(float);
	player.synth(stream, len);
}

void play_audio() {
	SDL_AudioSpec want, have;
	SDL_AudioDeviceID dev;
	SDL_zero(want);
	want.freq = 44100;
	want.format = AUDIO_F32;
	want.channels = 1;
	want.samples = 2048;
	want.callback = audio_callback;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
	assert(dev);

	SDL_PauseAudioDevice(dev, 0);
	while(!player.finished)
		SDL_Delay(1);
	SDL_CloseAudioDevice(dev);
}

int main(int argc, char* argv[]) {
	SDL_Init(SDL_INIT_AUDIO);
	player.set_sample_rate(44100);

	//s3m.load("../SATELL.S3M");
	//s3m.load("../aryx.s3m");
	s3m.load("../2ND_PM.S3M");
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
	play_audio();
	return 0;
}
