#include <assert.h>
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

#define UNREACHABLE(...)      \
  do {                        \
    PRINT_ERROR(__VA_ARGS__); \
    exit(1);                  \
  } while (0)

#define NOT_IMPLEMENTED(...) UNREACHABLE(__VA_ARGS__)

typedef uint16_t Address;
typedef uint16_t MaskedAddress;

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

#define NEW_LICENSEE_CODE 0x33
#define HEADER_CHECKSUM_RANGE_START 0x134
#define HEADER_CHECKSUM_RANGE_END 0x14c

#define MINIMUM_ROM_SIZE 32768
#define ROM_BANK_SHIFT 14
#define ROM_BANK_BYTE_SIZE (1 << ROM_BANK_SHIFT)

#define ADDR_MASK_8K 0x1fff
#define ADDR_MASK_16K 0x3fff
#define ADDR_MASK_32K 0x7fff

#define MBC1_RAM_ENABLED_MASK 0xf
#define MBC1_RAM_ENABLED_VALUE 0xa
#define MBC1_ROM_BANK_LO_MASK 0x1f
#define MBC1_BANK_HI_MASK 0x3
#define MBC1_BANK_HI_SHIFT 5

#define FOREACH_RESULT(V) \
  V(OK, 0)                \
  V(ERROR, 1)

#define FOREACH_BOOL(V) \
  V(FALSE, 0)           \
  V(TRUE, 1)

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
DEFINE_NAMED_ENUM(BOOL, Bool, bool, FOREACH_BOOL)
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

enum BankMode {
  BANK_MODE_ROM = 0,
  BANK_MODE_RAM = 1,
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
  uint32_t rom_banks;
  enum RamSize ram_size;
  enum DestinationCode destination_code;
  uint8_t header_checksum;
  uint16_t global_checksum;
  enum Result header_checksum_valid;
  enum Result global_checksum_valid;
};

struct Emulator;

struct MemoryMap {
  uint8_t (*read_rom_bank_switch)(struct Emulator*, MaskedAddress addr);
  void (*write_rom)(struct Emulator*, MaskedAddress addr, uint8_t value);
};

struct Mbc {
  uint32_t rom_bank;
  uint32_t ram_bank;
  enum Bool ram_enabled;
  enum BankMode bank_mode;
};

struct Emulator {
  struct RomData* rom_data;
  struct RomInfo rom_info;
  struct MemoryMap* memory_map;
  struct Mbc mbc;
};

