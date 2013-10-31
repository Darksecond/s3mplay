#include <SDL.h>
#include <SDL_audio.h>
#include <cmath>
#include <stdio.h>
#include <cassert>

struct S3MFile {
	struct {
		char name[28];
		uint8_t eofchar;
		uint8_t type;
		uint8_t dummy[2];
		uint16_t num_orders;
		uint16_t num_instruments;
		uint16_t num_patterns;
		uint16_t flags;
		uint16_t version;
		uint16_t ffi; //signed/unsigned samples
		char scrm[4];
		uint8_t global_volume; //Vxx
		uint8_t initial_speed; //Axx
		uint8_t initial_tempo; //Txx
		uint8_t master_volume;
		uint8_t uc; //ultraclick removal
		uint8_t default_panning;
		uint8_t dummy2[8];
		uint16_t special;
		uint8_t channel_settings[32];
	} __attribute__((packed)) header;

	uint8_t orders[256];

	struct Instrument {
		struct {
			uint8_t type;
			char filename[12];
			struct {
				uint8_t memseg[3];

				uint32_t ptr() {
					return (((uint32_t)memseg[0] << 16) + ((uint32_t)memseg[2] << 8) + (uint32_t)memseg[1]) * 16UL;
				}
			} memseg;
			uint32_t length;
			uint32_t loop_begin;
			uint32_t loop_end;
			uint8_t volume;
			uint8_t dummy;
			uint8_t pack_scheme;
			uint8_t flags;
			uint32_t c4spd;
			uint8_t dummy2[12];
			char sample_name[28];
			uint8_t scrs[4];
		} __attribute__((packed)) header;

		uint8_t *sample_data;

		Instrument() : sample_data(nullptr) {
		}

		~Instrument() {
			if(sample_data)
				delete [] sample_data;
		}

		void load(FILE* fp) {
			fread(&header, sizeof(header), 1, fp);
			
			//Verify header
			assert(memcmp(header.scrs,"SCRS",4)==0);

			//Fix length if needed
			if(header.length > 64000UL) header.length = 64000UL;

			//If the sample loops, check loop values
			if(header.flags & 1) { //1 == Sample loops
				assert(header.loop_begin < header.length);
				assert(header.loop_end <= header.length);
			}

			//Only PCM samples are supported
			if(header.type == 1) {
				//Load PCM sample data
				fseek(fp, header.memseg.ptr(), SEEK_SET);
				sample_data = new uint8_t[header.length];
				fread(sample_data, 1, header.length, fp);
			}
		}
	} instruments[99];

	struct Pattern {
		struct Row {
			struct Slot {
				uint8_t channel;
				uint8_t note;
				uint8_t instrument;
				uint8_t volume;
				uint8_t command;
				uint8_t infobyte;

				void load(uint8_t *&ptr) {
					uint8_t byte = *ptr++;
					channel = byte & 0x1F;
					note = 255;
					instrument = 0;
					volume = 255;
					command = 0;
					infobyte = 0;
					if(byte & 0x20) {
						note = *ptr++;
						instrument = *ptr++;
					}
					if(byte & 0x40) {
						volume = *ptr++;
					}
					if(byte & 0x80) {
						command = *ptr++;
						infobyte = *ptr++;
					}
				}

				void print() {
					static const char notenames[] = "C-C#D-D#E-F-F#G-G#A-A#B-12131415";

					printf("c%02u ",channel);

					if(note == 255)
						printf("...");
					else if(note == 254)
						printf("^^^");
					else
						printf("%.2s%u", notenames+2*(note&15), note>>4);

					if(instrument)
						printf(" %02u", instrument);
					else
						printf(" ..");

					if(volume == 255)
						printf(" ..");
					else
						printf(" %02u", volume);

					if(command)
						printf(" %c", command+64);
					else
						printf(" .");

					printf("%02X", infobyte);
				}

				int base_note() {
					return (note>>4)*12 + (note&0x0F);
				}

			} slots[32]; //maximum 32 channels, so a maximum of 32 slots per row

			int num_slots;

			void load(uint8_t *&ptr) {
				num_slots = 0;
				while(*ptr != 0) {
					slots[num_slots].load(ptr);
					++num_slots;
				}

				//Ignore end-of-row slot
				Slot x;
				x.load(ptr);
			}

			void print() {
				if(num_slots == 0) return;

				for(int i=0;i<num_slots;++i) {
					printf("|");
					slots[i].print();
				}
				printf("|\n");
			}

			Slot* begin() {
				return slots;
			}

			Slot* end() {
				return slots + num_slots;
			}
		} rows[64]; //there are always 64 rows in a pattern

		void load(FILE* fp) {
			uint16_t length;
			fread(&length, sizeof(uint16_t), 1, fp);
			uint8_t *data = new uint8_t[length];
			fread(data, 1, length, fp);
			uint8_t *ptr = data;
			for(int i=0;i<64;++i) {
				rows[i].load(ptr);
			}
			delete [] data;
		}

