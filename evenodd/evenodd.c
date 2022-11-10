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
unsigned char buf[MAX_P + 10][MAX_P + 10];       // p+2个数据块的一列
unsigned char syndrome;                          // 对角线编号为(p-1)上的数据的异或值
int cnt;                                         // 一次fread读取的大小

// S[i][0]表示缺失的两个数据块的第i行数据的异或值；S[i][1]表示缺失的两个数据块对角线编号为i的数据的异或值
unsigned char S[MAX_P + 10][2]; // 水平校验子和对角线校验子

void usage() {
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0> ...\n");
}

// 如果p不是质数返回1，否则返回0
int notPrime(int p) {
    for (int i = 2; i <= sqrt(p); i++) {
        if (p % i == 0) {
            return 1;
        }
    }
    return 0;
}

// 判断 disk_0, disk_1, ..., disk_<p+1>
// 是否存在，不存在则需要创建对应文件夹
void createDisk(int p) {
    for (int i = 0; i < p + 2; i++) {
        sprintf(dirnames[i], "./disk_%d", i);
        if (access(dirnames[i], F_OK) == -1) {
            mkdir(dirnames[i], S_IRWXU);
        }
    }
}

// BKDRHash对原文件名进行加密，密文用于数据文件的分块
unsigned int BKDRHash(char *str) {
    unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
    unsigned int hash = 0;

    while (*str) {
        hash = hash * seed + (*str++);
    }

    return (hash & 0x7FFFFFFF);
}

// 创建元数据
void createMeta(char *filename, unsigned int hashname, int p, long long int fileSize) {
    FILE *fp = fopen("./.meta", "a+");
    fprintf(fp, "%s %u %d %lld\n", filename, hashname, p, fileSize);
    fclose(fp);
}

// 设置数据列和校验列
// a e i m q
// b f j n r
// c g k o s
// d h l p t
void setBuf(unsigned char *tmp, int p) {
    for (int j = 0; j < p; j++) {
        for (int i = 0; i < p - 1; i++) {
            buf[j][i] = tmp[j * (p - 1) + i]; // 数据列
        }
    }
    for (int i = 0; i < p - 1; i++) {
        for (int j = 0; j < p; j++) {
            buf[p][i] ^= buf[j][i];               // 第i行逐列异或得到第i行的行校验位
            buf[p + 1][(i + j) % p] ^= buf[j][i]; // 逐步得到未与syndrome异或的对角线校验位
        }
    }
    syndrome = buf[p + 1][p - 1];
    buf[p + 1][p - 1] = 0;
    for (int i = 0; i < p - 1; i++) {
        buf[p + 1][i] ^= syndrome; // 与syndrome异或得到正确的对角线校验位
    }
}