enum Result read_rom_data_from_file(const char* filename,
                                    struct RomData* out_rom_data) {
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

uint32_t get_rom_bank_count(enum RomSize rom_size) {
  return is_rom_size_valid(rom_size) ? s_rom_bank_size[rom_size] : 0;
}

uint32_t get_rom_byte_size(enum RomSize rom_size) {
  return get_rom_bank_count(rom_size) * ROM_BANK_BYTE_SIZE;
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

  rom_info.rom_banks = get_rom_bank_count(rom_info.rom_size);

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

uint8_t rom_only_read_rom_bank_switch(struct Emulator* e, MaskedAddress addr) {
  /* Always return ROM in range 0x4000-0x7fff. */
  assert(addr <= ADDR_MASK_16K);
  addr += 0x4000;
  return e->rom_data->data[addr];
}

void rom_only_write_rom(struct Emulator* e, MaskedAddress addr, uint8_t value) {
  /* TODO(binji): log? */
}

uint8_t mbc1_read_rom_bank_switch(struct Emulator* e, MaskedAddress addr) {
  assert(addr <= ADDR_MASK_16K);
  uint32_t rom_addr = (e->mbc.rom_bank << ROM_BANK_SHIFT) | addr;
  if (rom_addr < e->rom_data->size) {
    return e->rom_data->data[rom_addr];
  } else {
    /* TODO(binji): log? */
    return 0;
  }
}

void mbc1_write_rom(struct Emulator* e, MaskedAddress addr, uint8_t value) {
  switch (addr >> 14) {
    case 0: /* 0000-1fff */
      e->mbc.ram_enabled =
          (value & MBC1_RAM_ENABLED_MASK) == MBC1_RAM_ENABLED_VALUE;
      break;

    case 1: /* 2000-3fff */
      e->mbc.rom_bank &= MBC1_ROM_BANK_LO_MASK;
      e->mbc.rom_bank |= value & MBC1_ROM_BANK_LO_MASK;
      /* Banks 0, 0x20, 0x40, 0x60 map to 1, 0x21, 0x41, 0x61 respectively. */
      if (e->mbc.rom_bank == 0) {
        e->mbc.rom_bank++;
      }
      break;

    case 2: /* 4000-5fff */
      value &= MBC1_BANK_HI_MASK;
      if (e->mbc.bank_mode == BANK_MODE_ROM) {
        e->mbc.rom_bank &= MBC1_ROM_BANK_LO_MASK;
        e->mbc.rom_bank |= value << MBC1_BANK_HI_SHIFT;
      } else {
        e->mbc.ram_bank = value;
      }
      break;

    case 3: { /* 6000-7fff */
      enum BankMode new_bank_mode = (enum BankMode)(value & 1);
      if (e->mbc.bank_mode != new_bank_mode) {
        if (new_bank_mode == BANK_MODE_ROM) {
          /* Use the ram bank bits as the high rom bank bits. */
          assert(e->mbc.rom_bank <= MBC1_ROM_BANK_LO_MASK);
          e->mbc.rom_bank |= e->mbc.ram_bank << MBC1_BANK_HI_SHIFT;
          e->mbc.ram_bank = 0;
        } else {
          /* Use the high rom bank bits as the ram bank bits. */
          e->mbc.ram_bank = e->mbc.rom_bank >> MBC1_BANK_HI_SHIFT;
          assert(e->mbc.ram_bank <= MBC1_BANK_HI_MASK);
          e->mbc.rom_bank &= MBC1_ROM_BANK_LO_MASK;
        }
        e->mbc.bank_mode = new_bank_mode;
      }
      break;
    }

    default:
      UNREACHABLE("invalid addr: 0x%04x\n", addr);
      break;
  }
}

struct MemoryMap s_rom_only_memory_map = {
  .read_rom_bank_switch = rom_only_read_rom_bank_switch,
  .write_rom = rom_only_write_rom,
};

struct MemoryMap s_mbc1_memory_map = {
  .read_rom_bank_switch = mbc1_read_rom_bank_switch,
  .write_rom = mbc1_write_rom,
};

enum Result get_memory_map(struct RomInfo* rom_info,
                           struct MemoryMap** out_memory_map,
                           struct Mbc* out_mbc) {
  switch (rom_info->cartridge_type) {
    case CARTRIDGE_TYPE_ROM_ONLY:
      *out_memory_map = &s_rom_only_memory_map;
      return OK;

    case CARTRIDGE_TYPE_MBC1:
    case CARTRIDGE_TYPE_MBC1_RAM:
    case CARTRIDGE_TYPE_MBC1_RAM_BATTERY:
      *out_memory_map = &s_mbc1_memory_map;
      out_mbc->rom_bank = 1;
      return OK;

    default:
      NOT_IMPLEMENTED("memory map for %s not implemented.\n",
                      get_cartridge_type_string(rom_info->cartridge_type));
  }
  return ERROR;
}

enum Result init_emulator(struct Emulator* e, struct RomData* rom_data) {
  ZERO_MEMORY(*e);
  e->rom_data = rom_data;
  CHECK(SUCCESS(get_rom_info(rom_data, &e->rom_info)));
#if 1
  print_rom_info(&e->rom_info);
#endif
  CHECK(SUCCESS(get_memory_map(&e->rom_info, &e->memory_map, &e->mbc)));
  return OK;
error:
  return ERROR;
}


uint8_t read_rom(struct Emulator* e, MaskedAddress addr) {
  return e->rom_data->data[addr];
}

uint8_t read_rom_bank_switch(struct Emulator* e, MaskedAddress addr) {
  return e->memory_map->read_rom_bank_switch(e, addr);
}

uint8_t read_vram(struct Emulator* e, MaskedAddress addr) {
  NOT_IMPLEMENTED("vram not implemented. Addr: 0x%04x\n", addr);
  return 0;
}

uint8_t read_external_ram(struct Emulator* e, MaskedAddress addr) {
  NOT_IMPLEMENTED("ext ram not implemented. Addr: 0x%04x\n", addr);
  return 0;
}

uint8_t read_work_ram(struct Emulator* e, MaskedAddress addr) {
  NOT_IMPLEMENTED("work ram not implemented. Addr: 0x%04x\n", addr);
  return 0;
}

uint8_t read_work_ram_bank_switch(struct Emulator* e,
                                     MaskedAddress addr) {
  NOT_IMPLEMENTED("work ram not implemented. Addr: 0x%04x\n", addr);
  return 0;
}

uint8_t read_hardware(struct Emulator* e, Address addr) {
  NOT_IMPLEMENTED("hardware not implemented. Addr: 0x%04x\n", addr);
  return 0;
}

uint8_t read_u8(struct Emulator* e, Address addr) {
  switch (addr >> 12) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
      return read_rom(e, addr & ADDR_MASK_16K);

    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
      return read_rom_bank_switch(e, addr & ADDR_MASK_16K);

    case 0x8:
    case 0x9:
      return read_vram(e, addr & ADDR_MASK_8K);

    case 0xA:
    case 0xB:
      return read_external_ram(e, addr & ADDR_MASK_8K);

    case 0xC:
    case 0xE:
      return read_work_ram(e, addr & ADDR_MASK_8K);

    case 0xD:
      return read_work_ram_bank_switch(e, addr & ADDR_MASK_8K);

    case 0xF:
      if (addr < 0xFE00) {
        return read_work_ram_bank_switch(e, addr & ADDR_MASK_8K);
      } else {
        return read_hardware(e, addr);
      }
      break;

    default:
      UNREACHABLE("read_u8: invalid address: 0x%04X\n", addr);
      break;
  }
}

uint16_t read_u16(struct Emulator* e, Address addr) {
  return read_u8(e, addr) | (read_u8(e, addr + 1) << 8);
}

void write_rom(struct Emulator* e, MaskedAddress addr, uint8_t value) {
  e->memory_map->write_rom(e, addr, value);
}

void write_vram(struct Emulator* e, MaskedAddress addr, uint8_t value) {
  NOT_IMPLEMENTED("write_vram not implemented. Addr: 0x%04x\n", addr);
}

void write_external_ram(struct Emulator* e,
                        MaskedAddress addr,
                        uint8_t value) {
  NOT_IMPLEMENTED("write_external_ram not implemented. Addr: 0x%04x\n", addr);
}

void write_work_ram(struct Emulator* e, MaskedAddress addr, uint8_t value) {
  NOT_IMPLEMENTED("write_work_ram not implemented. Addr: 0x%04x\n", addr);
}

void write_work_ram_bank_switch(struct Emulator* e,
                                MaskedAddress addr,
                                uint8_t value) {
  NOT_IMPLEMENTED("write_work_ram_bank_switch not implemented. Addr: 0x%04x\n",
                  addr);
}

void write_hardware(struct Emulator* e, MaskedAddress addr, uint8_t value) {
  NOT_IMPLEMENTED("write_hardware not implemented. Addr: 0x%04x\n", addr);
}

void write_u8(struct Emulator* e, Address addr, uint8_t value) {
  switch (addr >> 12) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
      return write_rom(e, addr & ADDR_MASK_32K, value);

    case 0x8:
    case 0x9:
      return write_vram(e, addr & ADDR_MASK_8K, value);

    case 0xA:
    case 0xB:
      return write_external_ram(e, addr & ADDR_MASK_8K, value);

    case 0xC:
    case 0xE:
      return write_work_ram(e, addr & ADDR_MASK_8K, value);

    case 0xD:
      return write_work_ram_bank_switch(e, addr & ADDR_MASK_8K, value);

    case 0xF:
      if (addr < 0xFE00) {
        return write_work_ram_bank_switch(e, addr & ADDR_MASK_8K, value);
      } else {
        return write_hardware(e, addr, value);
      }
      break;

    default:
      UNREACHABLE("write_u8: invalid address: 0x%04X\n", addr);
      break;
  }
}

