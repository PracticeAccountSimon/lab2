#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef uint32_t u32;
typedef int32_t i32;

struct process
{
  u32 pid;
  u32 arrival_time;
  u32 burst_time;

  TAILQ_ENTRY(process) pointers;

  /* Additional fields here */
  u32 original_burst_time; // Used to calculate waiting time
  bool found; // Has a process already started? (useful for response time)
  /* End of "Additional fields here" */
};

TAILQ_HEAD(process_list, process);

u32 next_int(const char **data, const char *data_end)
{
  u32 current = 0;
  bool started = false;
  while (*data != data_end)
  {
    char c = **data;

    if (c < 0x30 || c > 0x39)
    {
      if (started)
      {
        return current;
      }
    }
    else
    {
      if (!started)
      {
        current = (c - 0x30);
        started = true;
      }
      else
      {
        current *= 10;
        current += (c - 0x30);
      }
    }

    ++(*data);
  }

  printf("Reached end of file while looking for another integer\n");
  exit(EINVAL);
}

u32 next_int_from_c_str(const char *data)
{
  char c;
  u32 i = 0;
  u32 current = 0;
  bool started = false;
  while ((c = data[i++]))
  {
    if (c < 0x30 || c > 0x39)
    {
      exit(EINVAL);
    }
    if (!started)
    {
      current = (c - 0x30);
      started = true;
    }
    else
    {
      current *= 10;
      current += (c - 0x30);
    }
  }
  return current;
}

void init_processes(const char *path,
                    struct process **process_data,
                    u32 *process_size)
{
  int fd = open(path, O_RDONLY);
  if (fd == -1)
  {
    int err = errno;
    perror("open");
    exit(err);
  }

  struct stat st;
  if (fstat(fd, &st) == -1)
  {
    int err = errno;
    perror("stat");
    exit(err);
  }

  u32 size = st.st_size;
  const char *data_start = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data_start == MAP_FAILED)
  {
    int err = errno;
    perror("mmap");
    exit(err);
  }

  const char *data_end = data_start + size;
  const char *data = data_start;

  *process_size = next_int(&data, data_end);

  *process_data = calloc(sizeof(struct process), *process_size);
  if (*process_data == NULL)
  {
    int err = errno;
    perror("calloc");
    exit(err);
  }

  for (u32 i = 0; i < *process_size; ++i)
  {
    (*process_data)[i].pid = next_int(&data, data_end);
    (*process_data)[i].arrival_time = next_int(&data, data_end);
    (*process_data)[i].burst_time = next_int(&data, data_end);
  }

  munmap((void *)data, size);
  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    return EINVAL;
  }
  struct process *data;
  u32 size;
  init_processes(argv[1], &data, &size);

  u32 quantum_length = next_int_from_c_str(argv[2]);

  struct process_list list;
  TAILQ_INIT(&list);

  u32 total_waiting_time = 0;
  u32 total_response_time = 0;

  /* Your code here */
  u32 cur_time = data[0].arrival_time; // Tracks the earliest start time

  for (u32 i = 0; i < size; i++) {
    data[i].found = false; // Process hasn't started 
    data[i].original_burst_time = data[i].burst_time;

    // The "min" function wasn't working and I'm too lazy to fix it
    if (data[i].arrival_time < cur_time) 
      cur_time = data[i].arrival_time;
  }

  u32 process_time = 0; // Time used by the current process
  u32 processes_queued = 0; // Number of processes ready to run

  // This loop should run until all processes are complete.
  // Each iteration represents one quanta, unless we're doing a context switch.
  while (processes_queued < size || !TAILQ_EMPTY(&list)) {
    // Add newly arrived processes to the linked list 
    for (u32 i = 0; i < size; i++)
      if (data[i].arrival_time == cur_time) {
        TAILQ_INSERT_TAIL(&list, &data[i], pointers);
        processes_queued++;
      }

    // There are no processes operating at the moment
    if (TAILQ_EMPTY(&list)) {
      cur_time++;
      process_time = 0;
    }

    struct process* cur_process = TAILQ_FIRST(&list);

    // Measure response time if applicable
    if (!cur_process->found) {
      total_response_time += cur_time-cur_process->arrival_time;
      cur_process->found = true;
    }

    // If a process has completed, remove it from the queue
    if (cur_process->burst_time == 0) {
      total_waiting_time += cur_time-cur_process->arrival_time-cur_process->original_burst_time;
      TAILQ_REMOVE(&list, cur_process, pointers);
      process_time = 0;
    }

    // If a process has used up its quantum, place it at the back of the queue
    else if (process_time == quantum_length) {
      TAILQ_REMOVE(&list, cur_process, pointers);
      TAILQ_INSERT_TAIL(&list, cur_process, pointers);
      process_time = 0;
    }

    // We only increase time measurements if the current process is actively running,
    // i.e. it has not completed or used up its quantum. Otherwise, we're scheduling.
    else {
      cur_time++;
      process_time++;
      cur_process->burst_time--;
    }
  }
  /* End of "Your code here" */

  printf("Average waiting time: %.2f\n", (float)total_waiting_time / (float)size);
  printf("Average response time: %.2f\n", (float)total_response_time / (float)size);

  free(data);
  return 0;
}
