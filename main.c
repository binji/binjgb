#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define SUCCESS(x) ((x) == OK)
#define FAIL(x) ((x) != OK)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ZERO_MEMORY(x) memset(&(x), 0, sizeof(x))

#define PRINT_ERROR(...) fprintf(stderr, __VA_ARGS__)
#define CHECK_MSG(x, ...)                       \
  if (!(x)) {                                   \
    PRINT_ERROR("%s:%d: ", __FILE__, __LINE__); \
    PRINT_ERROR(__VA_ARGS__);                   \
    goto error;                                 \
  }
#define CHECK(x) \
  if (!(x)) {    \
    goto error;  \
  }

#define ROM_U8(type, addr) ((type)*(rom_data->data + addr))
#define ROM_U16_BE(addr) \
  ((uint16_t)((ROM_U8(uint16_t, addr) << 8) | ROM_U8(uint16_t, addr + 1)))

#define ENTRY_POINT_START_ADDR 0x100
#define ENTRY_POINT_END_ADDR 0x103
#define LOGO_START_ADDR 0x104
#define LOGO_END_ADDR 0x133
#define TITLE_START_ADDR 0x134
#define TITLE_END_ADDR 0x143
#define MANUFACTURERS_CODE_START_ADDR 0x13F
#define MANUFACTURERS_CODE_END_ADDR 0x142
#define CGB_FLAG_ADDR 0x143
#define NEW_LINCENSEE_CODE_START_ADDR 0x144
#define NEW_LINCENSEE_CODE_END_ADDR 0x145
#define SGB_FLAG_ADDR 0x146
#define CARTRIDGE_TYPE_ADDR 0x147
#define ROM_SIZE_ADDR 0x148
#define RAM_SIZE_ADDR 0x149
#define DESTINATATION_CODE_ADDR 0x14a
#define OLD_LICENSEE_CODE_ADDR 0x14b
#define MASK_ROM_VERSION_NUMBER_ADDR 0x14c
#define HEADER_CHECKSUM_ADDR 0x14d
#define GLOBAL_CHECKSUM_START_ADDR 0x14e
#define GLOBAL_CHECKSUM_END_ADDR 0x14f

#define MINIMUM_ROM_SIZE 32768
#define BANK_BYTE_SIZE 16384

#define NEW_LICENSEE_CODE 0x33

#define HEADER_CHECKSUM_RANGE_START 0x134
#define HEADER_CHECKSUM_RANGE_END 0x14c

#define FOREACH_RESULT(V) \
  V(OK, 0)                \
  V(ERROR, 1)

#define FOREACH_CGB_FLAG(V)   \
  V(CGB_FLAG_NONE, 0)         \
  V(CGB_FLAG_SUPPORTED, 0x80) \
  V(CGB_FLAG_REQUIRED, 0xC0)

#define FOREACH_SGB_FLAG(V) \
  V(SGB_FLAG_NONE, 0)       \
  V(SGB_FLAG_SUPPORTED, 3)

