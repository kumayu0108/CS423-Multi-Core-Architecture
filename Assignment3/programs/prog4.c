#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define SIZE (1 << 15)

int *a, num_threads;
unsigned long long *private_sum, sum=0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *work (void *param)
{
	int i, id = *(int*)(param), j;
	unsigned long long private_sum_local = 0;
	
        j = (SIZE/num_threads)*(id+1);
	// for(int k = 0; k < 10; k++)
	// sleep(id * 10);
	for (i=(SIZE/num_threads)*id; i<j; i++) {
		private_sum_local += a[i];
        }

        // pthread_mutex_lock(&mutex);
        sum += private_sum_local;
        // pthread_mutex_unlock(&mutex);
}	

int main (int argc, char *argv[])
{
	int i, j, *tid;
	pthread_t *threads;
	pthread_attr_t attr;

	if (argc != 2) {
		printf ("Need number of threads.\n");
		exit(1);
	}
	num_threads = atoi(argv[1]);
	threads = (pthread_t*)malloc(num_threads*sizeof(pthread_t));
	private_sum = (unsigned long long*)malloc(num_threads*sizeof(unsigned long long));
        tid = (int*)malloc(num_threads*sizeof(int));
        for (i=0; i<num_threads; i++) tid[i] = i;
        a = (int*)malloc(SIZE*sizeof(int));
		printf("array start addr : %llu, second addr : %llu\n", a, a + 1);
	for (i=0; i<SIZE; i++) a[i] = i;

	pthread_attr_init(&attr);

	for (i=1; i<num_threads; i++) {
                /* pthread_create arguments: thread pointer,
                                             attribute pointer,
                                             function pointer,
                                             argument pointer to the function
                */
		pthread_create(&threads[i], &attr, work, &tid[i]);
   	}

	work ((void*)&tid[0]);
	
	for (i=1; i<num_threads; i++) {
		pthread_join(threads[i], NULL);
	}

	printf("SUM: %llu\n", sum);

	return 0;
}
