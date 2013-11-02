#include "s3mfile.h"

#include <cstring>
#include <cassert>

void S3M::Instrument::load(FILE* fp) {
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

void S3M::Slot::load(uint8_t *&ptr) {
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

void S3M::Slot::print() const {
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


void S3M::Row::load(uint8_t *&ptr) {
	num_slots = 0;
	while(*ptr != 0) {
		slots[num_slots].load(ptr);
		++num_slots;
	}

	//Ignore end-of-row slot
	Slot x;
	x.load(ptr);
}

void S3M::Row::print() const {
	if(num_slots == 0) {
		printf("\n");
		return;
	}

	for(int i=0;i<num_slots;++i) {
		printf("|");
		slots[i].print();
	}
	printf("|\n");
}

void S3M::Pattern::load(FILE* fp) {
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

void S3M::File::load(const char* filename) {
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

	//Panning information

	//Center channel by default
	for(int i=0;i<32;++i) {
		panning[i] = 0.5;
	}

	for(int i=0;i<32;++i) {
		if(header.channel_settings[i]<16) { //channel is enabled
			if(header.channel_settings[i]<8) { //left oriented
				panning[i] = 0.25;
			} else { //right oriented
				panning[i] = 0.75;
			}
		}
	}

	//If we have panning information, use it
	if(header.default_panning == 0xFC) {
		uint8_t pan[32];
		fread(pan, sizeof(uint8_t), 32, fp);
		for(int i=0;i<32;++i) {
			if(pan[i] & 0x20) { //pan specified
				panning[i] = (pan[i] & 0x0F) / 16.0;
			}
		}
	}

	//Track is in mono
	if((header.master_volume & 0x80) == 0) {
		for(int i=0;i<32;++i) {
			panning[i] = 0.5;
		}
	}

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