#define FOREACH_CARTRIDGE_TYPE(V)                 \
  V(CARTRIDGE_TYPE_ROM_ONLY, 0x0)                 \
  V(CARTRIDGE_TYPE_MBC1, 0x1)                     \
  V(CARTRIDGE_TYPE_MBC1_RAM, 0x2)                 \
  V(CARTRIDGE_TYPE_MBC1_RAM_BATTERY, 0x3)         \
  V(CARTRIDGE_TYPE_MBC2, 0x5)                     \
  V(CARTRIDGE_TYPE_MBC2_BATTERY, 0x6)             \
  V(CARTRIDGE_TYPE_ROM_RAM, 0x8)                  \
  V(CARTRIDGE_TYPE_ROM_RAM_BATTERY, 0x9)          \
  V(CARTRIDGE_TYPE_MMM01, 0xb)                    \
  V(CARTRIDGE_TYPE_MMM01_RAM, 0xc)                \
  V(CARTRIDGE_TYPE_MMM01_RAM_BATTERY, 0xd)        \
  V(CARTRIDGE_TYPE_MBC3_TIMER_BATTERY, 0xf)       \
  V(CARTRIDGE_TYPE_MBC3_TIMER_RAM_BATTERY, 0x10)  \
  V(CARTRIDGE_TYPE_MBC3, 0x11)                    \
  V(CARTRIDGE_TYPE_MBC3_RAM, 0x12)                \
  V(CARTRIDGE_TYPE_MBC3_RAM_BATTERY, 0x13)        \
  V(CARTRIDGE_TYPE_MBC4, 0x15)                    \
  V(CARTRIDGE_TYPE_MBC4_RAM, 0x16)                \
  V(CARTRIDGE_TYPE_MBC4_RAM_BATTERY, 0x17)        \
  V(CARTRIDGE_TYPE_MBC5, 0x19)                    \
  V(CARTRIDGE_TYPE_MBC5_RAM, 0x1a)                \
  V(CARTRIDGE_TYPE_MBC5_RAM_BATTERY, 0x1b)        \
  V(CARTRIDGE_TYPE_MBC5_RUMBLE, 0x1c)             \
  V(CARTRIDGE_TYPE_MBC5_RUMBLE_RAM, 0x1d)         \
  V(CARTRIDGE_TYPE_MBC5_RUMBLE_RAM_BATTERY, 0x1e) \
  V(CARTRIDGE_TYPE_POCKET_CAMERA, 0xfc)           \
  V(CARTRIDGE_TYPE_BANDAI_TAMA5, 0xfd)            \
  V(CARTRIDGE_TYPE_HUC3, 0xfe)                    \
  V(CARTRIDGE_TYPE_HUC1_RAM_BATTERY, 0xff)

#define FOREACH_ROM_SIZE(V)  \
  V(ROM_SIZE_32K, 0, 2)      \
  V(ROM_SIZE_64K, 1, 4)      \
  V(ROM_SIZE_128K, 2, 8)     \
  V(ROM_SIZE_256K, 3, 16)    \
  V(ROM_SIZE_512K, 4, 32)    \
  V(ROM_SIZE_1M, 5, 64)      \
  V(ROM_SIZE_2M, 6, 128)     \
  V(ROM_SIZE_4M, 7, 256)     \
  V(ROM_SIZE_1_1M, 0x52, 72) \
  V(ROM_SIZE_1_2M, 0x53, 80) \
  V(ROM_SIZE_1_5M, 0x54, 96)

#define FOREACH_RAM_SIZE(V) \
  V(RAM_SIZE_NONE, 0, 0)    \
  V(RAM_SIZE_2K, 1, 2048)   \
  V(RAM_SIZE_8K, 2, 8192)   \
  V(RAM_SIZE_32K, 3, 32768)

#define FOREACH_DESTINATION_CODE(V) \
  V(DESTINATION_CODE_JAPANESE, 0)   \
  V(DESTINATION_CODE_NON_JAPANESE, 1)

#define DEFINE_ENUM(name, code, ...) name = code,
#define DEFINE_STRING(name, code, ...) [code] = #name,

const char* get_enum_string(const char** strings,
                            size_t string_count,
                            size_t value) {
  return value < string_count ? strings[value] : "unknown";
}

#define DEFINE_NAMED_ENUM(NAME, Name, name, foreach)                 \
  enum Name { foreach (DEFINE_ENUM) NAME##_COUNT };                  \
  enum Result is_##name##_valid(enum Name value) {                   \
    return value < NAME##_COUNT;                                     \
  }                                                                  \
  const char* get_##name##_string(enum Name value) {                 \
    static const char* s_strings[] = {foreach (DEFINE_STRING)};      \
    return get_enum_string(s_strings, ARRAY_SIZE(s_strings), value); \
  }

DEFINE_NAMED_ENUM(RESULT, Result, result, FOREACH_RESULT)
DEFINE_NAMED_ENUM(CGB_FLAG, CgbFlag, cgb_flag, FOREACH_CGB_FLAG)
DEFINE_NAMED_ENUM(SGB_FLAG, SgbFlag, sgb_flag, FOREACH_SGB_FLAG)
DEFINE_NAMED_ENUM(CARTRIDGE_TYPE,
                  CartridgeType,
                  cartridge_type,
                  FOREACH_CARTRIDGE_TYPE)
DEFINE_NAMED_ENUM(ROM_SIZE, RomSize, rom_size, FOREACH_ROM_SIZE)
DEFINE_NAMED_ENUM(RAM_SIZE, RamSize, ram_size, FOREACH_RAM_SIZE)
DEFINE_NAMED_ENUM(DESTINATION_CODE,
                  DestinationCode,
                  destination_code,
                  FOREACH_DESTINATION_CODE)

