#pragma once

#include <cstdint>
#include <vector>
#include <string>

uint32_t lead_magic = 0xEDABEEDB;
uint32_t header_magic = 0x8EADE801;

struct RPMLead {
	uint32_t magic;        // Magic number: 0xed, 0xab, 0xee, 0xdb
	uint8_t major;           // Major version number
	uint8_t minor;           // Minor version number
	uint16_t type;           // Package type: binary or source
	uint16_t arch;           // Target architecture
	char name[66];           // Package name
	uint16_t os;             // Operating system
	uint16_t signature_type; // Signature type
	char reserved[16];       // Reserved bytes

	void print() const {
		printf("Lead Magic: %X\n", magic);
		printf("Lead Version: %d.%d\n", major, minor);
		printf("Lead Type: %X\n", type);
		printf("Lead Arch: %X\n", arch);
		printf("Lead Name: %s\n", name);
		printf("Lead Os: %X\n", os);
		printf("Lead SignatureType: %X\n", signature_type);
	}

	bool is_correct() const {
		if (magic != lead_magic
			|| major != 3
			|| minor != 0
			|| os != 1
			|| signature_type != 5
			) {
			return false;
		}
		return true;
	}
};

struct RPMIndexEntry {
	uint32_t tag;            // Tag identifier
	uint32_t type;           // Data type
	uint32_t offset;         // Offset to data
	uint32_t count;          // Number of items
};

struct RPMHeader {
	uint32_t magic;        // Magic number: 0x8e, 0xad, 0xe8, 0x01
	uint32_t reserved;       // Reserved bytes
	uint32_t count;          // Number of index entries
	uint32_t data_size;      // Size of data store
	std::vector<RPMIndexEntry> index_entries; // Index entries
	std::vector<uint8_t> data_store;         // Data store

	void print() const {
		printf("Head Magic: %X\n", magic);
		printf("Head Count: %d\n", count);
		printf("Head Data Size: %d\n", data_size);
	}

	void print_entries() const {
		int counter = 0;
		for (RPMIndexEntry entry : index_entries)
		{
			printf("Entry [%d]\n", counter);
			printf("| Tag: %d | ", entry.tag);
			printf("Type: %d | ", entry.type);
			printf("Offset: %d | ", entry.offset);
			printf("Count: %d |\n", entry.count);
			counter++;
		}
	}

	bool is_correct() const {
		return magic == header_magic;
	}
};

struct RPMSignature {
	RPMHeader header;        // Signature header
};

struct RPMFileEntry {
	std::string name;        // File name
	uint32_t mode;           // File mode (permissions)
	uint32_t size;           // File size
	uint32_t mtime;          // Modification time
	uint32_t digest;         // File digest
	uint16_t link_flag;      // Link flag
	std::string link_name;   // Link target
};

struct RPMPayload {
	std::vector<RPMFileEntry> files; // List of files in the payload
	std::vector<uint8_t> compressed_data; // Compressed payload data
};

struct RPMPackage {
	RPMLead lead;            // RPM lead
	RPMSignature signature;  // RPM signature
	RPMHeader header;        // RPM header
	RPMPayload payload;      // RPM payload
};