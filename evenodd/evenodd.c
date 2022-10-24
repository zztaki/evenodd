#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX_P 10000
char dirnames[MAX_P + 10][20];
int arrays[MAX_P + 10][MAX_P + 10];

// BKDRHash对原文件名进行加密，密文用于数据文件的分块
unsigned int BKDRHash(char *str) {
    unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
    unsigned int hash = 0;

    while (*str) {
        hash = hash * seed + (*str++);
    }

    return (hash & 0x7FFFFFFF);
}

void usage() {
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0> ...\n");
}

/*
Isn't <p>(p >= 2) a prime?
*/
int notPrime(int p) {
    for (int i = 2; i <= sqrt(p); i++) {
        if (p % i == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return -1;
    }

    char *op = argv[1];
    if (strcmp(op, "write") == 0) {
        /*
         * Please encode the input file with EVENODD code
         * and store the erasure-coded splits into corresponding disks
         * For example: Suppose "file_name" is "testfile", and "p" is 5. After
         * your encoding logic, there should be 7 splits, "testfile_0",
         * "testfile_1",
         * ..., "testfile_6", stored in 7 diffrent disk folders from "disk_0" to
         * "disk_6".
         */
        if (argc != 4) {
            usage();
            return -1;
        }

        // p should be a prime
        int p = atoi(argv[3]);
        if (p <= 1 || notPrime(p)) {
            printf("<p>: %d isn't a prime!\n", p);
            return -1;
        }

        // disk_0, disk_1, ..., disk_<p+1> exist?
        for (int i = 0; i < p + 2; i++) {
            sprintf(dirnames[i], "./disk_%d", i);
            if (access(dirnames[i], F_OK) == -1) {
                mkdir(dirnames[i], S_IRWXU);
            }
        }

        // open <file_name>, read from it and write to ./disk_<i>/<filename>_i
        char *file_name = argv[2];
        FILE *fpr = fopen(file_name, "r");

        // 添加行校验位以及未与syndrome进行异或的对角线校验位
        for (int i = 0; i < p - 1; i++) {
            for (int j = 0; j < p; j++) {
                fscanf(fpr, "%d", arrays[i][j]);
                arrays[i][p] ^= arrays[i][j];
                if (i == (i + j) % p) {
                    arrays[i][p + 1] ^= arrays[i][j];
                }
            }
        }
        int syndrome = 0; //对角线编号为(p-1)上的数据的异或值
        for (int i = 0; i < p - 1; i++) {
            syndrome ^= arrays[i][p - 1 - i];
        }
        // 最后修正对角线校验位
        for (int i = 0; i < p - 1; i++) {
            arrays[i][p + 1] ^= syndrome;
        }

        // 写入数据块
        for (int j = 0; j < p + 2; j++) {
            char file[100];
            sprintf(file, "./disk_%d/%s_%d", j, BKDRHash(file_name), j);
            FILE *fpw = fopen(file, "w+");
            for (int i = 0; i < p - 1; i++) {
                fprintf(fpw, "%d ", arrays[i][j]);
            }
            fclose(fpw);
        }

    } else if (strcmp(op, "read")) {
        /*
         * Please read the file specified by "file_name", and store it as a file
         * named "save_as" in the local file system.
         * For example: Suppose "file_name" is "testfile" (which we have encoded
         * before), and "save_as" is "tmp_file". After the read operation, there
         * should be a file named "tmp_file", which is the same as "testfile".
         */

        // should be: evenodd read <file_name> <save_as>
        if (argc != 4) {
            usage();
            return -1;
        }
        char *file_name = argv[2];
        char *save_as = argv[3];

        // 检查文件是否存在
        if (access(file_name, F_OK) == -1) {
            printf("File does not exist!");
            return -1;
        }
    } else if (strcmp(op, "repair")) {
        /*
         * Please repair failed disks. The number of failures is specified by
         * "num_erasures", and the index of disks are provided in the command
         * line parameters.
         * For example: Suppose "number_erasures" is 2, and the indices of
         * failed disks are "0" and "1". After the repair operation, the data
         * splits in folder "disk_0" and "disk_1" should be repaired.
         */
    } else {
        printf("Non-supported operations!\n");
    }
    return 0;
}
