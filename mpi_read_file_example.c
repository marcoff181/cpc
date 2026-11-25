#include <mpi.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include "../sequential/fm85.h"
#include "../sequential/fm85Merging.h"


// The amount of bytes each process reads from its own file chunk
#define READ_BLOCK_SIZE 1024 * 1024

#define HASH_SEED 37



int main(int argc, char **argv) {

    MPI_Init(&argc, &argv);

    int my_rank, mpi_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_procs);

    if (argc != 2) {
        if (my_rank == 0) {
            fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        }
        MPI_Finalize();
        return -1;
    }

    // MPI_Barrier(MPI_COMM_WORLD); // ?
    double t0 = MPI_Wtime();

    MPI_File file;
    if (MPI_File_open(MPI_COMM_WORLD, argv[1], MPI_MODE_RDONLY, MPI_INFO_NULL, &file) != MPI_SUCCESS) {
        if (my_rank == 0) {
            fprintf(stderr, "Could not open %s\n", argv[1]);
        }
        MPI_Abort(MPI_COMM_WORLD, -2);
    }

    MPI_Offset file_size;
    if (MPI_File_get_size(file, &file_size) != MPI_SUCCESS) {
        if (my_rank == 0) {
            fprintf(stderr, "MPI_File_get_size: something went wrong!\n");
        }
        MPI_Abort(MPI_COMM_WORLD, -3);
    }

    if (mpi_procs > file_size) {
        if (my_rank == 0) {
            fprintf(stderr, "Too many processes for such a small file!\n");
        }
        MPI_Finalize();
        return -1;
    }

    const MPI_Offset chunk_size = file_size / mpi_procs;
    const MPI_Offset leftover = file_size % mpi_procs;

    /* Split the `leftover` bytes of the previous division among the first
    `leftover` processes */
    const MPI_Offset start = my_rank * chunk_size + (my_rank < leftover ? my_rank : leftover);
    const MPI_Offset length = chunk_size + (my_rank < leftover ? 1 : 0);

    /* Number of entries in the file. Use `long long`, which should always be
    at least 64-bits long, instead of `long` */
    long long count = 0;

    /** TODO: */
    fm85Init();
    Short lgK = 16;
    FM85 *sketch = fm85Make(lgK);

    /* Allocate a buffer to read one memory block at a time from your own
    file chunk */
    char *buffer = malloc(READ_BLOCK_SIZE + INET_ADDRSTRLEN + 1);
    if (NULL == buffer) {
        fprintf(stderr, "Process %d could not allocate buffer\n", my_rank);
        MPI_Abort(MPI_COMM_WORLD, -4);
    }

    // Loop through your own file chunk
    const MPI_Offset end = start + length;
    MPI_Offset offset = start;
    while (offset < end) {
        // Make sure you don't attempt to read more bytes than the buffer's
        MPI_Offset remaining = end - offset;
        int block_length = (remaining > READ_BLOCK_SIZE) ? READ_BLOCK_SIZE : (int)remaining;
        MPI_Offset block_end = offset + block_length;

        /* Include additional byte before the current block in order to
        detect whether the first IP has been trimmed */
        int before = (offset > 0) ? 1 : 0;

        /* Determine how many additional bytes to include to read IPs which
        might have been trimmed */
        MPI_Offset tail = file_size - block_end;
        int after = (tail > INET_ADDRSTRLEN - 1) ? INET_ADDRSTRLEN - 1 : (int)tail;

        int requested_bytes = before + block_length + after;

        MPI_Status status;
        int received;
        if (
            MPI_File_read_at(file, offset - before, buffer, requested_bytes, MPI_BYTE, &status) != MPI_SUCCESS ||
            MPI_Get_count(&status, MPI_BYTE, &received) != MPI_SUCCESS ||
            received != requested_bytes
        ) {
            fprintf(stderr, "Process %d could not read at byte %lld\n", my_rank, (long long)offset);
            MPI_Abort(MPI_COMM_WORLD, -5);
        }

        char *line = buffer + before;
        char *line_end = line + block_length;
        char *read_end = line_end + after;

        /* If the block starts inside an IPv4, the previous iteration takes 
        care of it; go to the next address */
        if (before && buffer[0] != '\n') {
            char *newline = memchr(line, '\n', (size_t)(line_end - line));
            line = (newline != NULL) ? newline + 1 : line_end;
        }

        // Loop through the current block
        while (line < line_end) {
            char *newline = memchr(line, '\n', (size_t)(read_end - line));
            if (NULL == newline) {
                MPI_Abort(MPI_COMM_WORLD, -6);
            }

            // Set the new-line char to '\0', so the IPv4 parser will stop
            *newline = '\0';
            struct in_addr ip;
            if (inet_pton(AF_INET, line, &ip) != 1) { // AF_INET means "parse as IPv4"
                fprintf(stderr, "Process %d failed at parsing\n", my_rank);
                MPI_Abort(MPI_COMM_WORLD, -7);
            }

            /** TODO: do sketch stuff */
            /*
            XXH128_hash_t hash = XXH3_128bits_withSeed(&ip, sizeof(ip), HASH_SEED);
            fm85Update(sketch, hash.low64, hash.high64);
            */

            count++;
            line = newline + 1;
        }

        offset = block_end;
    }

    // Don't forget the chores!!!
    free(buffer);
    MPI_File_close(&file);

    /** TODO: do sketch stuff */
    if (my_rank == 0) {
        // Prepare the unioner...
        UG85 *union_gadget = ug85Make(lgK);
        /* ... and merge your sketch first. From now on, the local sketch is
        used as buffer */
        ug85MergeInto(union_gadget, sketch);

        for (int i = 1; i < mpi_procs; i++) {
            MPI_Recv(sketch->slidingWindow);
        }



        ug85Free(union_gadget);
    } else {
        /* Compress before sending? */
        MPI_Send(sketch->);
    }

    long long total = 0;
    MPI_Reduce(&count, &total, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    double elapsed = MPI_Wtime() - t0;

    if (my_rank == 0) {
        MPI_Reduce(MPI_IN_PLACE, &elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        printf("Total number of entries in %s: %lld\n", argv[1], total);
        printf("Elapsed time: %.6f s\n", elapsed);
    } else {
        MPI_Reduce(&elapsed, NULL, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
}
