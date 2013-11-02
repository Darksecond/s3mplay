#pragma once

#include "s3mfile.h"

#include <cmath>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <cstring>

namespace S3M {

	class Cursor {
		static constexpr int invalid = -1;
		int row;
		int order;
		int pattern;
	public:

		//TODO Proper end-of-song detection
		bool set_order(const int new_order, const File *s3m);

		inline bool next_order(const Cursor& cursor, const File *s3m) {
			return set_order(cursor.order+1,s3m);
		}

		inline void set_row(const int new_row) {
			assert(new_row < 64 && new_row >= 0);
			row = new_row;
		}

		inline void reset(const File *s3m) {
			set_row(0);
			set_order(0, s3m);
		}

		inline Row& current_row(File *s3m) {
			assert(row != invalid && order != invalid && pattern != invalid);
			return s3m->patterns[pattern].rows[row];
		}

		inline const Row& current_row(const File *s3m) {
			assert(row != invalid && order != invalid && pattern != invalid);
			return s3m->patterns[pattern].rows[row];
		}

		inline void print() const {
			printf("O%02uP%02uR%02u ",order,pattern,row);
		}

		bool apply(const Cursor& other);

		inline void invalidate() {
			row = invalid;
			order = invalid;
			pattern = invalid;
		}

		//TODO Proper end-of-song detection
		bool next_row(const File *s3m);

		inline bool order_invalid() const {
			return order == invalid;
		}
	};

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

		float pan; //0.0 == left, 1.0 == right

		Channel() : active(false), instrument(0), base_note(-1), period(0), slide_period(0), sample_offset(0), volume(64), note_on(0), note_off(999)
			    , volume_slide(0), last_volume_slide(0), portamento(0), last_portamento(0), new_sample_offset(0), pan(0.5) {
			    }

		void apply_volume_slide();
		void apply_portamento();
		double sample(const File *s3m, const int sample_rate);
		
		inline void mono(const File *s3m, const int sample_rate, double& middle) {
			middle += sample(s3m,sample_rate);
		}

		inline void stereo(const File *s3m, const int sample_rate, double& left, double& right) {
			double s = sample(s3m,sample_rate);
			left += s*(1.0-pan);
			right += s*pan;
		}
	};

	class Player {
		const File* s3m; //s3m file to play
		std::atomic<int> finished; //how many times has player looped through the song? i.e. 0==not done playing yet, >0==done playing, at least once
		int sample_rate; //Sample rate in Hz
		int tick_length; //length of a tick in samples
		int tick_offset; //offset into the current row, in samples
		int tempo; //tempo in BPM
		int speed; //amount of ticks in a row
		int global_volume; //Vxx
		int current_tick; //amount of ticks into a row (offset);
		Cursor cursor; //current order,pattern,row
		Cursor jump_cursor; //order,pattern,row to jump to

		struct Channel channels[32];

		inline void set_tempo(const int new_tempo) {
			tempo = new_tempo;
			tick_length = 2.5 * sample_rate / tempo;
		}

		inline void note_on(const Slot& slot) {
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


		inline void update_row() {
			cursor.print();
			const Row& crow = cursor.current_row(s3m);
			crow.print();
			for(const Slot& slot : crow) {
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
						jump_cursor.set_row(0);
						jump_cursor.set_order(slot.infobyte, s3m);
						break;
					case 'C': //Cxx, Pattern break
						if(jump_cursor.order_invalid()) {
							jump_cursor.next_order(cursor, s3m);
						}
						jump_cursor.set_row((slot.infobyte >> 4)*10 + (slot.infobyte & 15));
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
							const int x = (slot.infobyte & 0xF0)>>4;
							const int y = slot.infobyte & 0x0F;
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
							case '0x80': //S8x, Pan position
								channel.pan = (slot.infobyte & 0x0F) / 16.0;
								break;
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
				if((s3m->header.version == 0x1300) || (s3m->header.flags & 0x40)) channel.apply_volume_slide();
			}
		}

		//TODO end-of-song detection in pattern jump
		inline void tick_row() {
			update_row();

			if(cursor.apply(jump_cursor)) {
				jump_cursor.invalidate();
			} else {
				if(cursor.next_row(s3m))
					++finished;
			}
		}

		inline void channel_tick() {
			for(const Slot& slot : cursor.current_row(s3m)) {
				Channel& channel = channels[slot.channel];
				if(channel.note_on == current_tick) note_on(slot);
				if(channel.note_off == current_tick) channel.active = false;

				channel.apply_volume_slide();
				channel.apply_portamento();
			}
		}

		inline void tick() {
			++current_tick;
			if(current_tick >= speed) {
				current_tick = 0;
				tick_row(); //next row
			} else {
				channel_tick();
			}
		}
	public:
		Player() : s3m(nullptr), sample_rate(0) {
		}

		inline void set_sample_rate(const int sr) {
			sample_rate = sr;
		}

		inline void load(const File* file) {
			s3m = file;
			reset();
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

			jump_cursor.invalidate();
			cursor.reset(s3m);

			for(int i=0;i<32;++i) {
				channels[i].pan = s3m->panning[i];
			}
		}

		void synth_mono(float* buffer, int samples) {
			memset(buffer, 0, samples*sizeof(float));

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
							channels[c].mono(s3m,sample_rate,sound);
						}
					}
					sound *= (s3m->header.master_volume & 127) * global_volume / (512.0*32.0); //32 channels, 512 == 2^7*2^8/64
					buffer[offset+i] = sound;
				}
				offset += remain;
				samples -= remain;
			}
		}

		void synth_stereo(float* buffer, int samples) {
			//Samples is the total amount of samples, so with 2 channels, 20 would be 10 samples per channel (interlaced)
			memset(buffer, 0, samples*sizeof(float));
			samples /= 2;

			int offset = 0;
			while(samples > 0) {
				int remain = tick_length - tick_offset;
				if(remain > samples) remain = samples;
				tick_offset += remain;
				if(tick_offset == tick_length) {
					tick();
					tick_offset = 0;
				}
				for(int i=0;i<remain*2;i+=2) {
					double soundL = 0;
					double soundR = 0;
					for(int c=0;c<32;++c) {
						if(channels[c].active) {
							channels[c].stereo(s3m,sample_rate,soundL,soundR);
						}
					}
					soundL *= (s3m->header.master_volume & 127) * global_volume / (512.0*32.0); //32 channels, 512 == 2^7*2^8/64
					soundR *= (s3m->header.master_volume & 127) * global_volume / (512.0*32.0); //32 channels, 512 == 2^7*2^8/64
					buffer[offset*2+i] = soundL;
					buffer[offset*2+i+1] = soundR;
				}
				offset += remain;
				samples -= remain;
			}
		}

		void print() const {
			printf("Song: %s\n",s3m->header.name);

			/*
			printf("Orders: |");
			for(int i=0;i<s3m->header.num_orders;++i) {
				printf("%3i|",i);
			}
			printf("\n");
			printf("        |");
			for(int i=0;i<s3m->header.num_orders;++i) {
				printf("%3i|",s3m->orders[i]);
			}
			printf("\n");
			*/

			printf("Pans: |");
			for(int i=0;i<32;++i) {
				printf("%.02f|",s3m->panning[i]);
			}
			printf("\n");

			printf("Master volume: %i",s3m->header.master_volume);

			printf("\n");
		}

		bool is_finished() const {
			return finished;
		}
	};
}