uint8_t s_opcode_bytes[] = {
    /*       0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
    /* 00 */ 1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1,
    /* 10 */ 1, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    /* 20 */ 2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    /* 30 */ 2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    /* 40 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 50 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 60 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 70 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 80 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 90 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* a0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* b0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* c0 */ 1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 2, 3, 3, 2, 1,
    /* d0 */ 1, 1, 3, 0, 3, 1, 2, 1, 1, 1, 3, 0, 3, 0, 2, 1,
    /* e0 */ 2, 1, 1, 0, 0, 1, 2, 1, 2, 1, 3, 0, 0, 0, 2, 1,
    /* f0 */ 2, 1, 1, 1, 0, 1, 2, 1, 2, 1, 3, 1, 0, 0, 2, 1,
};

const char* s_opcode_mnemonic[] = {
        [0x00] = "NOP",
        [0x01] = "LD BC,%hu",
        [0x02] = "LD (BC),A",
        [0x03] = "INC BC",
        [0x04] = "INC B",
        [0x05] = "DEC B",
        [0x06] = "LD B,%hhu",
        [0x07] = "RLCA",
        [0x08] = "LD (%04hXH),SP",
        [0x09] = "ADD HL,BC",
        [0x0A] = "LD A,(BC)",
        [0x0B] = "DEC BC",
        [0x0C] = "INC C",
        [0x0D] = "DEC C",
        [0x0E] = "LD C,%hhu",
        [0x0F] = "RRCA",
        [0x10] = "STOP",
        [0x11] = "LD DE,%hu",
        [0x12] = "LD (DE),A",
        [0x13] = "INC DE",
        [0x14] = "INC D",
        [0x15] = "DEC D",
        [0x16] = "LD D,%hhu",
        [0x17] = "RLA",
        [0x18] = "JR %+hhd",
        [0x19] = "ADD HL,DE",
        [0x1A] = "LD A,(DE)",
        [0x1B] = "DEC DE",
        [0x1C] = "INC E",
        [0x1D] = "DEC E",
        [0x1E] = "LD E,%hhu",
        [0x1F] = "RRA",
        [0x20] = "JR NZ,%+hhd",
        [0x21] = "LD HL,%hu",
        [0x22] = "LDI (HL),A",
        [0x23] = "INC HL",
        [0x24] = "INC H",
        [0x25] = "DEC H",
        [0x26] = "LD H,%hhu",
        [0x27] = "DAA",
        [0x28] = "JR Z,%+hhd",
        [0x29] = "ADD HL,HL",
        [0x2A] = "LDI A,(HL)",
        [0x2B] = "DEC HL",
        [0x2C] = "INC L",
        [0x2D] = "DEC L",
        [0x2E] = "LD L,%hhu",
        [0x2F] = "CPL",
        [0x30] = "JR NC,%+hhd",
        [0x31] = "LD SP,%hu",
        [0x32] = "LDD (HL),A",
        [0x33] = "INC SP",
        [0x34] = "INC (HL)",
        [0x35] = "DEC (HL)",
        [0x36] = "LD (HL),%hhu",
        [0x37] = "SCF",
        [0x38] = "JR C,%+hhd",
        [0x39] = "ADD HL,SP",
        [0x3A] = "LDD A,(HL)",
        [0x3B] = "DEC SP",
        [0x3C] = "INC A",
        [0x3D] = "DEC A",
        [0x3E] = "LD A,%hhu",
        [0x3F] = "CCF",
        [0x40] = "LD B,B",
        [0x41] = "LD B,C",
        [0x42] = "LD B,D",
        [0x43] = "LD B,E",
        [0x44] = "LD B,H",
        [0x45] = "LD B,L",
        [0x46] = "LD B,(HL)",
        [0x47] = "LD B,A",
        [0x48] = "LD C,B",
        [0x49] = "LD C,C",
        [0x4A] = "LD C,D",
        [0x4B] = "LD C,E",
        [0x4C] = "LD C,H",
        [0x4D] = "LD C,L",
        [0x4E] = "LD C,(HL)",
        [0x4F] = "LD C,A",
        [0x50] = "LD D,B",
        [0x51] = "LD D,C",
        [0x52] = "LD D,D",
        [0x53] = "LD D,E",
        [0x54] = "LD D,H",
        [0x55] = "LD D,L",
        [0x56] = "LD D,(HL)",
        [0x57] = "LD D,A",
        [0x58] = "LD E,B",
        [0x59] = "LD E,C",
        [0x5A] = "LD E,D",
        [0x5B] = "LD E,E",
        [0x5C] = "LD E,H",
        [0x5D] = "LD E,L",
        [0x5E] = "LD E,(HL)",
        [0x5F] = "LD E,A",
        [0x60] = "LD H,B",
        [0x61] = "LD H,C",
        [0x62] = "LD H,D",
        [0x63] = "LD H,E",
        [0x64] = "LD H,H",
        [0x65] = "LD H,L",
        [0x66] = "LD H,(HL)",
        [0x67] = "LD H,A",
        [0x68] = "LD L,B",
        [0x69] = "LD L,C",
        [0x6A] = "LD L,D",
        [0x6B] = "LD L,E",
        [0x6C] = "LD L,H",
        [0x6D] = "LD L,L",
        [0x6E] = "LD L,(HL)",
        [0x6F] = "LD L,A",
        [0x70] = "LD (HL),B",
        [0x71] = "LD (HL),C",
        [0x72] = "LD (HL),D",
        [0x73] = "LD (HL),E",
        [0x74] = "LD (HL),H",
        [0x75] = "LD (HL),L",
        [0x76] = "HALT",
        [0x77] = "LD (HL),A",
        [0x78] = "LD A,B",
        [0x79] = "LD A,C",
        [0x7A] = "LD A,D",
        [0x7B] = "LD A,E",
        [0x7C] = "LD A,H",
        [0x7D] = "LD A,L",
        [0x7E] = "LD A,(HL)",
        [0x7F] = "LD A,A",
        [0x80] = "ADD A,B",
        [0x81] = "ADD A,C",
        [0x82] = "ADD A,D",
        [0x83] = "ADD A,E",
        [0x84] = "ADD A,H",
        [0x85] = "ADD A,L",
        [0x86] = "ADD A,(HL)",
        [0x87] = "ADD A,A",
        [0x88] = "ADC A,B",
        [0x89] = "ADC A,C",
        [0x8A] = "ADC A,D",
        [0x8B] = "ADC A,E",
        [0x8C] = "ADC A,H",
        [0x8D] = "ADC A,L",
        [0x8E] = "ADC A,(HL)",
        [0x8F] = "ADC A,A",
        [0x90] = "SUB B",
        [0x91] = "SUB C",
        [0x92] = "SUB D",
        [0x93] = "SUB E",
        [0x94] = "SUB H",
        [0x95] = "SUB L",
        [0x96] = "SUB (HL)",
        [0x97] = "SUB A",
        [0x98] = "SBC B",
        [0x99] = "SBC C",
        [0x9A] = "SBC D",
        [0x9B] = "SBC E",
        [0x9C] = "SBC H",
        [0x9D] = "SBC L",
        [0x9E] = "SBC (HL)",
        [0x9F] = "SBC A",
        [0xA0] = "AND B",
        [0xA1] = "AND C",
        [0xA2] = "AND D",
        [0xA3] = "AND E",
        [0xA4] = "AND H",
        [0xA5] = "AND L",
        [0xA6] = "AND (HL)",
        [0xA7] = "AND A",
        [0xA8] = "XOR B",
        [0xA9] = "XOR C",
        [0xAA] = "XOR D",
        [0xAB] = "XOR E",
        [0xAC] = "XOR H",
        [0xAD] = "XOR L",
        [0xAE] = "XOR (HL)",
        [0xAF] = "XOR A",
        [0xB0] = "OR B",
        [0xB1] = "OR C",
        [0xB2] = "OR D",
        [0xB3] = "OR E",
        [0xB4] = "OR H",
        [0xB5] = "OR L",
        [0xB6] = "OR (HL)",
        [0xB7] = "OR A",
        [0xB8] = "CP B",
        [0xB9] = "CP C",
        [0xBA] = "CP D",
        [0xBB] = "CP E",
        [0xBC] = "CP H",
        [0xBD] = "CP L",
        [0xBE] = "CP (HL)",
        [0xBF] = "CP A",
        [0xC0] = "RET NZ",
        [0xC1] = "POP BC",
        [0xC2] = "JP NZ,%04hXH",
        [0xC3] = "JP %04hXH",
        [0xC4] = "CALL NZ,%04hXH",
        [0xC5] = "PUSH BC",
        [0xC6] = "ADD A,%hhu",
        [0xC7] = "RST 0",
        [0xC8] = "RET Z",
        [0xC9] = "RET",
        [0xCA] = "JP Z,%04hXH",
        [0xCC] = "CALL Z,%04hXH",
        [0xCD] = "CALL %04hXH",
        [0xCE] = "ADC A,%hhu",
        [0xCF] = "RST 8H",
        [0xD0] = "RET NC",
        [0xD1] = "POP DE",
        [0xD2] = "JP NC,%04hXH",
        [0xD4] = "CALL NC,%04hXH",
        [0xD5] = "PUSH DE",
        [0xD6] = "SUB %hhu",
        [0xD7] = "RST 10H",
        [0xD8] = "RET C",
        [0xD9] = "RETI",
        [0xDA] = "JP C,%04hXH",
        [0xDC] = "CALL C,%04hXH",
        [0xDE] = "SBC A,%hhu",
        [0xDF] = "RST 18H",
        [0xE0] = "LD (FF%02hhXH),A",
        [0xE1] = "POP HL",
        [0xE2] = "LD (FF00H+C),A",
        [0xE5] = "PUSH HL",
        [0xE6] = "AND %hhu",
        [0xE7] = "RST 20H",
        [0xE8] = "ADD SP,%hhd",
        [0xE9] = "JP (HL)",
        [0xEA] = "LD (%04hXH),A",
        [0xEE] = "XOR %hhu",
        [0xEF] = "RST 28H",
        [0xF0] = "LD A,(FF%02hhXH)",
        [0xF1] = "POP AF",
        [0xF2] = "LD A,(FF00H+C)",
        [0xF3] = "DI",
        [0xF5] = "PUSH AF",
        [0xF6] = "OR %hhu",
        [0xF7] = "RST 30H",
        [0xF8] = "LD HL,SP + %hhd",
        [0xF9] = "LD SP,HL",
        [0xFA] = "LD A,(%04hXH)",
        [0xFB] = "EI",
        [0xFE] = "CP %hhu",
        [0xFF] = "RST 38H",
};

