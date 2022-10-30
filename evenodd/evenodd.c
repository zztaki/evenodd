#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX_P 100
#define MAX_FILE_LENGTH 100
char dirnames[MAX_P + 10][MAX_FILE_LENGTH + 10]; // 存放目录名（disk_i）或文件名(disk_i/file_name_i)
int arrays[MAX_P + 10][MAX_P + 10];
unsigned char buf[MAX_P + 10][MAX_P + 10]; // p+2个数据块的一列
unsigned char syndrome;                    // 对角线编号为(p-1)上的数据的异或值

void usage() {
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0> ...\n");
}

// 如果p不是质数返回1，否则返回0
inline int notPrime(int p) {
    for (int i = 2; i <= sqrt(p); i++) {
        if (p % i == 0) {
            return 1;
        }
    }
    return 0;
}

// 判断 disk_0, disk_1, ..., disk_<p+1>
// 是否存在，不存在则需要创建对应文件夹
inline void createDisk(int p) {
    for (int i = 0; i < p + 2; i++) {
        sprintf(dirnames[i], "./disk_%d", i);
        if (access(dirnames[i], F_OK) == -1) {
            mkdir(dirnames[i], S_IRWXU);
        }
    }
}

// 设置数据列和校验列
inline void setBuf(unsigned char *tmp, int p) {
    syndrome = 0;
    for (int i = 0; i < p - 1; i++) {
        syndrome ^= tmp[(p - 1) * (i + 1)];
        for (int j = 0; j < p; j++) {
            buf[p][i] ^= tmp[i * p + j];                     // 第i行逐列异或得到第i行的行校验位
            buf[p + 1][(i + j) % (p - 1)] ^= tmp[i * p + j]; // 逐步得到未与syndrome异或的对角线校验位
            buf[j][i] = tmp[i * p + j];                      // 数据列
        }
    }
    for (int i = 0; i < p - 1; i++) {
        buf[p + 1][i] ^= syndrome; // 对角线校验位与syndrome异或
    }
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
        if (p <= 1 || notPrime(p)) { // 是否需要判定？
            printf("<p>: %d isn't a prime!\n", p);
            return -1;
        }

        createDisk(p);

        // open <file_name>, read from it and write to ./disk_<i>/<filename>_i
        char *file_name = argv[2];
        unsigned char tmp[(MAX_P + 10) * MAX_P] = {0};
        FILE *fpr = fopen(file_name, "r"), *fpw[MAX_P + 10];
        int cnt; // 实际一次从file_name中读取的长度

        for (int j = 0; j < p + 2; j++) {
            sprintf(dirnames[j], "./disk_%d/%s_%d", j, file_name, j);
            fpw[j] = fopen(dirnames[j], "w");
        }

        // 将源文件进行EVENODD编码
        while (fread(tmp, sizeof(unsigned char), p * (p - 1), fpr)) {
            setBuf(tmp, p);

            // 写入磁盘
            for (int j = 0; j < p + 2; j++) {
                fwrite(buf[j], sizeof(unsigned char), p - 1, fpw[j]);
                memset(buf[j], 0, MAX_P + 10);
            }

            memset(tmp, 0, (MAX_P + 10) * MAX_P);
        }
        fclose(fpr);
        for (int j = 0; j < p + 2; j++) {
            fclose(fpw[j]);
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