uint32_t s_rom_bank_size[] = {
#define V(name, code, bank_size) [code] = bank_size,
    FOREACH_ROM_SIZE(V)
#undef V
};

struct RomData {
  uint8_t* data;
  size_t size;
};

struct StringSlice {
  const char* start;
  size_t length;
};

struct RomInfo {
  struct StringSlice title;
  struct StringSlice manufacturer;
  enum CgbFlag cgb_flag;
  struct StringSlice new_licensee;
  uint8_t old_licensee_code;
  enum SgbFlag sgb_flag;
  enum CartridgeType cartridge_type;
  enum RomSize rom_size;
  enum RamSize ram_size;
  enum DestinationCode destination_code;
  uint8_t header_checksum;
  uint16_t global_checksum;
  enum Result header_checksum_valid;
  enum Result global_checksum_valid;
};

enum Result read_rom(const char* filename, struct RomData* out_rom_data) {
  FILE* f = fopen(filename, "rb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  CHECK_MSG(fseek(f, 0, SEEK_END) >= 0, "fseek to end failed.\n");
  long size = ftell(f);
  CHECK_MSG(size >= 0, "ftell failed.");
  CHECK_MSG(fseek(f, 0, SEEK_SET) >= 0, "fseek to beginning failed.\n");
  CHECK_MSG(size >= MINIMUM_ROM_SIZE, "size < minimum rom size (%u).\n",
            MINIMUM_ROM_SIZE);
  uint8_t* data = malloc(size);
  CHECK_MSG(data, "allocation failed.");
  CHECK_MSG(fread(data, size, 1, f) == 1, "fread failed.\n");
  fclose(f);
  out_rom_data->data = data;
  out_rom_data->size = size;
  return OK;
error:
  if (f) {
    fclose(f);
  }
  return ERROR;
}

void get_rom_title(struct RomData* rom_data, struct StringSlice* out_title) {
  const char* start = (char*)rom_data->data + TITLE_START_ADDR;
  const char* end = start + TITLE_END_ADDR;
  const char* p = start;
  size_t length = 0;
  while (p <= end && *p != 0 && (*p & 0x80) == 0) {
    length++;
    p++;
  }
  out_title->start = start;
  out_title->length = length;
}

void get_manufacturer_code(struct RomData* rom_data,
                           struct StringSlice* out_manufacturer) {
  const char* start = (char*)rom_data->data + MANUFACTURERS_CODE_START_ADDR;
  const char* end = start + MANUFACTURERS_CODE_END_ADDR;
  if (*(start - 1) != 0) {
    /* The title is too long, so this must be a ROM without a manufacturer's
     * code */
    out_manufacturer->start = "";
    out_manufacturer->length = 0;
    return;
  }

  const char* p = start;
  size_t length = 0;
  while (p <= end && *p != 0) {
    length++;
    p++;
  }
  out_manufacturer->start = start;
  out_manufacturer->length = length;
}

void get_new_licensee(struct RomData* rom_data,
                      uint8_t old_licensee_code,
                      struct StringSlice* out_licensee) {
  if (old_licensee_code == NEW_LICENSEE_CODE) {
    out_licensee->start =
        (const char*)rom_data->data + NEW_LINCENSEE_CODE_START_ADDR;
    out_licensee->length =
        NEW_LINCENSEE_CODE_END_ADDR - NEW_LINCENSEE_CODE_START_ADDR + 1;
  } else {
    out_licensee->start = "";
    out_licensee->length = 0;
  }
}

enum Result validate_header_checksum(struct RomData* rom_data) {
  uint8_t expected_checksum = ROM_U8(uint8_t, HEADER_CHECKSUM_ADDR);
  uint8_t checksum = 0;
  size_t i = 0;
  for (i = HEADER_CHECKSUM_RANGE_START; i <= HEADER_CHECKSUM_RANGE_END; ++i) {
    checksum = checksum - rom_data->data[i] - 1;
  }
  return checksum == expected_checksum ? OK : ERROR;
}

enum Result validate_global_checksum(struct RomData* rom_data) {
  uint16_t expected_checksum = ROM_U16_BE(GLOBAL_CHECKSUM_START_ADDR);
  uint16_t checksum = 0;
  size_t i = 0;
  for (i = 0; i < rom_data->size; ++i) {
    if (i == GLOBAL_CHECKSUM_START_ADDR || i == GLOBAL_CHECKSUM_END_ADDR)
      continue;
    checksum += rom_data->data[i];
  }
  return checksum == expected_checksum ? OK : ERROR;
}

uint32_t get_rom_bank_size(enum RomSize rom_size) {
  return is_rom_size_valid(rom_size) ? s_rom_bank_size[rom_size] : 0;
}

uint32_t get_rom_byte_size(enum RomSize rom_size) {
  return get_rom_bank_size(rom_size) * BANK_BYTE_SIZE;
}

enum Result get_rom_info(struct RomData* rom_data,
                         struct RomInfo* out_rom_info) {
  struct RomInfo rom_info;
  ZERO_MEMORY(rom_info);

