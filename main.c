#include <mpi.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>



int main(void) {

    MPI_Init(NULL, NULL);

    int rank, w_size;
    MPI_Comm_size(MPI_COMM_WORLD, &w_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // exactly 2 processes allowed
    if (w_size != 2) {
        if (rank == 0) {
            fprintf(stderr, "Exactly 2 processes are allowed (specified %d instead)\n", w_size);
        }
        MPI_Finalize();
        return 1;
    }

    // buffer data (# of bytes to send)
    char *buffer = NULL;
    int len;

    // instant to start measuring time, instant to stop measuring time
    double ti, tf;

    const int ITERATIONS = 100;
    const int BYTES_EXP = 30;

    double send_total, recv_total, roundtrip_total;
    double avg_send_bandwidth, avg_recv_bandwidth, avg_roundtrip_bandwidth;

    if (rank == 0) {
        printf("BYTES\tAVG SEND BANDWIDTH\tAVG RECV BANDWIDTH\tAVG ROUNDTRIP BANDWIDTH\n");
    }

    // change buffer size (bandwith test)
    for (int exp = 0; exp <= BYTES_EXP; exp++) {

        // prepare the buffer
        len = (int)pow(2, exp);
        buffer = malloc(len);

        // prepare sums to calculate avg
        send_total = 0;
        recv_total = 0;
        roundtrip_total = 0;

        // iterate over the same test (statistical approach)
        for (int i = 0; i < ITERATIONS; i++) {

            MPI_Barrier(MPI_COMM_WORLD);
            if (rank == 0) {
                ti = MPI_Wtime();
                MPI_Send(buffer, len, MPI_CHAR, 1, 0, MPI_COMM_WORLD);
                tf = MPI_Wtime();

                send_total += tf - ti;
            } else {
                ti = MPI_Wtime();
                MPI_Recv(buffer, len, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                tf = MPI_Wtime();
                recv_total += tf - ti;
            }

            MPI_Barrier(MPI_COMM_WORLD);
            if (rank == 0) {
                ti = MPI_Wtime();
                MPI_Send(buffer, len, MPI_CHAR, 1, 0, MPI_COMM_WORLD);
                MPI_Recv(buffer, len, MPI_CHAR, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                tf = MPI_Wtime();
                roundtrip_total += tf - ti;
            } else {
                MPI_Recv(buffer, len, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send(buffer, len, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            }
        }

        avg_send_bandwidth = (double)ITERATIONS * len / send_total;
        avg_recv_bandwidth = (double)ITERATIONS * len / recv_total;
        avg_roundtrip_bandwidth = 2.0 * len * ITERATIONS / roundtrip_total;

        if (rank == 0) {
            MPI_Recv(&avg_recv_bandwidth, 1, MPI_DOUBLE, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            printf("%d\t%lf\t%lf\t%lf\n", len, avg_send_bandwidth, avg_recv_bandwidth, avg_roundtrip_bandwidth);
        } else {
            MPI_Send(&avg_recv_bandwidth, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
        }

        // clear the buffer
        free(buffer);
    }

    MPI_Finalize();

    return 0;
}
