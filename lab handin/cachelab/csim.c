
// 采用struct定义地址，使用参数均列为全局变量，通过getopt实现指令读写，
// 文件流实现输入输出读写，直接记录时间并查找最大时间实现LRU，
// 采用malloc动态分配模拟cache的空间，最终在procedure函数直接模拟实现cache。
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "cachelab.h"
#include <time.h>
#include <getopt.h>
#include<unistd.h>
#include<string.h>
#include<limits.h>
#include<stdint.h>
int hit=0,miss=0,evictions=0,v=0,s,E,b,S;//实际上block并没有用
char trace[5005]={};//存储指令
typedef struct{//定义地址结构
    int valid_bit;
    int tag;
    int time;
}cache_line, *cache_asso, **cache; 
cache Cache = NULL;
void prints_usage(){//输出help内容
    printf("Usage: ./csim-ref [-hv] -s <num> -E <num> -b <num> -t <file>\n"
                "Options:\n"
                "  -h         Print this help message.\n"
                "  -v         Optional verbose flag.\n"
                "  -s <num>   Number of set index bits.\n"
                "  -E <num>   Number of lines per set.\n"
                "  -b <num>   Number of block offset bits.\n"
                "  -t <file>  Trace file.\n\n"
                "Like:\n"
                "  linux>  ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace\n"
                "  linux>  ./csim-ref -v -s 4 -E 1 -b 4 -t traces/yi.trace\n");
}
void init_cache(){//初始化cache
    Cache = (cache)malloc(sizeof(cache_asso) * S);
    for(int i = 0; i < S; i++){
		Cache[i] = (cache_asso)malloc(sizeof(cache_line) * E);
		for(int j = 0; j < E; ++j){
			Cache[i][j].valid_bit = 0;
			Cache[i][j].tag = -1;
			Cache[i][j].time = -1;
		}
	}
}
void update_time(){//更新指令时间
	for(int i=0;i<S;i++){
		for(int j=0;j<E;j++){
			if(Cache[i][j].valid_bit)
				Cache[i][j].time++;
		}
	}
}
void update(uint64_t address){//主读写函数
	int set= (address>>b)&((-1U)>>(64-s));
	int ctag=(address>>(b+s));
	int mtime=INT_MIN;
	int mindex=-1;
	for(int j=0;j<E;j++){
		if(Cache[set][j].tag==ctag){
				hit++;
				Cache[set][j].time=0;
				return;
		}
	}
	miss++;
	for(int j=0;j<E;j++){
		if(Cache[set][j].valid_bit==0){
				Cache[set][j].valid_bit=1;
				Cache[set][j].time=0;
				Cache[set][j].tag=ctag;
				return;
		}
	}
	evictions++;
	for(int j=0;j<E;j++){
		if(Cache[set][j].time>mtime){
			mtime=Cache[set][j].time;
			mindex=j;
		}
	}
	Cache[set][mindex].time=0;
	Cache[set][mindex].tag=ctag;
	return;
}
void procedure(){
	FILE* fp = fopen(trace, "r"); // 读取文件名	
	if(fp == NULL){
		printf("error in file\n");
		return;
	}//空指令
	char op;         // 命令开头的 I L M S
	uint64_t address;   // 地址参数
	int size;               // 大小
	while(fscanf(fp," %c %lx,%d\n",&op,&address,&size) > 0){
		if(v)printf("%c %lx,%d\n",op,address,size);
		switch(op){
			case 'L'://L=S
				update(address);
				break;
			case 'M'://M=L+S
				update(address); 
				update(address);
				break;
			case 'S':
				update(address);
				break;
		}
		update_time();	//更新时间
	}
	fclose(fp);

	for(int i = 0; i < S; ++i)
		free(Cache[i]);
	free(Cache);            // malloc 完要记得 free 并且关文件
}
int main(int argc, char* argv[]){
    int opt; 
    v=0;
	while(-1 != (opt = (getopt(argc, argv, "hvs:E:b:t:")))){
        switch(opt){
			case 'h':
				prints_usage();
				break;
			case 'v':
				v=1;
				break;
			case 's':
				s = atoi(optarg);
				break;
			case 'E':
				E = atoi(optarg);
				break;
			case 'b':
				b = atoi(optarg);
				break;
			case 't':
				strcpy(trace, optarg);
				break;
			default:
				prints_usage();
				break;
		}
    }
    if(s<=0 || E<=0 || b<=0 || trace==NULL){
		printf("illegal argument\n");
		return -1;
		}   
	S=1<<s;       
    FILE* fp = fopen(trace, "r");
	if(fp == NULL){
		printf("error in file\n");
		return 1;
    }//空指令
    init_cache();
    procedure();
	printSummary(hit, miss, evictions);
    return 0;
}