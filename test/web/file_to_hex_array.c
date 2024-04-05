#include <stdio.h>
#include <stdlib.h>

void convert_file_to_hex_array(const char* input_file, const char* array_name, const char* output_file) {
    FILE *in_file, *out_file;
    unsigned char byte;
    int byte_count = 0;

    in_file = fopen(input_file, "rb");
    if (in_file == NULL) {
        fprintf(stderr, "Error: cannot open input file %s\n", input_file);
        exit(EXIT_FAILURE);
    }

    out_file = fopen(output_file, "w");
    if (out_file == NULL) {
        fprintf(stderr, "Error: cannot open output file %s\n", output_file);
        exit(EXIT_FAILURE);
    }

    fprintf(out_file, "const char %s[] PROGMEM = {\n", array_name);
    while (fread(&byte, sizeof(byte), 1, in_file) == 1) {
        fprintf(out_file, "0x%02x, ", byte);
        byte_count++;

        if (byte_count % 12 == 0) {
            fprintf(out_file, "\n");
        }
    }
    fprintf(out_file, "};\n");

    fclose(in_file);
    fclose(out_file);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input_file> <array_name> <output_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    convert_file_to_hex_array(argv[1], argv[2], argv[3]);
    return 0;
}