// 读文件时，检查.meta中是否有file_name，有则返回1，并且将其hash码和p值保存到指针中；否则返回0
int checkFileExist(char *file_name, unsigned int *hash_name_point, int *p_point, long long int *fileSize_point) {
    FILE *fp = fopen("./.meta", "r");
    char _file_name[MAX_FILE_LENGTH + 10] = {0};
    unsigned int hash_name;
    int p;
    long long int fileSize;

    while (fscanf(fp, "%s %u %d %lld\n", _file_name, &hash_name, &p, &fileSize) != EOF) {
        if (strcmp(file_name, _file_name) == 0) {
            *hash_name_point = hash_name;
            *p_point = p;
            *fileSize_point = fileSize;
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

// 获得文件丢失数据块的数目以及具体丢失的块编号
int getFailCnt(unsigned int hashname, int p, int *a) {
    int cnt = 0;
    for (int j = 0; j < p + 2; j++) {
        sprintf(dirnames[j], "./disk_%d", j);
        if (access(dirnames[j], F_OK) == -1) {
            a[cnt++] = j;
        } else {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hashname, j);
            if (access(dirnames[j], F_OK) == -1) {
                a[cnt++] = j;
            }
        }
    }
    return cnt;
}

// -------------------------read START------------------------------
// 读数据时需要注意fwrite末尾多余'\0'以及原文件内容包含'\0'的情况

// 从前p列读取数据到sava_as中
void readData(unsigned int hash_name, int p, char *save_as, long long int remainSize) {
    FILE *fpw = fopen(save_as, "w"), *fpr[MAX_P + 10];
    if (remainSize == 0) {
        fclose(fpw);
        return;
    }

    for (int j = 0; j < p; j++) {
        sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
        fpr[j] = fopen(dirnames[j], "r");
    }
    char tmp[MAX_P + 10] = {0};
    int flag = 1;
    while (flag) {
        for (int j = 0; j < p; j++) {
            fread(tmp, sizeof(unsigned char), p - 1, fpr[j]);
            if (p - 1 < remainSize) {
                fwrite(tmp, sizeof(unsigned char), p - 1, fpw);
                remainSize -= p - 1;
            } else {
                fwrite(tmp, sizeof(unsigned char), remainSize, fpw);
                remainSize = 0;
                flag = 0;
                break;
            }
            memset(tmp, 0, MAX_P + 10);
        }
        if (!flag)
            break;
    }
    for (int j = 0; j < p; j++) {
        fclose(fpr[j]);
    }
    fclose(fpw);
}

// 利用行校验列正常读出损坏列
void readDataByLine(unsigned int hash_name, int fail, int p, char *save_as, long long int remainSize) {
    FILE *fpw = fopen(save_as, "w"), *fpr[MAX_P + 10];
    if (remainSize == 0) {
        fclose(fpw);
        return;
    }

    for (int j = 0; j < p + 1; j++) {
        if (j != fail) {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
            fpr[j] = fopen(dirnames[j], "r");
        }
    }

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p + 1; j++) {
            if (j != fail) { // 读出p-1个数据列以及行校验列
                fread(buf[j], sizeof(unsigned char), p - 1, fpr[j]);
            }
        }

        for (int i = 0; i < p - 1; i++) { // 利用异或运算得到第fail数据列
            for (int j = 0; j < p + 1; j++) {
                if (j != fail) {
                    buf[fail][i] ^= buf[j][i];
                }
            }
        }

        if (p * (p - 1) < remainSize) {
            for (int j = 0; j < p; j++) { // 写入sava_as
                fwrite(buf[j], sizeof(unsigned char), p - 1, fpw);
            }
            remainSize -= p * (p - 1);
        } else {
            for (int j = 0; j < p; j++) { // 写入sava_as
                if (p - 1 < remainSize) {
                    fwrite(buf[j], sizeof(unsigned char), p - 1, fpw);
                    remainSize -= p - 1;
                } else {
                    fwrite(buf[j], sizeof(unsigned char), remainSize, fpw);
                    remainSize = 0;
                    flag = 0;
                    break;
                }
            }
        }

        for (int j = 0; j < p + 1; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }

        if (!flag)
            break;
    }
    for (int j = 0; j < p + 1; j++) {
        if (j != fail) {
            fclose(fpr[j]);
        }
    }
    fclose(fpw);
}

