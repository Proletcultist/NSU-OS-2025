#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

#ifndef MAX_ITERATIONS
#define MAX_ITERATIONS 100000000
#endif

typedef struct
{
	int thread_id;
	int num_threads;
} thread_data_t;

typedef struct
{
	uint32_t salt;
	uint32_t hash;
	int zeros;
} result_t;

static const uint32_t FIXED_X = 0xDEADBEEF;

uint32_t toy_hash(uint32_t x, uint32_t salt)
{
	uint32_t h = x ^ salt;
	h *= 0x7feb352d;
	h ^= h >> 15;
	h *= 0x846ca68b;
	h ^= h >> 16;
	return h;
}

int count_leading_zeros(uint32_t n)
{
	if (n == 0)
		return 32;
	int count = 0;
	for (int i = 31; i >= 0; i--)
	{
		if ((n >> i) & 1)
			break;
		count++;
	}
	return count;
}

void *thread_func(void *arg)
{
	thread_data_t *data = (thread_data_t *)arg;
	int id = data->thread_id;
	int num_threads = data->num_threads;

	result_t *local_result = (result_t *)malloc(sizeof(result_t));
	if (!local_result)
	{
		perror("Failed to allocate memory for result");
		pthread_exit(NULL);
	}

	local_result->zeros = -1;
	local_result->salt = 0;
	local_result->hash = 0;

	for (uint32_t i = (uint32_t)id; i < MAX_ITERATIONS; i += (uint32_t)num_threads)
	{
		uint32_t h = toy_hash(FIXED_X, i);
		int zeros = count_leading_zeros(h);
		if (zeros > local_result->zeros ||
			(zeros == local_result->zeros && i < local_result->salt))
		{
			local_result->zeros = zeros;
			local_result->salt = i;
			local_result->hash = h;
		}
	}

	pthread_exit((void *)local_result);
}

void print_binary(uint32_t n)
{
	for (int i = 31; i >= 0; i--)
	{
		printf("%d", (n >> i) & 1);
		if (i % 8 == 0 && i != 0)
			printf(" ");
	}
	printf("\n");
}

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <number_of_threads>\n", argv[0]);
		return 1;
	}

	char *endptr = NULL;
	long parsed = strtol(argv[1], &endptr, 10);
	if (*argv[1] == '\0' || *endptr != '\0')
	{
		fprintf(stderr, "Invalid number of threads: %s\n", argv[1]);
		return 1;
	}

	if (parsed <= 0)
	{
		fprintf(stderr, "Number of threads must be positive\n");
		return 1;
	}

	if (parsed > 1000000)
	{
		fprintf(stderr, "Too many threads requested\n");
		return 1;
	}

	int num_threads = (int)parsed;
	if (num_threads <= 0)
	{
		fprintf(stderr, "Number of threads must be positive\n");
		return 1;
	}

	pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
	thread_data_t *thread_data = malloc(sizeof(thread_data_t) * num_threads);

	if (!threads || !thread_data)
	{
		perror("Failed to allocate memory for threads");
		free(threads);
		free(thread_data);
		return 1;
	}

	int created = 0;
	for (int i = 0; i < num_threads; i++)
	{
		thread_data[i].thread_id = i;
		thread_data[i].num_threads = num_threads;
		if (pthread_create(&threads[i], NULL, thread_func, &thread_data[i]) != 0)
		{
			perror("Failed to create thread");
			for (int j = 0; j < created; j++)
			{
				pthread_join(threads[j], NULL);
			}
			free(threads);
			free(thread_data);
			return 1;
		}
		created++;
	}

	result_t best_global = {0, 0, -1};

	for (int i = 0; i < num_threads; i++)
	{
		void *retval;
		if (pthread_join(threads[i], &retval) != 0)
		{
			perror("Failed to join thread");
			free(threads);
			free(thread_data);
			return 1;
		}

		if (retval != NULL)
		{
			result_t *thread_res = (result_t *)retval;
			if (thread_res->zeros > best_global.zeros ||
				(thread_res->zeros == best_global.zeros && thread_res->salt < best_global.salt))
			{
				best_global = *thread_res;
			}
			free(thread_res);
		}
	}

	printf("Best salt found: %u\n", best_global.salt);
	printf("Hash: 0x%08X\n", best_global.hash);
	printf("Leading zeros: %d\n", best_global.zeros);
	printf("Binary: ");
	print_binary(best_global.hash);

	free(threads);
	free(thread_data);

	return 0;
}
