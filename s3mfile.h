#pragma once

#include <cstdint>
#include <cstdio>

namespace S3M {

	struct Instrument {
		struct {
			uint8_t type;
			char filename[12];
			struct {
				uint8_t memseg[3];

				inline uint32_t ptr() {
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

		void load(FILE* fp);
	};

	struct Slot {
		uint8_t channel;
		uint8_t note;
		uint8_t instrument;
		uint8_t volume;
		uint8_t command;
		uint8_t infobyte;

		void load(uint8_t *&ptr);
		void print() const;

		inline int base_note() const {
			return (note>>4)*12 + (note&0x0F);
		}

	};

	struct Row {
		struct Slot slots[32]; //maximum 32 channels, so a maximum of 32 slots per row

		int num_slots;

		void load(uint8_t *&ptr);
		void print() const;

		inline Slot* begin() {
			return slots;
		}

		inline const Slot* begin() const {
			return slots;
		}

		inline Slot* end() {
			return slots + num_slots;
		}

		inline const Slot* end() const {
			return slots + num_slots;
		}
	};

	struct Pattern {
		struct Row rows[64]; //there are always 64 rows in a pattern

		void load(FILE* fp);
	};

	struct File {
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
		struct Instrument instruments[99];
		struct Pattern patterns[100];
		float panning[32];

		void load(const char* filename);
	};
}
