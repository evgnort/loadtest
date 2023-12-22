#define _GNU_SOURCE 

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <xmmintrin.h>

#define ARRAY_SIZE (1024 * 1024 / 16)
#define KEYS_COUNT (ARRAY_SIZE / 16)
#define ITERS_COUNT 20
#define MAX_CORES_CNT 10

typedef struct FDataTg
	{
	int key;
	char payload[60];
	} FData;

typedef struct FSearchSetTg
	{
	FData *main_array;
	int *keys;
	char **results;
	int size;
	int array_size;
	char cache_line_padding[32];
	} FSearchSet;

int volatile start = 0;

static int64_t get_nanotime(void)
	{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	return t.tv_sec * 1000000000 + t.tv_nsec;
	}

int64_t make_search(FSearchSet *search)
	{
	int i,*key = search->keys; char **result = search->results;
	FData *data;

	for (i = 0; i < search->size; i++, key++, result++)
		{
		int step_size = search->array_size / 2;
		int pos_num = step_size;

		for(data = &search->main_array[pos_num]; step_size > 1; data = &search->main_array[pos_num])
			{
			if (*key == data->key)
				{ *result = data->payload; break; }
			step_size /= 2;
			pos_num += step_size * ((data->key > *key) ? -1 : 1);
			}
		if (*key == data->key)
			*result = data->payload;
		else if (*key == search->main_array[pos_num-1].key)
			*result = search->main_array[pos_num-1].payload;
		if (!(i % 100) && atomic_load_explicit(&start,__ATOMIC_RELAXED) > 1) break;
		}
	return i;
	}

void *tsearch(void *param)
	{
	while (!atomic_load_explicit(&start,__ATOMIC_RELAXED)) _mm_pause();
	int64_t found = make_search((FSearchSet *)param);
	atomic_store_explicit(&start,2,__ATOMIC_RELAXED);
	return (void *)found;
	}

void free_ssets(FSearchSet *ssets,int size)
	{
	int i;
	for (i = 0; i < size; i++)
		{
		if (ssets[i].keys) free(ssets[i].keys);
		if (ssets[i].results) free(ssets[i].results);
		}
	free(ssets);
	}

FSearchSet *make_ssets(int size,FData *main_array,int asize)
	{
	FSearchSet *ssets = (FSearchSet *)calloc(MAX_CORES_CNT,sizeof(FSearchSet));
	if (!ssets)	return NULL;

	int i,j;
	int maxval = main_array[asize - 1].key + 1;
	for (i = 0; i < size; i++)
		{
		ssets[i].main_array = main_array;
		ssets[i].array_size = asize;
		ssets[i].keys = (int *)malloc(sizeof(int) * KEYS_COUNT);
		ssets[i].results = (char **)malloc(sizeof(char *) * KEYS_COUNT);
		if (!ssets[i].keys || !ssets[i].results)
			return free_ssets(ssets,size),NULL;
		for (j = 0; j < KEYS_COUNT; j++)
			ssets[i].keys[j] = rand() % maxval;
		}
	return ssets;
	}

FData *make_main_array(int size)
	{
	FData *main_array = (FData *)malloc(sizeof(FData) * size);
	if (!main_array) return NULL;

	int i,val = 0;
	for (i = 0; i < size; i++)
		main_array[i].key = (val += rand() % 20 + 1);
	return main_array;
	}

int main()
	{
	pthread_t tids[MAX_CORES_CNT];
	srand(time(NULL));
	int asize = ARRAY_SIZE;

	while (asize <= ARRAY_SIZE * 8)
		{
		FData *main_array = make_main_array(asize);
		if (!main_array) return puts("failed to allocate memory"),1;
		FSearchSet *ssets = make_ssets(MAX_CORES_CNT,main_array,asize);
		if (!ssets) return puts("failed to allocate memory"),free(main_array),1;

		double results[MAX_CORES_CNT] = {0};
		int k,cores,j;
		for (k = 0; k < ITERS_COUNT; k++)
			for (cores = 1; cores <= MAX_CORES_CNT; cores++)
				{
				for (j = 0; j < cores; j++)
					{
					ssets[j].size = KEYS_COUNT / cores;
					pthread_create(&tids[j],NULL,tsearch,(void *)&ssets[j]);
					}
				int64_t t1 = get_nanotime();
				atomic_store_explicit(&start,1,__ATOMIC_RELAXED);
				uint64_t trv, sum = 0;
				for (j = 0; j < cores; j++)
					{
					pthread_join(tids[j],(void **)&trv);
					sum += trv;
					}

				double tm = (get_nanotime() - t1) / 1000000000.0;
				start = 0;
				double res = (double)sum / tm;
				results[cores-1] += res;
				}

		printf("%.0f",results[0] / ITERS_COUNT);
		for (cores = 2; cores <= MAX_CORES_CNT; cores++)
			printf("\t%.0f",results[cores-1] / ITERS_COUNT);
		printf("\n");

		free_ssets(ssets,MAX_CORES_CNT);
		free(main_array);
		asize += ARRAY_SIZE;
		}
	return 0;
	}
