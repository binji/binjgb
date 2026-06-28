#include "printer.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void handle_command(Printer *p)
{
    switch (p->command) {
        case PRINTER_INIT_COMMAND:
            p->status = PRINTER_STATUS_NONE;
            p->dot_data_length = 0;
            break;
        case PRINTER_PRINT_COMMAND:
            if (p->data_length == 4) {
                printf("PRINT COMMAND\n");

                p->status = PRINTER_STATUS_BUSY | PRINTER_STATUS_DATA_FULL; // Printing

                static const uint32_t print_colors[4] = {
                    0xFFFFFFFF,
                    0xFFAAAAAA,
                    0xFF555555,
                    0xFF000000
                };

                uint8_t palette = p->data[2];

                uint8_t colors[4] = {
                    (palette >> 0) & 3,
                    (palette >> 2) & 3,
                    (palette >> 4) & 3,
                    (palette >> 6) & 3
                };

                int tile_count = p->dot_data_length / 16;
                int tile_rows  = tile_count / 20;

                uint32_t *buffer = xmalloc(160 * 144 * sizeof(uint32_t));
                if (!buffer) break;

                for (int ty = 0; ty < tile_rows; ty++) {
                    for (int tx = 0; tx < 20; tx++) {
                        int tile = tx + ty * 20;
                        for (int row = 0; row < 8; row++) {
                            int n = tile * 16 + row * 2;
                            uint8_t a = p->dot_data[n];
                            uint8_t b = p->dot_data[n + 1];
                            for (int x = 0; x < 8; x++) {
                                uint8_t bit = (0x80 >> x);
                                uint8_t pixel =
                                    ((a & bit) ? 1 : 0) |
                                    ((b & bit) ? 2 : 0);
                                buffer[(tx * 8 + x) + (ty * 8 + row) * 160] =
                                    print_colors[colors[pixel]];
                            }
                        }
                    }
                }

                if (printer_done_cb) {
                    printer_done_cb(buffer, 
                                    tile_rows * 8,  // height
                                    p->data[1] >> 4, // margin top
                                    p->data[1] & 7,  // margin bottom
                                    p->data[3] & 0x7F); // exposure
                }

                xfree(buffer);
                p->dot_data_length = 0;
 
            }
            break;
        case PRINTER_DATA_COMMAND:
            if (p->data_length > 0) {
                p->status = PRINTER_STATUS_UNPROCESSED;

                size_t dst = p->dot_data_length;
                size_t i = 0;
                if (p->compression) {
                    while (i < p->data_length) {
                        uint8_t data = p->data[i++];
                        if (data & 0x80) {
                            size_t length = (data & 0x7F) + 2;
                            uint8_t value = p->data[i++]; 
                            while (length--) {
                                if (dst < sizeof(p->dot_data)) {
                                    p->dot_data[dst] = value;
                                }
                                dst++;
                            }
                        } else {
                            size_t length = (data & 0x7F) + 1;
                            while (length--) {
                                if (i < p->data_length) {
                                    uint8_t value = p->data[i++];
                                    if (dst < sizeof(p->dot_data)) {
                                        p->dot_data[dst] = value;
                                    }
                                }
                                dst++;
                            }
                        }
                    }
                } else {
                    while (i < p->data_length) {
                        uint8_t value = p->data[i++];
                        if (dst < sizeof(p->dot_data)) {
                            p->dot_data[dst] = value;
                        }
                        dst++;
                    }
                }
                p->dot_data_length = dst;
            }
            break;
        case PRINTER_BREAK_COMMAND:
            // Not implemented
            break;
        case PRINTER_NUL_COMMAND:
            p->status |= 0;
            break;
        default:
            printf("Invalid command sent to printer: 0x%02x\n", p->command);
            break;
    }
}