// 利用对角线校验列正常读出损坏列
void readDataByDiagonal(unsigned int hash_name, int fail, int p, char *save_as, long long int remainSize) {
    FILE *fpw = fopen(save_as, "w"), *fpr[MAX_P + 10];
    if (remainSize == 0) {
        fclose(fpw);
        return;
    }

    for (int j = 0; j < p + 2; j++) {
        if (j != fail && j != p) {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
            fpr[j] = fopen(dirnames[j], "r");
        }
    }

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p + 2; j++) {
            if (j != fail && j != p) { // 读出p-1个数据列以及行校验列
                fread(buf[j], sizeof(unsigned char), p - 1, fpr[j]);
            }
        }

        // 计算出syndrome
        syndrome = fail - 1 >= 0 ? buf[p + 1][fail - 1] : buf[p + 1][fail - 1 + p];
        for (int j = 0; j < p; j++) {
            syndrome ^= (fail - j - 1 >= 0 ? buf[j][fail - j - 1] : buf[j][fail - j - 1 + p]);
        }

        // 恢复丢失数据列fail
        for (int i = 0; i < p - 1; i++) {
            // buf[fail][i] = syndrome ^ (fail - 1 >= 0 ? buf[p + 1][fail - 1] : buf[p + 1][fail - 1 + p]);论文中错了
            buf[fail][i] = syndrome ^ buf[p + 1][(fail + i) % p];
            for (int j = 0; j < p; j++) {
                if (j == fail) {
                    continue;
                }
                buf[fail][i] ^= (i + fail - j >= 0 ? buf[j][(i + fail - j) % p] : buf[j][i + fail - j + p]);
            }
        }

        if (p * (p - 1) < remainSize) {
            for (int j = 0; j < p; j++) { // 写入sava_as
                fwrite(buf[j], sizeof(unsigned char), p - 1, fpw);
            }
            remainSize -= p * (p - 1);
        } else {
            for (int j = 0; j < p; j++) {
                if (p - 1 < remainSize) {
                    fwrite(buf[j], sizeof(unsigned char), p - 1, fpw);
                    remainSize -= p - 1;
                } else {
                    fwrite(buf[j], sizeof(unsigned char), remainSize, fpw);
                    remainSize = 0;
                    flag = 0;
                    break;
                }
            }
        }
        for (int j = 0; j < p; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }
        memset(buf[p + 1], 0, MAX_P + 10);

        if (!flag)
            break;
    }
    for (int j = 0; j < p + 2; j++) {
        if (j != fail && j != p) {
            fclose(fpr[j]);
        }
    }
    fclose(fpw);
}

// 利用行校验列和对角线校验列正常读出两个损坏列
void readDataByLine_Diagonal(unsigned int hash_name, int fail1, int fail2, int p, char *save_as, long long int remainSize) {
    FILE *fpw = fopen(save_as, "w"), *fpr[MAX_P + 10];
    if (remainSize == 0) {
        fclose(fpw);
        return;
    }

    for (int j = 0; j < p + 2; j++) {
        if (j != fail1 && j != fail2) {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
            fpr[j] = fopen(dirnames[j], "r");
        }
    }

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p + 2; j++) {
            if (j != fail1 && j != fail2) { // 读出p-1个数据列以及行校验列
                fread(buf[j], sizeof(unsigned char), p - 1, fpr[j]);
            }
        }

        // 获得syndrome
        syndrome = 0;
        for (int i = 0; i < p - 1; i++) {
            syndrome ^= (buf[p][i] ^ buf[p + 1][i]);
        }

        // 获得水平和对角线校验子
        for (int i = 0; i < p; i++) {
            S[i][0] = 0;
            S[i][1] = syndrome ^ buf[p + 1][i];
            for (int j = 0; j < p; j++) {
                if (j != fail1 && j != fail2) {
                    S[i][0] ^= buf[j][i];
                    S[i][1] ^= (i - j >= 0 ? buf[j][i - j] : buf[j][i - j + p]);
                }
            }
            S[i][0] ^= buf[p][i];
        }

        // 恢复两个缺失数据列
        int s = fail1 - fail2 - 1 + p;
        do {
            buf[fail2][s] = S[(fail2 + s) % p][1] ^ buf[fail1][(s + fail2 - fail1) % p];
            buf[fail1][s] = S[s][0] ^ buf[fail2][s];
            s = s - fail2 + fail1 >= 0 ? s - fail2 + fail1 : s - fail2 + fail1 + p;
        } while (s != p - 1);

        if (p * (p - 1) < remainSize) {
            for (int j = 0; j < p; j++) { // 写入sava_as
                fwrite(buf[j], sizeof(unsigned char), p - 1, fpw);
            }
            remainSize -= p * (p - 1);
        } else {
            for (int j = 0; j < p; j++) {
                if (p - 1 < remainSize) {
                    fwrite(buf[j], sizeof(unsigned char), p - 1, fpw);
                    remainSize -= p - 1;
                } else {
                    fwrite(buf[j], sizeof(unsigned char), remainSize, fpw);
                    remainSize = 0;
                    flag = 0;
                    break;
                }
            }
        }

        for (int j = 0; j < p + 2; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }

        if (!flag)
            break;
    }
    for (int j = 0; j < p + 2; j++) {
        if (j != fail1 && j != fail2) {
            fclose(fpr[j]);
        }
    }
    fclose(fpw);
}

