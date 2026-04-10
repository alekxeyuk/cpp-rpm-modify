#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "rpmlib.h"

namespace {
  constexpr uint32_t kTagImmutable = 63;
  constexpr uint32_t kTagVendor = 1011;
  constexpr uint32_t kTagPackager = 1015;

  constexpr uint32_t kTypeInt16 = 3;
  constexpr uint32_t kTypeInt32 = 4;
  constexpr uint32_t kTypeString = 6;
  constexpr uint32_t kTypeBin = 7;
  constexpr uint32_t kTypeStringArray = 8;
  constexpr uint32_t kTypeI18nString = 9;

  struct TagValues {
    std::string vendor;
    std::string packager;
  };

  struct RewriteConfig {
    std::string vendor;
    std::string packager;
    ptrdiff_t vendor_delta{};
    ptrdiff_t packager_delta{};
    size_t new_data_size{};
  };

  struct HeaderSectionInfo {
    size_t index_offset{};
    size_t data_offset{};
  };

  struct PayloadInfo {
    size_t offset{};
    size_t size{};
    std::vector<char> data;
  };

  struct RewriteState {
    int entry_index{};
    ptrdiff_t total_offset{};
    ptrdiff_t align_offset{};
    bool has_immutable_entry{};
    char immutable_bytes[16]{};
    RPMIndexEntry immutable_entry{};
  };

  size_t align_to(size_t offset, size_t alignment) {
    return (offset + alignment - 1) & ~(alignment - 1);
  }

  size_t align_to_8_bytes(size_t offset) {
    return align_to(offset, 8);
  }

  size_t align_to_4_bytes(size_t offset) {
    return align_to(offset, 4);
  }

  size_t align_to_2_bytes(size_t offset) {
    return align_to(offset, 2);
  }

  uint32_t add_signed_offset(uint32_t offset, ptrdiff_t delta) {
    return static_cast<uint32_t>(static_cast<ptrdiff_t>(offset) + delta);
  }

  void print_usage(const char* program_name) {
    std::fprintf(stderr, "Usage: %s <rpm_file> <packager> <vendor>\n", program_name);
  }

  bool validate_file_open(const std::fstream& file) {
    if (file.is_open()) {
      return true;
    }

    std::cout << "Cant open file\n";
    return false;
  }

  bool validate_lead(const RPMLead& lead) {
    if (lead.is_correct()) {
      return true;
    }

    std::cout << "Wrong file\n";
    return false;
  }

  bool validate_header(const RPMHeader& header) {
    if (header.is_correct()) {
      return true;
    }

    std::cout << "Wrong Head\n";
    return false;
  }

  RPMLead read_lead(std::fstream& file) {
    RPMLead lead{};
    file.read(reinterpret_cast<char*>(&lead), sizeof(RPMLead));

    lead.magic = _byteswap_ulong(lead.magic);
    lead.type = _byteswap_ushort(lead.type);
    lead.arch = _byteswap_ushort(lead.arch);
    lead.os = _byteswap_ushort(lead.os);
    lead.signature_type = _byteswap_ushort(lead.signature_type);

    return lead;
  }

  RPMHeader read_header(std::fstream& file) {
    RPMHeader header{};
    file.read(reinterpret_cast<char*>(&header), 16);

    header.byte_swap();

    header.index_entries.resize(header.count);
    file.read(reinterpret_cast<char*>(header.index_entries.data()), sizeof(RPMIndexEntry) * header.count);

    for (auto& entry : header.index_entries) {
      entry.byte_swap();
    }

    return header;
  }

  HeaderSectionInfo find_main_header(std::fstream& file, const RPMHeader& signature_header) {
    file.seekg(align_to_8_bytes(signature_header.data_size), std::ios::cur);

    HeaderSectionInfo info{};
    info.index_offset = static_cast<size_t>(file.tellg());
    std::cout << "Header Index offset : " << info.index_offset << '\n';
    return info;
  }

  std::vector<char> read_header_data(std::fstream& file, const RPMHeader& header) {
    std::vector<char> data(header.data_size);
    file.read(data.data(), static_cast<std::streamsize>(header.data_size));
    return data;
  }

  PayloadInfo read_payload(std::fstream& file) {
    PayloadInfo payload{};
    payload.offset = static_cast<size_t>(file.tellg());
    std::cout << "RPM Data offset : " << payload.offset << '\n';

    file.seekg(0, std::ios::end);
    std::cout << "File ends at " << file.tellg() << '\n';

    payload.size = static_cast<size_t>(file.tellg()) - payload.offset;
    std::cout << "RPM Data size = " << payload.size << '\n';

    payload.data.resize(payload.size);
    file.seekg(static_cast<std::streamoff>(payload.offset), std::ios::beg);
    file.read(payload.data.data(), static_cast<std::streamsize>(payload.size));
    return payload;
  }

