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

	//TODO Create 'Player', or 'S3MPlayer' class/struct with most, if not all, of this stuff in it.
	struct {
		uint8_t speed; //Axx //number of ticks in a raw
		uint8_t tempo; //Txx //in BPM
		int sample_rate; //sample rate in Hz (for example: 44100Hz)
		int tick_len; //number of samples (at sample_rate) in a tick
		int tick;
		int tick_offset; //how far in a tick are we?

		int row;
		int pattern;
		int order;
	} state;

	void prepare(int sr) {
		state.sample_rate = sr;
		state.speed = header.initial_speed;
		state.tempo = header.initial_tempo;
		state.tick_len = 2.5 * state.sample_rate / state.tempo;
		state.tick_offset = 0;
		state.tick = state.speed;
		state.row = 0;
		state.order = 0;
		state.pattern = orders[state.order];

		update_row();
	}

	void update_row() {
		auto& row = patterns[state.pattern].rows[state.row];
		printf("O%02uP%02uR%02u ",state.order,state.pattern,state.row);
		row.print();
		//channel_row(channel, row.slots[channelnr]); //adapt channel to new row
	}

	void tick_row() {
		++state.row;
		if(state.row >= 64) {
			state.row = 0;
			++state.order;

			//Find a valid order
			while(orders[state.order] == 254 || orders[state.order] == 255) { //255==end-of-song, 254==marker
				++state.order;
				if(state.order >= header.num_orders) state.order = 0;
			}
			state.pattern = orders[state.order];
		}
		update_row();
	}

	void tick() {
		--state.tick;
		if(state.tick <= 0) {
			state.tick = state.speed;
			tick_row(); //next row
		} else {
			//channel_tick(channel); //tick channel, for effects that work per tick, for example
		}
	}

	void synth(float* buffer, int samples) {
		for(int i=0;i<samples;++i){buffer[i]=0;}

		int offset = 0; //how far into the buffer are we? i.e. how many samples have we generated so far?
		while(samples > 0) { //Still have samples to generate
			int remain = state.tick_len - state.tick_offset;
			if(remain > samples) remain = samples;
			state.tick_offset += remain;
			if(state.tick_offset == state.tick_len) {
				tick();
				state.tick_offset = 0;
			}
			offset += remain;
			samples -= remain;
			//resample(...)
		}
	}
};

static S3MFile s3m;

void audio_callback(void*, Uint8*, int);
int main(int argc, char* argv[]);
void play_audio();

void audio_callback(void*, Uint8* s, int len) {
	float* stream = (float*)s;
	len = len/sizeof(float);

	s3m.synth(stream, len);

	/*
	static float phase;
	for(int i =0; i < len; ++i) {
		phase += 2.0f * M_PI * (440.0f/44100.0f);
		phase = fmod(phase, 2*M_PI);
		stream[i] = sinf(phase);
	}
	*/
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
	printf("done\n");
	SDL_CloseAudioDevice(dev);
}

int main(int argc, char* argv[]) {
	SDL_Init(SDL_INIT_AUDIO);
	s3m.load("../SATELL.S3M");
	//s3m.load("../aryx.s3m");
	printf("playing\n");
	s3m.prepare(44100);
	play_audio();
	return 0;
}