// -------------------------read END------------------------------

// -------------------------repair START------------------------------
// 因为所有数据块的大小相等，因此某一个数据块fread读到文件结束时，其它文件此轮也结束，直接break避免多写一轮'\0'
// 注意，disk文件夹中的文件内容可能包含'\0'

// 恢复对角线校验块
void repairDiagonal(unsigned int hash_name, int p) {
    FILE *fpw, *fpr[MAX_P + 10];
    for (int j = 0; j < p; j++) {
        sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
        fpr[j] = fopen(dirnames[j], "r");
    }
    sprintf(dirnames[p + 1], "./disk_%d/%u_%d", p + 1, hash_name, p + 1);
    fpw = fopen(dirnames[p + 1], "w");

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p; j++) {
            if (!fread(buf[j], sizeof(unsigned char), p - 1, fpr[j])) {
                flag = 0;
                break;
            }
        }
        if (!flag)
            break;

        for (int i = 0; i < p - 1; i++) {
            for (int j = 0; j < p; j++) {
                buf[p + 1][(i + j) % p] ^= buf[j][i]; // 逐步得到未与syndrome异或的对角线校验位
            }
        }
        syndrome = buf[p + 1][p - 1];
        buf[p + 1][p - 1] = 0;
        for (int i = 0; i < p - 1; i++) {
            buf[p + 1][i] ^= syndrome; // 与syndrome异或得到正确的对角线校验位
        }
        fwrite(buf[p + 1], sizeof(unsigned char), p - 1, fpw);

        for (int j = 0; j < p; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }
        memset(buf[p + 1], 0, MAX_P + 10);
    }
    for (int j = 0; j < p; j++) {
        fclose(fpr[j]);
    }
    fclose(fpw);
}

// 恢复行校验块
void repairLine(unsigned int hash_name, int p) {
    FILE *fpw, *fpr[MAX_P + 10];
    for (int j = 0; j < p; j++) {
        sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
        fpr[j] = fopen(dirnames[j], "r");
    }
    sprintf(dirnames[p], "./disk_%d/%u_%d", p, hash_name, p);
    fpw = fopen(dirnames[p], "w");

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p; j++) {
            if (!fread(buf[j], sizeof(unsigned char), p - 1, fpr[j])) {
                flag = 0;
                break;
            }
        }
        if (!flag)
            break;

        for (int i = 0; i < p - 1; i++) {
            for (int j = 0; j < p; j++) {
                buf[p][i] ^= buf[j][i]; // 第i行逐列异或得到第i行的行校验位
            }
        }
        fwrite(buf[p], sizeof(unsigned char), p - 1, fpw);

        for (int j = 0; j < p + 1; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }
    }
    for (int j = 0; j < p; j++) {
        fclose(fpr[j]);
    }
    fclose(fpw);
}