  TagValues extract_tag_values(const RPMHeader& header, const std::vector<char>& header_data) {
    TagValues values{};

    for (const auto& tag : header.index_entries) {
      if (tag.tag == kTagVendor) {
        values.vendor = header_data.data() + tag.offset;
        std::printf("[Old Vendor] [%u at %u] = [%s] [%zu]\n", tag.tag, tag.offset, values.vendor.c_str(), values.vendor.size());
      }

      if (tag.tag == kTagPackager) {
        values.packager = header_data.data() + tag.offset;
        std::printf("[Old Packager] [%u at %u] = [%s] [%zu]\n", tag.tag, tag.offset, values.packager.c_str(), values.packager.size());
      }
    }

    return values;
  }

  RewriteConfig make_rewrite_config(char* argv[], const RPMHeader& header, const TagValues& current_values) {
    RewriteConfig config{};
    config.vendor = argv[2];
    config.packager = argv[3];
    config.vendor_delta = static_cast<ptrdiff_t>(config.vendor.size()) - static_cast<ptrdiff_t>(current_values.vendor.size());
    config.packager_delta = static_cast<ptrdiff_t>(config.packager.size()) - static_cast<ptrdiff_t>(current_values.packager.size());
    config.new_data_size = static_cast<size_t>(
      static_cast<ptrdiff_t>(header.data_size) + config.vendor_delta + config.packager_delta);

    std::printf("off_delta_vendor[%td] off_delta_packager[%td]\n", config.vendor_delta, config.packager_delta);
    return config;
  }

  void print_entry_debug(const RPMIndexEntry& entry, size_t old_offset, const RewriteState& state) {
    std::printf("Entry [%d]\n", state.entry_index);
    std::printf("| Tag: %u | ", entry.tag);
    std::printf("Type: %u | ", entry.type);
    std::printf("Old Offset: %zu | ", old_offset);
    std::printf("New Offset: %td | ", static_cast<ptrdiff_t>(old_offset) + state.total_offset);
    std::printf("Count: %u |\n", entry.count);
  }

  void remember_immutable_region(
    RPMIndexEntry& entry,
    size_t old_offset,
    const std::vector<char>& header_data,
    const RewriteConfig& config,
    RewriteState& state) {
    if (entry.tag != kTagImmutable) {
      return;
    }

    entry.offset = add_signed_offset(entry.offset, config.vendor_delta + config.packager_delta);
    std::memcpy(state.immutable_bytes, header_data.data() + old_offset, entry.count);
    state.immutable_entry = entry;
    state.has_immutable_entry = true;
  }

  void copy_string_values(char* destination, const char* source, const RPMIndexEntry& entry) {
    size_t array_offset = 0;
    for (size_t index = 0; index < entry.count; ++index) {
      const size_t string_size = std::strlen(source + array_offset) + 1;
      strcpy_s(destination + array_offset, string_size, source + array_offset);
      array_offset += string_size;
    }
  }

  void copy_fixed_width_values(char* destination, const char* source, const RPMIndexEntry& entry, size_t value_size) {
    size_t array_offset = 0;
    for (size_t index = 0; index < entry.count; ++index) {
      std::memcpy(destination + array_offset, source + array_offset, value_size);
      array_offset += value_size;
    }
  }

  void apply_alignment_if_needed(RPMIndexEntry& entry, size_t header_data_offset, RewriteState& state) {
    size_t aligned_offset = entry.offset;

    if (entry.type == kTypeInt32) {
      aligned_offset = align_to_4_bytes(header_data_offset + entry.offset) - header_data_offset;
    }
    else if (entry.type == kTypeInt16) {
      aligned_offset = align_to_2_bytes(header_data_offset + entry.offset) - header_data_offset;
    }
    else {
      return;
    }

    if (aligned_offset > entry.offset) {
      std::printf("[LAST OFFSET] = [%u] [ALLIGNED OFFSET] = [%zu]\n", entry.offset, aligned_offset);
      state.align_offset += static_cast<ptrdiff_t>(aligned_offset - entry.offset);
      entry.offset = add_signed_offset(entry.offset, state.align_offset);
    }
  }

