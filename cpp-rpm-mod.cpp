﻿#include <iostream>
#include <fstream>
#include <cstring>
#include "rpmlib.h"

using namespace std;

// Helper functions for alignment
static uint32_t alignTo(uint32_t offset, uint32_t alignment) {
	return (offset + alignment - 1) & ~(alignment - 1);
}

// Simplified alignment functions
static uint32_t alignTo8Bytes(uint32_t offset) { return alignTo(offset, 8); }
static uint32_t alignTo4Bytes(uint32_t offset) { return alignTo(offset, 4); }
static uint32_t alignTo2Bytes(uint32_t offset) { return alignTo(offset, 2); }

static RPMLead read_lead(fstream& file) {
	RPMLead lead;
	file.read(reinterpret_cast<char*>(&lead), sizeof(RPMLead));

	lead.magic = _byteswap_ulong(lead.magic);
	lead.type = _byteswap_ushort(lead.type);
	lead.arch = _byteswap_ushort(lead.arch);
	lead.os = _byteswap_ushort(lead.os);
	lead.signature_type = _byteswap_ushort(lead.signature_type);

	return lead;
}

static RPMHeader read_header(fstream& file) {
	RPMHeader head;
	file.read(reinterpret_cast<char*>(&head), 16);

	head.byte_swap();

	head.index_entries.resize(head.count);
	file.read(reinterpret_cast<char*>(head.index_entries.data()), sizeof(RPMIndexEntry) * head.count);

	for (auto& entry : head.index_entries) {
		entry.byte_swap();
	}

	return head;
}

