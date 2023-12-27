
/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
// 分别讨论不同矩阵，32x32中将矩阵分块为8x8减少miss，64x64中将矩阵分块为8x8后，
// 再分成4x4减少miss，中间需要一次反对角线上的交换，60x68采用12x12分块处理60x60，最后60x8单独处理
#include <stdio.h>
#include "cachelab.h"
#include "contracts.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. The REQUIRES and ENSURES from 15-122 are included
 *     for your convenience. They can be removed if you like.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    REQUIRES(M > 0);
    REQUIRES(N > 0);
    int i,j,u;
    int v0=0,v1=0,v2=0,v3=0,v4=0,v5=0,v6=0,v7=0;//11个临时变量
    if(M==32&&N==32){ 
        for (i = 0; i < N; i+=8) {
            for (j = 0; j < M; j+=8) {//通过分成8x8小块实现miss的减小
               for(u=0;u<8;u++){
                    v0=A[j][i+u],v1=A[j+1][i+u];
                    v2=A[j+2][i+u],v3=A[j+3][i+u];
                    v4=A[j+4][i+u],v5=A[j+5][i+u];
                    v6=A[j+6][i+u],v7=A[j+7][i+u];//一次性读取减小miss
                    B[i+u][j]=v0;
                    B[i+u][j+1]=v1;
                    B[i+u][j+2]=v2;
                    B[i+u][j+3]=v3;
                    B[i+u][j+4]=v4;
                    B[i+u][j+5]=v5;
                    B[i+u][j+6]=v6;
                    B[i+u][j+7]=v7;
               }
            }
        }
    }
    else if(M==64&&N==64){
        for (i = 0; i < N; i+=8) {
            for (j = 0; j < M; j+=8) {
               for (u = i; u < i + 4; u++){//先分为8x8，再分为4x4
					v0 = A[u][j], v1 = A[u][j+1]; 
                    v2 = A[u][j+2], v3 = A[u][j+3];
					v4 = A[u][j+4], v5 = A[u][j+5]; 
                    v6 = A[u][j+6], v7 = A[u][j+7];
					B[j][u] = v0; 
                    B[j+1][u] = v1; 
                    B[j+2][u] = v2; 
                    B[j+3][u] = v3;
					B[j][u+4] = v4; 
                    B[j+1][u+4] = v5; 
                    B[j+2][u+4] = v6; 
                    B[j+3][u+4] = v7;
				}
				for (u= j; u < j + 4; u++){//需要在中间进行一次调换
					v0 = A[i+4][u], v1 = A[i+5][u]; 
                    v2 = A[i+6][u], v3 = A[i+7][u];
					v4 = B[u][i+4], v5 = B[u][i+5]; 
                    v6 = B[u][i+6], v7 = B[u][i+7];
					B[u][i+4] = v0; 
                    B[u][i+5] = v1; 
                    B[u][i+6] = v2; 
                    B[u][i+7] = v3;
					B[u+4][i] = v4; 
                    B[u+4][i+1] = v5; 
                    B[u+4][i+2] = v6; 
                    B[u+4][i+3] = v7;
				}
				for (u = i + 4; u < i + 8; u++){
					v0 = A[u][j+4], v1 = A[u][j+5]; 
                    v2 = A[u][j+6], v3 = A[u][j+7];
					B[j+4][u] = v0; 
                    B[j+5][u] = v1; 
                    B[j+6][u] = v2; 
                    B[j+7][u] = v3;
				}
            }
        }
    }
    else{
        for (i = 0; i< 60; i += 12){
            for (j = 0; j < 60; j +=12){//分成12x12
                for (u = 0; u < 12; u++){
                    v0=A[j][i+u],v1=A[j+1][i+u];
                    v2=A[j+2][i+u],v3=A[j+3][i+u];
                    v4=A[j+4][i+u],v5=A[j+5][i+u];
                    v6=A[j+6][i+u],v7=A[j+7][i+u];
                    B[i+u][j]=v0;
                    B[i+u][j+1]=v1;
                    B[i+u][j+2]=v2;
                    B[i+u][j+3]=v3;
                    B[i+u][j+4]=v4;
                    B[i+u][j+5]=v5;
                    B[i+u][j+6]=v6;
                    B[i+u][j+7]=v7;
                    v0=A[j+8][i+u],v1=A[j+9][i+u];
                    v2=A[j+10][i+u],v3=A[j+11][i+u];
                    B[i+u][j+8]=v0;
                    B[i+u][j+9]=v1;
                    B[i+u][j+10]=v2;
                    B[i+u][j+11]=v3;
                }
            }
        }
        for(i = 0; i < M; i++)//单独处理最后60x8
            for(j=60; j < N; j++)
			    B[i][j] = A[j][i];
    }
    ENSURES(is_transpose(M, N, A, B));
}

/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

 /*
  * trans - A simple baseline transpose function, not optimized for the cache.
  */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    REQUIRES(M > 0);
    REQUIRES(N > 0);

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }

    ENSURES(is_transpose(M, N, A, B));
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc);

}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