		void print() {
			for(int i=0;i<64;++i) {
				rows[i].print();
			}
		}
	} patterns[100];
	
	void load(const char* filename) {
		FILE* fp = fopen(filename, "rb");
		assert(fp);
		setbuf(fp, NULL);
		fread(&header, sizeof(header), 1, fp);

		//Verify header
		assert(header.eofchar == 0x1A);
		assert(header.type == 16);
		assert(memcmp(header.scrm,"SCRM",4)==0);
		assert(header.num_orders <= 256);
		assert(header.num_instruments <= 99);
		assert(header.num_patterns <= 100);

		//Load orders
		memset(orders, 255, sizeof(orders));
		fread(orders, 1, header.num_orders, fp);

		//Load instrument & pattern pointers
		uint16_t ins_ptrs[99];
		fread(ins_ptrs, sizeof(uint16_t), header.num_instruments, fp);
		uint16_t pat_ptrs[100];
		fread(pat_ptrs, sizeof(uint16_t), header.num_patterns, fp);

		//Load instruments
		for(int i=0;i<header.num_instruments; ++i) {
			auto& inst = instruments[i];
			fseek(fp, ins_ptrs[i]*16UL, SEEK_SET);
			inst.load(fp);
		}

		//Load patterns
		for(int i=0;i<header.num_patterns; ++i) {
			if(pat_ptrs[i]) {
				auto& pat = patterns[i];
				fseek(fp, pat_ptrs[i]*16UL, SEEK_SET);
				pat.load(fp);
			}
		}

		fclose(fp);
	}
};

struct S3MPlayer {
	S3MFile* s3m;
	int sample_rate; //Sample rate in Hz
	int tick_length; //length of a tick in samples
	int tick_offset; //offset into the current row, in samples
	int tempo; //tempo in BPM
	int speed; //amount of ticks in a row
	int current_tick; //amount of ticks into a row (offset);
	int row; //current row
	int order; //current order
	int pattern; //current pattern

	struct Channel {
		bool active;
		int instrument;
		int base_note;
		double period;
		double sample_offset;

		S3MFile::Pattern::Row::Slot current_slot;
		int current_instrument;

		Channel() : active(false), instrument(0), base_note(-1), period(0), sample_offset(0) {
		}
	} channels[32];

	S3MPlayer() : s3m(nullptr), sample_rate(0) {
	}

	void set_sample_rate(int sr) {
		sample_rate = sr;
	}

	void load(S3MFile* file) {
		s3m = file;
		reset();
	}

	void reset() {
		assert(s3m);
		assert(sample_rate);

		tempo = s3m->header.initial_tempo;
		speed = s3m->header.initial_speed;

		tick_offset = 0;
		current_tick = 0;
		row = 0;
		order = 0;

		tick_length = 2.5 * sample_rate / tempo;
		pattern = s3m->orders[order];
	}

	S3MFile::Pattern::Row& current_row() {
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

	void update_row() {
		printf("O%02uP%02uR%02u ",order,pattern,row);
		current_row().print();
		for(S3MFile::Pattern::Row::Slot& slot : current_row()) {
			Channel& channel = channels[slot.channel];

			if((slot.note != 255 && slot.note != 254) || slot.instrument != 0) {
				if(slot.note != 255 && slot.note != 254) {
					channel.base_note = slot.base_note();
					channel.active = true;
					channel.sample_offset = 0;
				}
				if(slot.instrument) {
					channel.instrument = slot.instrument-1;
				}

				const int c4spd = s3m->instruments[channel.current_instrument].header.c4spd;
				channel.period = 8362*16*1712/pow(2,(channel.base_note/12.0)) / c4spd;
			}
		}
	}

	void tick_row() {
		update_row();
		
		++row;
		if(row >= 64) {
			row = 0;
			++order;

			//Find a valid order
			while(s3m->orders[order] == 254 || s3m->orders[order] == 255) { //255==end-of-song, 254==marker
				++order;
				if(order >= s3m->header.num_orders) order = 0;
			}
			pattern = s3m->orders[order];
		}
	}

	void tick() {
		--current_tick;
		if(current_tick <= 0) {
			current_tick = speed;
			tick_row(); //next row
		} else {
			//channel_tick(channel); //tick channel, for effects that work per tick, for example
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
						sound += resample(channels[c])   /2; //devide by 2 is a HACK, HACK!
					}
				}
				buffer[offset+i] = sound;
			}
			offset += remain;
			samples -= remain;
		}
	}
};

static S3MFile s3m;
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
	SDL_Delay(120000);
	SDL_CloseAudioDevice(dev);
}

int main(int argc, char* argv[]) {
	SDL_Init(SDL_INIT_AUDIO);
	player.set_sample_rate(44100);
	s3m.load("../SATELL.S3M");
	//s3m.load("../aryx.s3m");
	player.load(&s3m);
	printf("playing\n");
	play_audio();
	printf("done\n");
	return 0;
}