static void byte_receive_completed(Printer *p, uint8_t value)
{
    switch (p->current_state) {
        case PRINTER_STATE_MAGIC1:
            printf("MAGIC 1: 0x%02x\n", value);
            
            if (value == 0x88) {
                p->status &= ~1;
                p->checksum = 0;

                p->byte_to_send = 0x00;
                p->current_state = PRINTER_STATE_MAGIC2;                
            }
            break;
            
        case PRINTER_STATE_MAGIC2:
            printf("MAGIC 2: 0x%02x\n", value);
            
            if (value == 0x33) {
                p->byte_to_send = 0x00;
                p->current_state = PRINTER_STATE_ID;
            } else {
                p->byte_to_send = 0x00;
                p->current_state = PRINTER_STATE_MAGIC1;
            }
            break;
        case PRINTER_STATE_ID:
            printf("COMMAND 0x%02x RECEIVED\n", value);

            p->command = value & 0xF;

            p->checksum += value;


            p->byte_to_send = 0x00;
            p->current_state = PRINTER_STATE_COMPRESSION;
            break;
        case PRINTER_STATE_COMPRESSION:
            printf("COMPRESSION 0x%02x\n", value);

            p->compression = value & 1;

            p->checksum += value;

            p->byte_to_send = 0x00;
            p->current_state = PRINTER_STATE_LENGTH;
            break;
        case PRINTER_STATE_LENGTH:
            if (p->length_bytes_received == 0) {
                printf("LENGTH1 0x%02x\n", value);
                p->length_bytes_received = 1;

                p->data_length = value;
                p->checksum += value;

                p->byte_to_send = 0x00;
            } else {
                printf("LENGTH2 0x%02x\n", value);
                p->length_bytes_received = 0;

                p->data_length |= (value & 3) << 8;
                p->checksum += value;

                if (p->data_length == 0) {
                    p->byte_to_send = 0x00;
                    p->current_state = PRINTER_STATE_CHECKSUM_LOW;
                } else {
                    p->current_data_length = 0;

                    p->byte_to_send = 0x00;
                    p->current_state = PRINTER_STATE_DATA;
                }
            }
            break;
        case PRINTER_STATE_DATA: 
            printf("DATA 0x%02x\n", value);
           
            if (p->current_data_length < PRINTER_MAX_DATA_LENGTH) {
                p->data[p->current_data_length++] = value;                
                p->checksum += value;
            }

            if (p->data_length == p->current_data_length) {
                p->byte_to_send = 0x00;
                p->current_state = PRINTER_STATE_CHECKSUM_LOW;
            }
            break;            
        case PRINTER_STATE_CHECKSUM_LOW:
            printf("CHECKSUM1 0x%02x\n", value);

            p->checksum ^= value;

            p->byte_to_send = 0x00;
            p->current_state = PRINTER_STATE_CHECKSUM_HIGH;
            break;
        case PRINTER_STATE_CHECKSUM_HIGH:
            printf("CHECKSUM2 0x%02x\n", value);

            p->checksum ^= value << 8;
            if (p->checksum) {
                printf("CHECKSUM ERROR\n");
                // Checksum error
                p->status |= 0x1;
            } else { 
                printf("CHECKSUM OK\n");
                p->status &= ~0x1; 
            }

            // TODO: Shouldn't this be returned after the next byte is received?
            p->byte_to_send = 0x81;

            p->current_state = PRINTER_STATE_KEEPALIVE;

            break;

        case PRINTER_STATE_KEEPALIVE:

            if (value == 0x00) {

                if ((p->command) == PRINTER_INIT_COMMAND) {
                    p->byte_to_send = PRINTER_STATUS_NONE;
                } else {
                    if (p->status == 0x06) {
                        printf("PRINT DONE!\n");
                        p->status = 0x04; /* Done */
                    }
                    p->byte_to_send = p->status;
                }
                p->current_state = PRINTER_STATE_STATUS;
            }

            break;

        case PRINTER_STATE_STATUS:
            handle_command(p);
    
            p->byte_to_send = p->status;

            p->current_state = PRINTER_STATE_MAGIC1;
    }
    printf("   > BYTE 0x%02x, STATUS: 0x%02x\n", p->byte_to_send, p->status);
}

static Bool serial_message(void *ctx, Bool value)
{
    Printer *p = (Printer *)ctx;

    p->bit_to_send = (p->byte_to_send & 0x80) >> 7;
    p->byte_to_send <<= 1;

    p->byte_being_received <<= 1;
    p->byte_being_received |= value;
    p->bits_received++;

    if (p->bits_received == 8) {
        
        byte_receive_completed(p, p->byte_being_received);

        p->bits_received = 0;
        p->byte_being_received = 0;
    }

    return p->bit_to_send;
}

void connect_printer(Emulator* e, PrinterDoneCallaback printer_done)
{
    printer_done_cb = printer_done;

    emulator_set_serial_message_callback(e, serial_message);
    emulator_set_accessory(e, ACCESSORY_PRINTER);
}

void disconnect_printer(Emulator* e) {
    printer_done_cb = NULL;
    emulator_set_serial_message_callback(e, NULL);
    emulator_set_accessory(e, ACCESSORY_NONE);
}