// 恢复一个数据块
void repairOneData(unsigned int hash_name, int fail, int p) {
    FILE *fpw, *fpr[MAX_P + 10];
    for (int j = 0; j < p + 1; j++) {
        if (j != fail) {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
            fpr[j] = fopen(dirnames[j], "r");
        }
    }
    sprintf(dirnames[fail], "./disk_%d/%u_%d", fail, hash_name, fail);
    fpw = fopen(dirnames[fail], "w");

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p + 1; j++) {
            if (j != fail && !fread(buf[j], sizeof(unsigned char), p - 1, fpr[j])) {
                flag = 0;
                break;
            }
        }
        if (!flag)
            break;

        for (int i = 0; i < p - 1; i++) {
            for (int j = 0; j < p + 1; j++) {
                if (j != fail) {
                    buf[fail][i] ^= buf[j][i]; // 其它数据列和行校验列异或恢复丢失数据列
                }
            }
        }
        fwrite(buf[fail], sizeof(unsigned char), p - 1, fpw);

        for (int j = 0; j < p + 1; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }
    }
    for (int j = 0; j < p + 1; j++) {
        if (j != fail) {
            fclose(fpr[j]);
        }
    }
    fclose(fpw);
}

// 恢复行校验块和对角线校验块
void repairLine_Diagonal(unsigned int hash_name, int p) {
    FILE *fpw[2], *fpr[MAX_P + 10];
    for (int j = 0; j < p; j++) {
        sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
        fpr[j] = fopen(dirnames[j], "r");
    }
    sprintf(dirnames[p], "./disk_%d/%u_%d", p, hash_name, p);
    fpw[0] = fopen(dirnames[p], "w");
    sprintf(dirnames[p + 1], "./disk_%d/%u_%d", p + 1, hash_name, p + 1);
    fpw[1] = fopen(dirnames[p + 1], "w");

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p; j++) {
            if (!fread(buf[j], sizeof(unsigned char), p - 1, fpr[j])) {
                flag = 0;
                break;
            }
        }
        if (!flag)
            break;

        for (int i = 0; i < p - 1; i++) {
            for (int j = 0; j < p; j++) {
                buf[p][i] ^= buf[j][i];               // 第i行逐列异或得到第i行的行校验位
                buf[p + 1][(i + j) % p] ^= buf[j][i]; // 逐步得到未与syndrome异或的对角线校验位
            }
        }
        syndrome = buf[p + 1][p - 1];
        buf[p + 1][p - 1] = 0;
        for (int i = 0; i < p - 1; i++) {
            buf[p + 1][i] ^= syndrome; // 与syndrome异或得到正确的对角线校验位
        }
        fwrite(buf[p], sizeof(unsigned char), p - 1, fpw[0]);
        fwrite(buf[p + 1], sizeof(unsigned char), p - 1, fpw[1]);

        for (int j = 0; j < p + 2; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }
    }
    for (int j = 0; j < p; j++) {
        fclose(fpr[j]);
    }
    fclose(fpw[0]);
    fclose(fpw[1]);
}

// 恢复一个数据块和对角线校验块
void repairOneData_Line(unsigned int hash_name, int fail, int p) {
    FILE *fpw[2], *fpr[MAX_P + 10];
    for (int j = 0; j < p + 2; j++) {
        if (j != fail && j != p) {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
            fpr[j] = fopen(dirnames[j], "r");
        }
    }
    sprintf(dirnames[fail], "./disk_%d/%u_%d", fail, hash_name, fail);
    fpw[0] = fopen(dirnames[fail], "w");
    sprintf(dirnames[p], "./disk_%d/%u_%d", p, hash_name, p);
    fpw[1] = fopen(dirnames[p], "w");

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p + 2; j++) {
            if (j != fail && j != p && !fread(buf[j], sizeof(unsigned char), p - 1, fpr[j])) { // 读出p-1个数据列以及行校验列
                flag = 0;
                break;
            }
        }
        if (!flag)
            break;

        // 计算出syndrome
        syndrome = fail - 1 >= 0 ? buf[p + 1][fail - 1] : buf[p + 1][fail - 1 + p];
        for (int j = 0; j < p; j++) {
            syndrome ^= (fail - j - 1 >= 0 ? buf[j][fail - j - 1] : buf[j][fail - j - 1 + p]);
        }

        // 恢复丢失数据列fail和行校验列p
        for (int i = 0; i < p - 1; i++) {
            // buf[fail][i] = syndrome ^ (fail - 1 >= 0 ? buf[p + 1][fail - 1] : buf[p + 1][fail - 1 + p]);论文中错了
            buf[fail][i] = syndrome ^ buf[p + 1][(fail + i) % p];
            buf[p][i] = 0;
            for (int j = 0; j < p; j++) {
                if (j == fail) {
                    continue;
                }
                buf[fail][i] ^= (i + fail - j >= 0 ? buf[j][(i + fail - j) % p] : buf[j][i + fail - j + p]);
                buf[p][i] ^= buf[j][i];
            }
            buf[p][i] ^= buf[fail][i];
        }
        fwrite(buf[fail], sizeof(unsigned char), p - 1, fpw[0]);
        fwrite(buf[p], sizeof(unsigned char), p - 1, fpw[1]);

        for (int j = 0; j < p + 2; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }
    }
    for (int j = 0; j < p + 2; j++) {
        if (j != fail && j != p) {
            fclose(fpr[j]);
        }
    }
    fclose(fpw[0]);
    fclose(fpw[1]);
}

