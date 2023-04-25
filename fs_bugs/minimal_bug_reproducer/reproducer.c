#include "mounts.h"
#include "fstestutil.h"

#define MAX_LINE_LENGTH 256

int main(int argc, char **argv)
{
    if (argc < 8) {
        fprintf(stderr, "Usage %s seqlog fs1 fs2 mp1 mp2 dev1 dev2\n", argv[0]);
        exit(1);
    }

    char *seqlog = argv[1];
    char *fs1 = argv[2];
    char *fs2 = argv[3];
    char *mp1 = argv[4];
    char *mp2 = argv[5];
    char *dev1 = argv[6];
    char *dev2 = argv[7];

    FILE *fp;
    char line[MAX_LINE_LENGTH];
    long offset, current_pos;
    bool found_checkpoint = 0;

    // Read sequence file from bottom to top
    fp = fopen(seqlog, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open sequence file\n");
        exit(1);
    }

    // Move file pointer to the end of file
    fseek(fp, 0L, SEEK_END);

    // Get the current position of the file pointer
    current_pos = ftell(fp);

    // current_pos: 479
    printf("current_pos: %ld\n", current_pos);

    // Read the file line by line in reverse order
    while (current_pos) {
        // Move file pointer to the beginning of the last line
        offset = ftell(fp);
        fseek(fp, offset - 2, SEEK_SET);

        // Read the last line
        fgets(line, MAX_LINE_LENGTH, fp);

        printf("line: %s\n", line);

        // Check if the line contains the "checkpoint" string
        if (strstr(line, "checkpoint")) {
            printf("Found checkpoint.\n");
            found_checkpoint = 1;
            break;
        }

        // Move file pointer to the beginning of the previous line
        current_pos -= strlen(line);
        fseek(fp, current_pos, SEEK_SET);
    }

    if (found_checkpoint) {
        // Move file pointer to the next line after the checkpoint
        fgets(line, MAX_LINE_LENGTH, fp);

        // Read the file from the checkpoint onwards
        while (fgets(line, MAX_LINE_LENGTH, fp) != NULL) {
            // Process the line as needed
            printf("%s", line);
        }
    }
    
    // Close the file
    fclose(fp);

    return 0;
}
