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
		bool set_order(int new_order, File *s3m);

		inline bool next_order(File *s3m) {
			return set_order(order+1,s3m);
		}

		inline void set_row(int new_row) {
			assert(new_row < 64 && new_row >= 0);
			row = new_row;
		}

		inline void reset(File *s3m) {
			set_row(0);
			set_order(0, s3m);
		}

		inline Row& current_row(File *s3m) {
			assert(row != invalid && order != invalid && pattern != invalid);
			return s3m->patterns[pattern].rows[row];
		}

		inline void print() {
			printf("O%02uP%02uR%02u ",order,pattern,row);
		}

		bool apply(const Cursor& other);

		inline void invalidate() {
			row = invalid;
			order = invalid;
			pattern = invalid;
		}

		//TODO Proper end-of-song detection
		bool next_row(File *s3m);

		inline bool order_invalid() {
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

		Channel() : active(false), instrument(0), base_note(-1), period(0), slide_period(0), sample_offset(0), volume(64), note_on(0), note_off(999)
			    , volume_slide(0), last_volume_slide(0), portamento(0), last_portamento(0), new_sample_offset(0) {
			    }

		void apply_volume_slide();
		void apply_portamento();
	};

	class Player {
		File* s3m; //s3m file to play
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
	public:

		Player() : s3m(nullptr), sample_rate(0) {
		}

		void set_sample_rate(int sr) {
			sample_rate = sr;
		}

		void load(File* file) {
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

			jump_cursor.invalidate();
			cursor.reset(s3m);
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

		void note_on(Slot& slot) {
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


		void update_row() {
			cursor.print();
			Row& crow = cursor.current_row(s3m);
			crow.print();
			for(Slot& slot : crow) {
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
							jump_cursor.next_order(s3m);
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
				if((s3m->header.version == 0x1300) || (s3m->header.flags & 0x40)) channel.apply_volume_slide();
			}
		}

		//TODO end-of-song detection in pattern jump
		void tick_row() {
			update_row();

			if(cursor.apply(jump_cursor)) {
				jump_cursor.invalidate();
			} else {
				if(cursor.next_row(s3m))
					++finished;
			}
		}

		void channel_tick() {
			for(Slot& slot : cursor.current_row(s3m)) {
				Channel& channel = channels[slot.channel];
				if(channel.note_on == current_tick) note_on(slot);
				if(channel.note_off == current_tick) channel.active = false;

				channel.apply_volume_slide();
				channel.apply_portamento();
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

		bool is_finished() {
			return finished;
		}
	};
}