  rom_info.rom_size = ROM_U8(enum RomSize, ROM_SIZE_ADDR);
  uint32_t rom_byte_size = get_rom_byte_size(rom_info.rom_size);
  CHECK_MSG(rom_data->size == rom_byte_size,
            "Invalid ROM size: expected %u, got %zu.\n", rom_byte_size,
            rom_data->size);

  get_rom_title(rom_data, &rom_info.title);
  get_manufacturer_code(rom_data, &rom_info.manufacturer);
  rom_info.cgb_flag = ROM_U8(enum CgbFlag, CGB_FLAG_ADDR);
  rom_info.sgb_flag = ROM_U8(enum SgbFlag, SGB_FLAG_ADDR);
  rom_info.cartridge_type = ROM_U8(enum CartridgeType, CARTRIDGE_TYPE_ADDR);
  rom_info.ram_size = ROM_U8(enum RamSize, RAM_SIZE_ADDR);
  rom_info.destination_code =
      ROM_U8(enum DestinationCode, DESTINATATION_CODE_ADDR);
  rom_info.old_licensee_code = ROM_U8(uint8_t, OLD_LICENSEE_CODE_ADDR);
  get_new_licensee(rom_data, rom_info.old_licensee_code,
                   &rom_info.new_licensee);
  rom_info.header_checksum = ROM_U8(uint8_t, HEADER_CHECKSUM_ADDR);
  rom_info.header_checksum_valid = validate_header_checksum(rom_data);
  rom_info.global_checksum = ROM_U16_BE(GLOBAL_CHECKSUM_START_ADDR);
  rom_info.global_checksum_valid = validate_global_checksum(rom_data);

  *out_rom_info = rom_info;
  return OK;
error:
  return ERROR;
}

void print_rom_info(struct RomInfo* rom_info) {
  printf("title: \"%.*s\"\n", (int)rom_info->title.length,
         rom_info->title.start);
  printf("manufacturer: \"%.*s\"\n", (int)rom_info->manufacturer.length,
         rom_info->manufacturer.start);
  printf("cgb flag: %s\n", get_cgb_flag_string(rom_info->cgb_flag));
  printf("sgb flag: %s\n", get_sgb_flag_string(rom_info->sgb_flag));
  printf("cartridge type: %s\n",
         get_cartridge_type_string(rom_info->cartridge_type));
  printf("rom size: %s\n", get_rom_size_string(rom_info->rom_size));
  printf("ram size: %s\n", get_ram_size_string(rom_info->ram_size));
  printf("destination code: %s\n",
         get_destination_code_string(rom_info->destination_code));
  printf("old licensee code: %u\n", rom_info->old_licensee_code);
  printf("new licensee: %.*s\n", (int)rom_info->new_licensee.length,
         rom_info->new_licensee.start);

  printf("header checksum: 0x%02x [%s]\n", rom_info->header_checksum,
         get_result_string(rom_info->header_checksum_valid));
  printf("global checksum: 0x%04x [%s]\n", rom_info->global_checksum,
         get_result_string(rom_info->global_checksum_valid));
}

int main(int argc, char** argv) {
  --argc; ++argv;
  CHECK_MSG(argc == 1, "no rom file given.\n");
  struct RomData rom_data;
  CHECK(SUCCESS(read_rom(argv[0], &rom_data)));
  struct RomInfo rom_info;
  CHECK(SUCCESS(get_rom_info(&rom_data, &rom_info)));
  print_rom_info(&rom_info);
  return 0;
error:
  return 1;
}
