#include "s3mplayer.h"

//TODO Proper end-of-song detection
bool S3M::Cursor::set_order(int new_order, S3M::File *s3m) {
	bool done = false;
	//if(new_order <= order) done = true; //is an option
	order = new_order;
	while(s3m->orders[order] == 254 || s3m->orders[order] == 255 || order >= s3m->header.num_orders) {
		if(s3m->orders[order] == 255) done = true; //another option
		++order;
		if(order >= s3m->header.num_orders) {
			order = 0;
			done = true; //another option
		}
	}
	pattern = s3m->orders[order];
	return done;
}

bool S3M::Cursor::apply(const Cursor& other) {
	bool success = false;
	if(other.row != invalid) {
		row = other.row;
		success = true;
	}
	if(other.order != invalid && other.pattern != invalid) {
		order = other.order;
		pattern = other.pattern;
		success = true;
	}
	return success;
}

//TODO Proper end-of-song detection
bool S3M::Cursor::next_row(S3M::File *s3m) {
	bool done = false;
	++row;
	if(row >= 64) {
		row = 0;
		if(set_order(order+1,s3m))
			done = true;
	}
	return done;
}

void S3M::Channel::apply_volume_slide() {
	volume += volume_slide;
	if(volume < 0)
		volume = 0;
	if(volume > 64)
		volume = 64;
}

void S3M::Channel::apply_portamento() {
	if(portamento) {
		if(period < slide_period) {
			period += portamento;
			if(period > slide_period)
				period = slide_period;
		} else if(period > slide_period) {
			period -= portamento;
			if(period < slide_period)
				period = slide_period;
		}
	}
}