// 恢复两个数据块
void repairTwoData(unsigned int hash_name, int fail1, int fail2, int p) {
    FILE *fpw[2], *fpr[MAX_P + 10];
    for (int j = 0; j < p + 2; j++) {
        if (j != fail1 && j != fail2) {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
            fpr[j] = fopen(dirnames[j], "r");
        }
    }
    sprintf(dirnames[fail1], "./disk_%d/%u_%d", fail1, hash_name, fail1);
    fpw[0] = fopen(dirnames[fail1], "w");
    sprintf(dirnames[fail2], "./disk_%d/%u_%d", fail2, hash_name, fail2);
    fpw[1] = fopen(dirnames[fail2], "w");

    int flag = 1;
    while (flag) {
        for (int j = 0; j < p + 2; j++) {
            if (j != fail1 && j != fail2 && !fread(buf[j], sizeof(unsigned char), p - 1, fpr[j])) { // 读出p-1个数据列以及行校验列
                flag = 0;
                break;
            }
        }
        if (!flag)
            break;

        // 获得syndrome
        syndrome = 0;
        for (int i = 0; i < p - 1; i++) {
            syndrome ^= (buf[p][i] ^ buf[p + 1][i]);
        }

        // 获得水平和对角线校验子
        for (int i = 0; i < p; i++) {
            S[i][0] = 0;
            S[i][1] = syndrome ^ buf[p + 1][i];
            for (int j = 0; j < p; j++) {
                if (j != fail1 && j != fail2) {
                    S[i][0] ^= buf[j][i];
                    S[i][1] ^= (i - j >= 0 ? buf[j][i - j] : buf[j][i - j + p]);
                }
            }
            S[i][0] ^= buf[p][i];
        }

        // 恢复两个缺失数据列
        int s = fail1 - fail2 - 1 + p;
        do {
            buf[fail2][s] = S[(fail2 + s) % p][1] ^ buf[fail1][(s + fail2 - fail1) % p];
            buf[fail1][s] = S[s][0] ^ buf[fail2][s];
            s = s - fail2 + fail1 >= 0 ? s - fail2 + fail1 : s - fail2 + fail1 + p;
        } while (s != p - 1);

        fwrite(buf[fail1], sizeof(unsigned char), p - 1, fpw[0]);
        fwrite(buf[fail2], sizeof(unsigned char), p - 1, fpw[1]);

        for (int j = 0; j < p + 2; j++) {
            memset(buf[j], 0, MAX_P + 10);
        }
    }
    for (int j = 0; j < p + 2; j++) {
        if (j != fail1 && j != fail2) {
            fclose(fpr[j]);
        }
    }
    fclose(fpw[0]);
    fclose(fpw[1]);
}