const char* s_cb_opcode_mnemonic[] = {
        [0x00] = "RLC B",      [0x01] = "RLC C",      [0x02] = "RLC D",
        [0x03] = "RLC E",      [0x04] = "RLC H",      [0x05] = "RLC L",
        [0x06] = "RLC (HL)",   [0x07] = "RLC A",      [0x08] = "RRC B",
        [0x09] = "RRC C",      [0x0A] = "RRC D",      [0x0B] = "RRC E",
        [0x0C] = "RRC H",      [0x0D] = "RRC L",      [0x0E] = "RRC (HL)",
        [0x0F] = "RRC A",      [0x10] = "RL B",       [0x11] = "RL C",
        [0x12] = "RL D",       [0x13] = "RL E",       [0x14] = "RL H",
        [0x15] = "RL L",       [0x16] = "RL (HL)",    [0x17] = "RL A",
        [0x18] = "RR B",       [0x19] = "RR C",       [0x1A] = "RR D",
        [0x1B] = "RR E",       [0x1C] = "RR H",       [0x1D] = "RR L",
        [0x1E] = "RR (HL)",    [0x1F] = "RR A",       [0x20] = "SLA B",
        [0x21] = "SLA C",      [0x22] = "SLA D",      [0x23] = "SLA E",
        [0x24] = "SLA H",      [0x25] = "SLA L",      [0x26] = "SLA (HL)",
        [0x27] = "SLA A",      [0x28] = "SRA B",      [0x29] = "SRA C",
        [0x2A] = "SRA D",      [0x2B] = "SRA E",      [0x2C] = "SRA H",
        [0x2D] = "SRA L",      [0x2E] = "SRA (HL)",   [0x2F] = "SRA A",
        [0x30] = "SWAP B",     [0x31] = "SWAP C",     [0x32] = "SWAP D",
        [0x33] = "SWAP E",     [0x34] = "SWAP H",     [0x35] = "SWAP L",
        [0x36] = "SWAP (HL)",  [0x37] = "SWAP A",     [0x38] = "SWAP B",
        [0x39] = "SWAP C",     [0x3A] = "SWAP D",     [0x3B] = "SWAP E",
        [0x3C] = "SWAP H",     [0x3D] = "SWAP L",     [0x3E] = "SWAP (HL)",
        [0x3F] = "SWAP A",     [0x40] = "BIT 0,B",    [0x41] = "BIT 0,C",
        [0x42] = "BIT 0,D",    [0x43] = "BIT 0,E",    [0x44] = "BIT 0,H",
        [0x45] = "BIT 0,L",    [0x46] = "BIT 0,(HL)", [0x47] = "BIT 0,A",
        [0x48] = "BIT 1,B",    [0x49] = "BIT 1,C",    [0x4A] = "BIT 1,D",
        [0x4B] = "BIT 1,E",    [0x4C] = "BIT 1,H",    [0x4D] = "BIT 1,L",
        [0x4E] = "BIT 1,(HL)", [0x4F] = "BIT 1,A",    [0x50] = "BIT 2,B",
        [0x51] = "BIT 2,C",    [0x52] = "BIT 2,D",    [0x53] = "BIT 2,E",
        [0x54] = "BIT 2,H",    [0x55] = "BIT 2,L",    [0x56] = "BIT 2,(HL)",
        [0x57] = "BIT 2,A",    [0x58] = "BIT 3,B",    [0x59] = "BIT 3,C",
        [0x5A] = "BIT 3,D",    [0x5B] = "BIT 3,E",    [0x5C] = "BIT 3,H",
        [0x5D] = "BIT 3,L",    [0x5E] = "BIT 3,(HL)", [0x5F] = "BIT 3,A",
        [0x60] = "BIT 4,B",    [0x61] = "BIT 4,C",    [0x62] = "BIT 4,D",
        [0x63] = "BIT 4,E",    [0x64] = "BIT 4,H",    [0x65] = "BIT 4,L",
        [0x66] = "BIT 4,(HL)", [0x67] = "BIT 4,A",    [0x68] = "BIT 5,B",
        [0x69] = "BIT 5,C",    [0x6A] = "BIT 5,D",    [0x6B] = "BIT 5,E",
        [0x6C] = "BIT 5,H",    [0x6D] = "BIT 5,L",    [0x6E] = "BIT 5,(HL)",
        [0x6F] = "BIT 5,A",    [0x70] = "BIT 6,B",    [0x71] = "BIT 6,C",
        [0x72] = "BIT 6,D",    [0x73] = "BIT 6,E",    [0x74] = "BIT 6,H",
        [0x75] = "BIT 6,L",    [0x76] = "BIT 6,(HL)", [0x77] = "BIT 6,A",
        [0x78] = "BIT 7,B",    [0x79] = "BIT 7,C",    [0x7A] = "BIT 7,D",
        [0x7B] = "BIT 7,E",    [0x7C] = "BIT 7,H",    [0x7D] = "BIT 7,L",
        [0x7E] = "BIT 7,(HL)", [0x7F] = "BIT 7,A",    [0x80] = "RES 0,B",
        [0x81] = "RES 0,C",    [0x82] = "RES 0,D",    [0x83] = "RES 0,E",
        [0x84] = "RES 0,H",    [0x85] = "RES 0,L",    [0x86] = "RES 0,(HL)",
        [0x87] = "RES 0,A",    [0x88] = "RES 1,B",    [0x89] = "RES 1,C",
        [0x8A] = "RES 1,D",    [0x8B] = "RES 1,E",    [0x8C] = "RES 1,H",
        [0x8D] = "RES 1,L",    [0x8E] = "RES 1,(HL)", [0x8F] = "RES 1,A",
        [0x90] = "RES 2,B",    [0x91] = "RES 2,C",    [0x92] = "RES 2,D",
        [0x93] = "RES 2,E",    [0x94] = "RES 2,H",    [0x95] = "RES 2,L",
        [0x96] = "RES 2,(HL)", [0x97] = "RES 2,A",    [0x98] = "RES 3,B",
        [0x99] = "RES 3,C",    [0x9A] = "RES 3,D",    [0x9B] = "RES 3,E",
        [0x9C] = "RES 3,H",    [0x9D] = "RES 3,L",    [0x9E] = "RES 3,(HL)",
        [0x9F] = "RES 3,A",    [0xA0] = "RES 4,B",    [0xA1] = "RES 4,C",
        [0xA2] = "RES 4,D",    [0xA3] = "RES 4,E",    [0xA4] = "RES 4,H",
        [0xA5] = "RES 4,L",    [0xA6] = "RES 4,(HL)", [0xA7] = "RES 4,A",
        [0xA8] = "RES 5,B",    [0xA9] = "RES 5,C",    [0xAA] = "RES 5,D",
        [0xAB] = "RES 5,E",    [0xAC] = "RES 5,H",    [0xAD] = "RES 5,L",
        [0xAE] = "RES 5,(HL)", [0xAF] = "RES 5,A",    [0xB0] = "RES 6,B",
        [0xB1] = "RES 6,C",    [0xB2] = "RES 6,D",    [0xB3] = "RES 6,E",
        [0xB4] = "RES 6,H",    [0xB5] = "RES 6,L",    [0xB6] = "RES 6,(HL)",
        [0xB7] = "RES 6,A",    [0xB8] = "RES 7,B",    [0xB9] = "RES 7,C",
        [0xBA] = "RES 7,D",    [0xBB] = "RES 7,E",    [0xBC] = "RES 7,H",
        [0xBD] = "RES 7,L",    [0xBE] = "RES 7,(HL)", [0xBF] = "RES 7,A",
        [0xC0] = "SET 0,B",    [0xC1] = "SET 0,C",    [0xC2] = "SET 0,D",
        [0xC3] = "SET 0,E",    [0xC4] = "SET 0,H",    [0xC5] = "SET 0,L",
        [0xC6] = "SET 0,(HL)", [0xC7] = "SET 0,A",    [0xC8] = "SET 1,B",
        [0xC9] = "SET 1,C",    [0xCA] = "SET 1,D",    [0xCB] = "SET 1,E",
        [0xCC] = "SET 1,H",    [0xCD] = "SET 1,L",    [0xCE] = "SET 1,(HL)",
        [0xCF] = "SET 1,A",    [0xD0] = "SET 2,B",    [0xD1] = "SET 2,C",
        [0xD2] = "SET 2,D",    [0xD3] = "SET 2,E",    [0xD4] = "SET 2,H",
        [0xD5] = "SET 2,L",    [0xD6] = "SET 2,(HL)", [0xD7] = "SET 2,A",
        [0xD8] = "SET 3,B",    [0xD9] = "SET 3,C",    [0xDA] = "SET 3,D",
        [0xDB] = "SET 3,E",    [0xDC] = "SET 3,H",    [0xDD] = "SET 3,L",
        [0xDE] = "SET 3,(HL)", [0xDF] = "SET 3,A",    [0xE0] = "SET 4,B",
        [0xE1] = "SET 4,C",    [0xE2] = "SET 4,D",    [0xE3] = "SET 4,E",
        [0xE4] = "SET 4,H",    [0xE5] = "SET 4,L",    [0xE6] = "SET 4,(HL)",
        [0xE7] = "SET 4,A",    [0xE8] = "SET 5,B",    [0xE9] = "SET 5,C",
        [0xEA] = "SET 5,D",    [0xEB] = "SET 5,E",    [0xEC] = "SET 5,H",
        [0xED] = "SET 5,L",    [0xEE] = "SET 5,(HL)", [0xEF] = "SET 5,A",
        [0xF0] = "SET 6,B",    [0xF1] = "SET 6,C",    [0xF2] = "SET 6,D",
        [0xF3] = "SET 6,E",    [0xF4] = "SET 6,H",    [0xF5] = "SET 6,L",
        [0xF6] = "SET 6,(HL)", [0xF7] = "SET 6,A",    [0xF8] = "SET 7,B",
        [0xF9] = "SET 7,C",    [0xFA] = "SET 7,D",    [0xFB] = "SET 7,E",
        [0xFC] = "SET 7,H",    [0xFD] = "SET 7,L",    [0xFE] = "SET 7,(HL)",
        [0xFF] = "SET 7,A",
};

