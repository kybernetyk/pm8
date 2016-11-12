/*
 * enumerate and dump Griffin Power Mate data
 * C++, works on OS X and Linux. Requires HIDAPI (https://github.com/signal11/hidapi) to work.
 * License: Affero GPL3
 *
 * I totally have to write a Swift wrapper for HIDAPI...
 */
#include <hidapi.h>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <map>
#include <string>

//shamelessly stolen from the HIDAPI doc page :3
void enumerate_devices() {
	auto devs = hid_enumerate(0x0, 0x0);
	auto cur_dev = devs;  
	while (cur_dev) {
		printf("Device Found\n  type: %04hx %04hx\n  path: %s\n  serial_number: %ls",
				cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
		printf("\n");
		printf("  Manufacturer: %ls\n", cur_dev->manufacturer_string);
		printf("  Product:      %ls\n", cur_dev->product_string);
		printf("\n");
		cur_dev = cur_dev->next;
	}
	hid_free_enumeration(devs);
}

void dump_features(hid_device *handle) {
	unsigned char buf[65];
	// Send a Feature Report to the device
	buf[0] = 0x2; // First byte is report number
	buf[1] = 0xa0;
	buf[2] = 0x0a;
	auto  res = hid_send_feature_report(handle, buf, 17);

	buf[0] = 0x2;
	res = hid_get_feature_report(handle, buf, sizeof(buf));
	printf("Feature Report\n   ");
	for (int i = 0; i < res; i++) {
		printf("%02hhx ", buf[i]);
	}
	printf("\n");


	buf[0] = 1; // First byte is report number
	buf[1] = 0x80;
	res = hid_write(handle, buf, 65);

	// Send an Output report to request the state (cmd 0x81)
	buf[1] = 0x81;
	hid_write(handle, buf, 65);

	// Read requested state
	res = hid_read(handle, buf, 65);
	if (res < 0) {
		printf("Unable to read()\n");
		return;
	}

	// Print out the returned buffer.
	for (int i = 0; i < res; i++) {
		printf("buf[%d]: %d\n", i, buf[i]);
	}
	printf("\n"); //poor man's flush() 
}

//we don't handle "button pressed and wheel moved" events for now. so this is not complete...
enum class PowerMateEvent {
	Unknown,
	WheelLeft,
	WheelRight,
	ButtonDown,
	ButtonUp,
};

//I wish c++ would make this easier ... using Swift made me weak :)
namespace std {
	string to_string (PowerMateEvent ev) {
		switch (ev) {
			case PowerMateEvent::WheelLeft:
				return "WheelLeft";
				break;
			case PowerMateEvent::WheelRight:
				return "WheelRight";
				break;
			case PowerMateEvent::ButtonDown:
				return "ButtonDown";
				break;
			case PowerMateEvent::ButtonUp:
				return "ButtonUp";
				break;
			default:
				return "Unknown";
		}
	}
}

//this works for our limited 4 event-type code but does ignore
//the button-held-down modifier byte and discards some scroll values :)
PowerMateEvent parse_buffer_stream(unsigned char buf[65]) {
	if (buf[0] == 0x01 && buf[1] == 0x00) {
		return PowerMateEvent::ButtonDown;
	}
	if (buf[0] == 0x00 && buf[1] == 0x00) {
		return PowerMateEvent::ButtonUp;
	}

	if (buf[1] == 0xff) {
		return PowerMateEvent::WheelLeft;
	}
	if (buf[1] == 0x01) {
		return PowerMateEvent::WheelRight;
	}
return PowerMateEvent::Unknown;	
}


//hooked up this thing to my philips hue scripts
//see https://github.com/kybernetyk/stroboface
void dispatch_event(PowerMateEvent ev) {
	static int cur_bri = -1;
	static int prev_bri = -10000;
	if (ev == PowerMateEvent::Unknown) {
		return;
	}

	if (ev == PowerMateEvent::WheelRight) {
		cur_bri += 20;
	}

	if (ev == PowerMateEvent::WheelLeft) {
		cur_bri -= 20;
	}

	if (ev == PowerMateEvent::ButtonDown) {
		cur_bri = 255;
	}

	if (ev == PowerMateEvent::ButtonUp) {
		cur_bri = 0;
	}

	//	 let val = min(max(0, brightness), 255)

	cur_bri = std::min(std::max(0, cur_bri), 255);
	if (cur_bri == prev_bri) {
		return;
	}

	prev_bri = cur_bri;

	std::string cmd = "lights -t 3 bri " + std::to_string(cur_bri);
	printf("%s\n", cmd.c_str());
	std::system(cmd.c_str());
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main(int argc, char **argv) {
	enumerate_devices();

	hid_device *handle = hid_open(0x077d, 0x0410, nullptr);
	if (!handle) {
		fprintf(stderr, "No powermate found, m8!\n");
		return 1;
	}

	const int MAX_STR = 255;
	wchar_t wstr[MAX_STR];

	printf("Device Info:\n");
	int res = hid_get_manufacturer_string(handle, wstr, MAX_STR);
	if (res == -1) {
		fprintf(stderr, "Could not get manufacturer string!\n");
		return 2;
	}
	printf("\tManufacturer String: %ls\n", wstr);

	res = hid_get_product_string(handle, wstr, MAX_STR);
	if (res == -1) {
		fprintf(stderr, "Could not get product string!\n");
		return 3;
	}
	printf("\tProduct String: %ls\n", wstr);

	dump_features(handle);

	//make the dvice nonblocking.
	hid_set_nonblocking(handle, 1);

	unsigned char buf[65];
	for (;;) {
		int num = hid_read(handle, buf, 64);
		if (num < 0) {
			fprintf(stderr, "Could not read from device!\n");
			return 4;
		}
		if (num == 6) {
			time_t now;
			time(&now);
			tm loctm;
			localtime_r(&now, &loctm);

			//man, I'm really shitty at printf()...
			printf("<%02d:%02d:%02d>: ",loctm.tm_hour, loctm.tm_min, loctm.tm_sec);
			for (int i = 0; i < num; i++) {
				printf("%02hhx ", buf[i]);
			}
			printf("\n");

			auto ev = parse_buffer_stream(buf);
			printf("%s\n", std::to_string(ev).c_str());
			dispatch_event(ev);
		}
	}
	return 0;
}