int main(int argc, char* argv[]) {
	if (argc != 4) {
		fprintf(stderr, "Usage: %s <rpm_file> <packager> <vendor>\n", argv[0]);
		return 1;
	}

	fstream file(argv[1], ios::in | ios::out | ios::binary);
	if (!file.is_open()) {
		cout << "Cant open file\n";
		file.close();
		return EXIT_FAILURE;
	}

	RPMLead lead = read_lead(file);
	if (!lead.is_correct()) {
		cout << "Wrong file\n";
		file.close();
		return EXIT_FAILURE;
	}

	lead.print();
	cout << file.tellg() << '\n';

	RPMHeader head = read_header(file);
	if (!head.is_correct()) {
		cout << "Wrong Head\n";
		file.close();
		return EXIT_FAILURE;
	}

	head.print();
	cout << file.tellg() << '\n';

	head.print_entries();

	file.seekg(alignTo8Bytes(head.data_size), ios::cur);
	size_t header_index_off = file.tellg();
	cout << "Header Index offset : " << header_index_off << '\n';


	head = read_header(file);
	if (!head.is_correct()) {
		cout << "Wrong Head\n";
		file.close();
		return EXIT_FAILURE;
	}

	head.print();
	size_t header_data_off = file.tellg();
	cout << "Header Data offset : " << header_data_off << '\n';

	head.print_entries();

	auto mem_arr = new char[head.data_size];
	file.read(mem_arr, head.data_size);

	size_t rpm_data_off = file.tellg();
	cout << "RPM Data offset : " << rpm_data_off << '\n';
	file.seekg(0, ios::end);
	cout << "File ends at " << file.tellg() << '\n';
	size_t rpm_data_size = (size_t)file.tellg() - rpm_data_off;
	cout << "RPM Data size = " << rpm_data_size << '\n';

	auto data_mem_arr = new char[rpm_data_size];
	file.seekg(rpm_data_off, ios::beg);
	file.read(data_mem_arr, rpm_data_size);

	string old_vendor;
	string old_packager;

	string new_vendor(argv[2]);
	size_t new_vendor_size = new_vendor.size();
	string new_packager(argv[3]);
	size_t new_packager_size = new_packager.size();

	for (auto& tag : head.index_entries) {
		if (tag.tag == 1011) {
			old_vendor = mem_arr + tag.offset;
			printf("[Old Vendor] [%d at %d] = [%s] [%lld]\n", tag.tag, tag.offset, old_vendor.c_str(), old_vendor.size());
		}
		if (tag.tag == 1015) {
			old_packager = mem_arr + tag.offset;
			printf("[Old Packager] [%d at %d] = [%s] [%lld]\n", tag.tag, tag.offset, old_packager.c_str(), old_packager.size());
		}
	}

	int off_delta_vendor = ((int)new_vendor_size) - old_vendor.size();
	int off_delta_packager = ((int)new_packager_size) - old_packager.size();
	printf("off_delta_vendor[%d] off_delta_packager[%d]\n", off_delta_vendor, off_delta_packager);

	file.seekp(header_index_off, ios::beg);

	size_t new_data_size = head.data_size + (off_delta_vendor + off_delta_packager);
	auto new_mem_arr = new char[new_data_size * 2]();


	file.write("XXXXXXXXXXXXXXX", 16);
	const char* temp_im_63[16];
	RPMIndexEntry temp_im_63_entry;


	int counter = 0;
	int total_offset = 0;
	int align_offset = 0;
	for (auto& entry : head.index_entries) {
		size_t old_offset = entry.offset;

		printf("Entry [%d]\n", counter);
		printf("| Tag: %d | ", entry.tag);
		printf("Type: %d | ", entry.type);
		printf("Old Offset: %lld | ", old_offset);
		printf("New Offset: %lld | ", old_offset + total_offset);
		printf("Count: %d |\n", entry.count);
		if (entry.tag == 63) {
			entry.offset += off_delta_vendor + off_delta_packager;
			memcpy(temp_im_63, mem_arr + old_offset, entry.count);
			temp_im_63_entry = entry;
		}


		entry.offset += total_offset + align_offset;
		if (entry.tag == 1011) {
			total_offset += off_delta_vendor;
			strcpy_s(new_mem_arr + entry.offset, new_vendor_size + 1, new_vendor.c_str());
		}
		else if (entry.tag == 1015) {
			total_offset += off_delta_packager;
			strcpy_s(new_mem_arr + entry.offset, new_packager_size + 1, new_packager.c_str());
		}
		else {
			if (entry.type == 7) { // BIN
				memcpy(new_mem_arr + entry.offset, mem_arr + old_offset, entry.count);
			}
			else if (entry.type == 8 || entry.type == 6 || entry.type == 9) { // STRING_ARRAY || STRING || I18NSTRING
				size_t array_off = 0;
				for (size_t i = 0; i < entry.count; i++) {
					size_t string_size = strlen(mem_arr + old_offset + array_off) + 1;
					strcpy_s(new_mem_arr + entry.offset + array_off, string_size, mem_arr + old_offset + array_off);
					array_off += string_size;
				}
			}
			else if (entry.type == 4) { // INT32
				auto aligned = alignTo4Bytes(header_data_off + entry.offset) - header_data_off;
				if (aligned > entry.offset) {
					printf("[LAST OFFSET] = [%lld] [ALLIGNED OFFSET] = [%uld]\n", entry.offset, aligned);
					align_offset += aligned - entry.offset;
					entry.offset += align_offset;
				}


				size_t array_off = 0;
				for (size_t i = 0; i < entry.count; i++) {
					memcpy(new_mem_arr + entry.offset + array_off, mem_arr + old_offset + array_off, 4);
					array_off += 4;
				}
			}
			else if (entry.type == 3) { // INT16
				auto aligned = alignTo2Bytes(header_data_off + entry.offset) - header_data_off;
				if (aligned > entry.offset) {
					printf("[LAST OFFSET] = [%lld] [ALLIGNED OFFSET] = [%u]\n", entry.offset, aligned);
					align_offset += aligned - entry.offset;
					entry.offset += align_offset;
				}


				size_t array_off = 0;
				for (size_t i = 0; i < entry.count; i++) {
					memcpy(new_mem_arr + entry.offset + array_off, mem_arr + old_offset + array_off, 2);
					array_off += 2;
				}
			}
		}


		entry.byte_swap();
		file.write((char*)&entry, sizeof(entry));

		counter++;
	}
	memcpy(new_mem_arr + temp_im_63_entry.offset + align_offset, temp_im_63, 16);
	file.write(new_mem_arr, new_data_size + align_offset);
	file.write(data_mem_arr, rpm_data_size);


	file.seekp(header_index_off, ios::beg);
	head.data_size = new_data_size + align_offset;
	head.byte_swap();
	file.write(reinterpret_cast<char*>(&head), 16);


	temp_im_63_entry.offset += align_offset;
	temp_im_63_entry.byte_swap();
	file.write((char*)&temp_im_63_entry, sizeof(temp_im_63_entry));

	file.close();
	delete[] mem_arr;
	delete[] data_mem_arr;
	delete[] new_mem_arr;
	return EXIT_SUCCESS;
}