/* Returns the opcode size. */
uint8_t print_instruction(struct Emulator* e, Address addr) {
  uint8_t opcode = read_u8(e, addr);
  if (opcode == 0xcb) {
    uint8_t cb_opcode = read_u8(e, addr + 1);
    const char* mnemonic = s_cb_opcode_mnemonic[cb_opcode];
    printf("0x%04x: cb %02x     %s\n", addr, cb_opcode, mnemonic);
    return 2;
  } else {
    char buffer[64];
    const char* mnemonic = s_opcode_mnemonic[opcode];
    uint8_t bytes = s_opcode_bytes[opcode];
    switch (bytes) {
      case 0:
        printf("0x%04x: %02x        *INVALID*\n", addr, opcode);
        break;
      case 1:
        printf("0x%04x: %02x        %s\n", addr, opcode, mnemonic);
        break;
      case 2: {
        uint8_t byte = read_u8(e, addr + 1);
        snprintf(buffer, sizeof(buffer), mnemonic, byte);
        printf("0x%04x: %02x %02x     %s\n", addr, opcode, byte, buffer);
        break;
      }
      case 3: {
        uint8_t byte1 = read_u8(e, addr + 1);
        uint8_t byte2 = read_u8(e, addr + 2);
        uint16_t word = (byte2 << 8) | byte1;
        snprintf(buffer, sizeof(buffer), mnemonic, word);
        printf("0x%04x: %02x %02x %02x  %s\n", addr, opcode, byte1, byte2,
               buffer);
        break;
      }
      default:
        UNREACHABLE("invalid opcode byte length.\n");
        break;
    }
    return bytes;
  }
}

int main(int argc, char** argv) {
  --argc; ++argv;
  CHECK_MSG(argc == 1, "no rom file given.\n");
  struct RomData rom_data;
  CHECK(SUCCESS(read_rom_data_from_file(argv[0], &rom_data)));
  struct Emulator emulator;
  ZERO_MEMORY(emulator);
  CHECK(SUCCESS(init_emulator(&emulator, &rom_data)));
  int i;
  Address a = read_u16(&emulator, 1);
  for (i = 0; i < 30; ++i) {
    a += print_instruction(&emulator, a);
  }
  return 0;
error:
  return 1;
}
