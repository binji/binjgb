#ifndef BINJGB_PRINTER_H_
#define BINJGB_PRINTER_H_

#include "common.h"
#include "emulator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRINTER_MAX_DATA_LENGTH 0x280
#define PRINTER_DATA_SIZE 0x280

struct Emulator;

enum {
    PRINTER_STATE_MAGIC1,
    PRINTER_STATE_MAGIC2,
    PRINTER_STATE_ID,
    PRINTER_STATE_COMPRESSION,
    PRINTER_STATE_LENGTH,
    // PRINTER_STATE_LENGTH_LOW,
    // PRINTER_STATE_LENGTH_HIGH,
    PRINTER_STATE_DATA,
    // PRINTER_STATE_CHECKSUM,
    PRINTER_STATE_CHECKSUM_LOW,
    PRINTER_STATE_CHECKSUM_HIGH,
    PRINTER_STATE_KEEPALIVE,
    PRINTER_STATE_STATUS,
} PrinterState;

enum {
    PRINTER_INIT_COMMAND = 0x01,
    PRINTER_PRINT_COMMAND = 0x02,
    PRINTER_DATA_COMMAND = 0x04,
    PRINTER_BREAK_COMMAND = 0x08,
    PRINTER_NUL_COMMAND = 0x0F,
} PrinterCommand;

typedef enum {
    PRINTER_STATUS_NONE           = 0x00,

    PRINTER_STATUS_CHECKSUM_ERROR = 0x01,
    PRINTER_STATUS_BUSY           = 0x02,
    PRINTER_STATUS_DATA_FULL      = 0x04,
    PRINTER_STATUS_UNPROCESSED    = 0x08,

    PRINTER_STATUS_PACKET_ERROR   = 0x10,
    PRINTER_STATUS_PAPER_JAM      = 0x20,
    PRINTER_STATUS_OTHER_ERROR    = 0x40,
    PRINTER_STATUS_LOW_BATTERY    = 0x80
} PrinterStatus;

typedef struct
{
    uint8_t current_state;

    uint8_t command;
    Bool compression;
    uint16_t data_length;
    uint8_t data[PRINTER_MAX_DATA_LENGTH];
    uint16_t checksum;

    uint8_t status;
    uint8_t byte_to_send;

    uint16_t current_data_length;
    uint8_t length_bytes_received;
    uint8_t dummy_count;

    uint8_t dot_data[PRINTER_MAX_DATA_LENGTH * 10];
    uint16_t dot_data_length;

    uint8_t bits_received;
    uint8_t byte_being_received;
    Bool bit_to_send;
} Printer;

typedef void (*PrinterDoneCallaback)(uint32_t * buffer,
                                     uint8_t height,
                                     uint8_t top_margin,
                                     uint8_t bottom_margin,
                                     uint8_t exposure);

PrinterDoneCallaback printer_done_cb;

void connect_printer(Emulator* e, PrinterDoneCallaback printer_done);
void disconnect_printer(Emulator* e);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_PRINTER_H_ */