// -------------------------repair END------------------------------

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

        char *file_name = argv[2];
        if (access(file_name, F_OK) == -1) {
            printf("File does not exist!\n");
            return -1;
        }

        // p应该是质数
        int p = atoi(argv[3]);
        if (p <= 1 || notPrime(p)) { // 是否需要判定？
            printf("<p>: %d isn't a prime!\n", p);
            return -1;
        }

        // 创建disk文件夹
        createDisk(p);

        // 在.meta文件中追加 file_name hash_name 的映射
        unsigned int hash_name = BKDRHash(file_name);

        unsigned char tmp[(MAX_P + 10) * MAX_P] = {0};
        FILE *fpr = fopen(file_name, "r"), *fpw[MAX_P + 10];

        for (int j = 0; j < p + 2; j++) {
            sprintf(dirnames[j], "./disk_%d/%u_%d", j, hash_name, j);
            fpw[j] = fopen(dirnames[j], "w");
        }
        long long int fileSize = 0;

        // 将源文件进行EVENODD编码
        while (cnt = fread(tmp, sizeof(unsigned char), p * (p - 1), fpr)) {
            setBuf(tmp, p);
            fileSize += cnt;

            // 写入磁盘
            for (int j = 0; j < p + 2; j++) {
                fwrite(buf[j], sizeof(unsigned char), p - 1, fpw[j]); // write写入p-1而不是strlen(buf[j])。因为校验块中间可能包含'\0'。
                memset(buf[j], 0, MAX_P + 10);
            }

            memset(tmp, 0, (MAX_P + 10) * MAX_P); // 通过置零，无需考虑一次fread读取长度小于 p*(p-1) 的情况
        }
        createMeta(file_name, hash_name, p, fileSize);

        fclose(fpr);
        for (int j = 0; j < p + 2; j++) {
            fclose(fpw[j]);
        }

    } else if (strcmp(op, "read") == 0) {
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
        char *file_name = argv[2], *save_as = argv[3];
        unsigned int hash_name;
        int p;
        long long int fileSize;

        // 检查文件是否存在，若存在一并获得hash_name
        int file_exist = checkFileExist(file_name, &hash_name, &p, &fileSize);
        if (!file_exist) {
            printf("File does not exist!\n");
            return -1;
        }

        int fail[MAX_P + 10]; // 记录缺失的数据块
        int failCnt = getFailCnt(hash_name, p, fail);
        if (failCnt == 0) {
            readData(hash_name, p, save_as, fileSize);
        } else if (failCnt == 1) {
            if (fail[0] >= p) { // 冗余块丢失不影响读
                readData(hash_name, p, save_as, fileSize);
            } else { // 某个数据块丢失，利用行校验位恢复读
                readDataByLine(hash_name, fail[0], p, save_as, fileSize);
            }
        } else if (failCnt == 2) {
            if (fail[0] == p && fail[1] == p + 1) { // 仅冗余块丢失，正常读
                readData(hash_name, p, save_as, fileSize);
            } else if (fail[0] < p && fail[1] == p) {
                readDataByDiagonal(hash_name, fail[0], p, save_as, fileSize);
            } else if (fail[0] < p && fail[1] == p + 1) { // 某个数据块，以及对角线校验列丢失，利用行校验列恢复读
                readDataByLine(hash_name, fail[0], p, save_as, fileSize);
            } else { // 两个数据块丢失
                readDataByLine_Diagonal(hash_name, fail[0], fail[1], p, save_as, fileSize);
            }
        } else {
            printf("File corrupted!\n");
            return -1;
        }

    } else if (strcmp(op, "repair") == 0) {
        /*
         * Please repair failed disks. The number of failures is specified by
         * "num_erasures", and the index of disks are provided in the command
         * line parameters.
         * For example: Suppose "number_erasures" is 2, and the indices of
         * failed disks are "0" and "1". After the repair operation, the data
         * splits in folder "disk_0" and "disk_1" should be repaired.
         */

        // should be: evenodd repair <number_erasures> <idx> ...
        int failCnt = atoi(argv[2]);
        if (failCnt <= 0 || argc != failCnt + 3) {
            usage();
            return -1;
        }

        char file_name[MAX_FILE_LENGTH + 10];
        unsigned int hash_name;
        int p;

        if (failCnt > 2) {
            printf("Too many corruptions!\n");
            return -1;
        } else if (failCnt == 1) {
            int fail = atoi(argv[3]);
            if (fail < 0) {
                printf("<idx>: %d should >= 0!\n", fail);
                return -1;
            }

            // 创建损坏文件夹
            char failDir[MAX_FILE_LENGTH + 10] = {0};
            sprintf(failDir, "./disk_%d", fail);
            if (access(failDir, F_OK) == -1) {
                mkdir(failDir, S_IRWXU);
            }

            FILE *fp = fopen("./.meta", "r");
            while (fscanf(fp, "%s %u %d\n", file_name, &hash_name, &p) != EOF) {
                if (fail >= p + 2) {
                    continue;
                } else if (fail == p + 1) {
                    repairDiagonal(hash_name, p);
                } else if (fail == p) {
                    repairLine(hash_name, p);
                } else {
                    repairOneData(hash_name, fail, p);
                }
            }
        } else { // 损坏两个文件夹
            int fail1 = atoi(argv[3]), fail2 = atoi(argv[4]);

            // 调整fail1 < fail2
            if (fail1 > fail2) {
                int tmp = fail1;
                fail1 = fail2;
                fail2 = tmp;
            }
            if (fail1 < 0) {
                printf("<idx>: %d should >= 0!\n", fail1);
                return -1;
            } else if (fail1 == fail2) {
                printf("<idx1>: %d == <idx2>: %d!\n", fail1, fail2);
                return -1;
            }

            // 创建损坏文件夹
            char failDir[2][MAX_FILE_LENGTH + 10] = {{0}, {0}};
            sprintf(failDir[0], "./disk_%d", fail1);
            sprintf(failDir[1], "./disk_%d", fail2);
            if (access(failDir[0], F_OK) == -1) {
                mkdir(failDir[0], S_IRWXU);
            }
            if (access(failDir[1], F_OK) == -1) {
                mkdir(failDir[1], S_IRWXU);
            }

            FILE *fp = fopen("./.meta", "r");
            while (fscanf(fp, "%s %u %d\n", file_name, &hash_name, &p) != EOF) {
                if (fail1 >= p + 2) { // 丢失的两个文件夹与file_name无关
                    continue;
                } else if (fail1 == p + 1) { // 丢失的fail2文件夹与file_name无关，丢失的fail1是file_name的对角线校验块
                    repairDiagonal(hash_name, p);
                } else if (fail1 == p) {  //丢失的fail1是file_name的行校验块
                    if (fail2 == p + 1) { // 丢失的fail2是file_name的对角线校验块
                        repairLine_Diagonal(hash_name, p);
                    } else { // 丢失的fail2文件夹与file_name无关
                        repairLine(hash_name, p);
                    }
                } else {                  // 丢失的fail1是file_name的数据块
                    if (fail2 >= p + 2) { // 丢失的fail2与file_name无关
                        repairOneData(hash_name, fail1, p);
                    } else if (fail2 == p + 1) { //丢失的fail2是file_name的对角线校验块
                        repairOneData(hash_name, fail1, p);
                        repairDiagonal(hash_name, p);
                    } else if (fail2 == p) { // 丢失的fail2是file_name的行校验块
                        repairOneData_Line(hash_name, fail1, p);
                    } else { // 丢失的fail2也是file_name的数据块
                        repairTwoData(hash_name, fail1, fail2, p);
                    }
                }
            }
            fclose(fp);
        }
    } else {
        printf("Non-supported operations!\n");
    }
    return 0;
}