  void copy_entry_data(
    RPMIndexEntry& entry,
    size_t old_offset,
    const RewriteConfig& config,
    const std::vector<char>& header_data,
    std::vector<char>& rewritten_data,
    size_t header_data_offset,
    RewriteState& state) {
    char* destination = rewritten_data.data() + entry.offset;
    const char* source = header_data.data() + old_offset;

    if (entry.tag == kTagVendor) {
      state.total_offset += config.vendor_delta;
      strcpy_s(destination, config.vendor.size() + 1, config.vendor.c_str());
      return;
    }

    if (entry.tag == kTagPackager) {
      state.total_offset += config.packager_delta;
      strcpy_s(destination, config.packager.size() + 1, config.packager.c_str());
      return;
    }

    if (entry.type == kTypeBin) {
      std::memcpy(destination, source, entry.count);
      return;
    }

    if (entry.type == kTypeStringArray || entry.type == kTypeString || entry.type == kTypeI18nString) {
      copy_string_values(destination, source, entry);
      return;
    }

    apply_alignment_if_needed(entry, header_data_offset, state);
    destination = rewritten_data.data() + entry.offset;

    if (entry.type == kTypeInt32) {
      copy_fixed_width_values(destination, source, entry, 4);
    }
    else if (entry.type == kTypeInt16) {
      copy_fixed_width_values(destination, source, entry, 2);
    }
  }

  RewriteState rewrite_entries(
    std::fstream& file,
    RPMHeader& header,
    const RewriteConfig& config,
    const std::vector<char>& header_data,
    std::vector<char>& rewritten_data,
    size_t header_data_offset) {
    RewriteState state{};

    file.write("XXXXXXXXXXXXXXX", 16);

    for (auto& entry : header.index_entries) {
      const size_t old_offset = entry.offset;

      print_entry_debug(entry, old_offset, state);
      remember_immutable_region(entry, old_offset, header_data, config, state);

      entry.offset = add_signed_offset(entry.offset, state.total_offset + state.align_offset);
      copy_entry_data(entry, old_offset, config, header_data, rewritten_data, header_data_offset, state);

      entry.byte_swap();
      file.write(reinterpret_cast<char*>(&entry), sizeof(entry));
      ++state.entry_index;
    }

    return state;
  }

  void write_header_data_and_payload(
    std::fstream& file,
    const RewriteConfig& config,
    const RewriteState& state,
    std::vector<char>& rewritten_data,
    const PayloadInfo& payload) {
    if (state.has_immutable_entry) {
      std::memcpy(
        rewritten_data.data() + state.immutable_entry.offset + state.align_offset,
        state.immutable_bytes,
        16);
    }

    file.write(rewritten_data.data(), static_cast<std::streamsize>(config.new_data_size + state.align_offset));
    file.write(payload.data.data(), static_cast<std::streamsize>(payload.size));
  }

  void rewrite_header_prefix(std::fstream& file, size_t header_index_offset, RPMHeader header, const RewriteConfig& config, const RewriteState& state) {
    file.seekp(static_cast<std::streamoff>(header_index_offset), std::ios::beg);
    header.data_size = static_cast<uint32_t>(config.new_data_size + state.align_offset);
    header.byte_swap();
    file.write(reinterpret_cast<char*>(&header), 16);

    if (state.has_immutable_entry) {
      RPMIndexEntry immutable_entry = state.immutable_entry;
      immutable_entry.offset = add_signed_offset(immutable_entry.offset, state.align_offset);
      immutable_entry.byte_swap();
      file.write(reinterpret_cast<char*>(&immutable_entry), sizeof(immutable_entry));
    }
  }

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 4) {
    print_usage(argv[0]);
    return 1;
  }

  std::fstream file(argv[1], std::ios::in | std::ios::out | std::ios::binary);
  if (!validate_file_open(file)) {
    file.close();
    return EXIT_FAILURE;
  }

  const RPMLead lead = read_lead(file);
  if (!validate_lead(lead)) {
    file.close();
    return EXIT_FAILURE;
  }

  lead.print();
  std::cout << file.tellg() << '\n';

  const RPMHeader signature_header = read_header(file);
  if (!validate_header(signature_header)) {
    file.close();
    return EXIT_FAILURE;
  }

  signature_header.print();
  std::cout << file.tellg() << '\n';
  signature_header.print_entries();

  const HeaderSectionInfo main_header_info = find_main_header(file, signature_header);

  RPMHeader header = read_header(file);
  if (!validate_header(header)) {
    file.close();
    return EXIT_FAILURE;
  }

  header.print();

  const size_t header_data_offset = static_cast<size_t>(file.tellg());
  std::cout << "Header Data offset : " << header_data_offset << '\n';
  header.print_entries();

  const std::vector<char> header_data = read_header_data(file, header);
  const PayloadInfo payload = read_payload(file);

  const TagValues current_values = extract_tag_values(header, header_data);
  const RewriteConfig config = make_rewrite_config(argv, header, current_values);
  std::vector<char> rewritten_data(config.new_data_size * 2, '\0');

  file.seekp(static_cast<std::streamoff>(main_header_info.index_offset), std::ios::beg);
  const RewriteState state = rewrite_entries(file, header, config, header_data, rewritten_data, header_data_offset);
  write_header_data_and_payload(file, config, state, rewritten_data, payload);
  rewrite_header_prefix(file, main_header_info.index_offset, header, config, state);

  file.close();
  return EXIT_SUCCESS;
}
