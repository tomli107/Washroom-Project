

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_sem.h"

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__);
#else
#define VERBOSE_PRINT(S, ...) ;
#endif

#define MAX_OCCUPANCY      3
#define NUM_ITERATIONS     100
#define NUM_PEOPLE         20

#define WAITING_HISTOGRAM_SIZE (NUM_ITERATIONS * NUM_PEOPLE)
int           entryTicker;
int           waitingHistogram         [WAITING_HISTOGRAM_SIZE];
int           waitingHistogramOverflow;
uthread_sem_t waitingHistogramMutex;
int           occupancyHistogram       [2] [MAX_OCCUPANCY + 1];

enum Sex {MALE = 0, FEMALE = 1};
const static enum Sex otherSex [] = {FEMALE, MALE};

struct Washroom {
  uthread_sem_t mutex;
  uthread_sem_t done;
  uthread_sem_t canEnter [2];
  int           occupantCount;
  enum Sex      occupantSex;
  int           waitersCount [2];
};

struct Washroom* createWashroom() {
  struct Washroom* washroom = malloc (sizeof (struct Washroom));
  washroom->mutex                 = uthread_sem_create (1);
  washroom->done                  = uthread_sem_create (0);
  washroom->canEnter [MALE]       = uthread_sem_create (0);
  washroom->canEnter [FEMALE]     = uthread_sem_create (0);
  washroom->occupantCount         = 0;
  washroom->occupantSex           = 0;
  washroom->waitersCount [MALE]   = 0;
  washroom->waitersCount [FEMALE] = 0;
  return washroom;
}

void recordEntry (struct Washroom* washroom, enum Sex Sex) {
  assert (washroom->occupantCount == 0 || washroom->occupantSex == Sex);
  assert (washroom->occupantCount < MAX_OCCUPANCY);
  washroom->occupantCount  ++;
  washroom->occupantSex = Sex;
  entryTicker++;
  occupancyHistogram [washroom->occupantSex] [washroom->occupantCount] ++;
}

void enterWashroom (struct Washroom* washroom, enum Sex Sex) {
  uthread_sem_wait (washroom->mutex);
    int isEmpty         = washroom->occupantCount                  == 0;
    int hasRoom         = washroom->occupantCount                  <  MAX_OCCUPANCY;
    int sameSex         = washroom->occupantSex                    == Sex;
    int otherSexWaiting = washroom->waitersCount [otherSex [Sex]]  >  0;
    int canEnter        = (isEmpty || (hasRoom && sameSex && ! otherSexWaiting));
    if (canEnter)
      recordEntry (washroom, Sex);
    else
      washroom->waitersCount [Sex] ++;
  uthread_sem_signal (washroom->mutex);
  if (! canEnter)
    uthread_sem_wait (washroom->canEnter [Sex]);
}

void leaveWashroom (struct Washroom* washroom) {
  uthread_sem_wait (washroom->mutex);
    washroom->occupantCount -= 1;
    enum Sex inSex  = washroom->occupantSex;
    enum Sex outSex = otherSex [inSex];
    if (washroom->waitersCount [outSex] > 0) {
      if (washroom->occupantCount == 0) {
        for (int i = 0; i < washroom->waitersCount [outSex] && i < MAX_OCCUPANCY; i++) {
          washroom->waitersCount [outSex] --;
          recordEntry (washroom, outSex);
          uthread_sem_signal (washroom->canEnter [outSex]);
        }
      }
    } else if (washroom->waitersCount [inSex]) {
      washroom->waitersCount [inSex] --;
      recordEntry (washroom, inSex);
      uthread_sem_signal (washroom->canEnter [inSex]);
    }
  uthread_sem_signal (washroom->mutex);
}

void recordWaitingTime (int waitingTime) {
  uthread_sem_wait (waitingHistogramMutex);
    if (waitingTime < WAITING_HISTOGRAM_SIZE)
      waitingHistogram [waitingTime] ++;
    else
      waitingHistogramOverflow ++;
  uthread_sem_signal (waitingHistogramMutex);
}

void* person (void* washroomv) {
  struct Washroom* washroom = washroomv;
  enum Sex      Sex   = random() & 1;
  
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int startTime = entryTicker;
    enterWashroom (washroom, Sex);
    recordWaitingTime (entryTicker - startTime - 1);
    for (int i = 0; i < NUM_PEOPLE; i++) uthread_yield();
    leaveWashroom (washroom);
    for (int i = 0; i < NUM_PEOPLE; i++) uthread_yield();
  }
  uthread_sem_signal (washroom->done);
  return NULL;
}

void mysrandomdev() {
  unsigned long seed;
  int f = open ("/dev/random", O_RDONLY);
  read    (f, &seed, sizeof (seed));
  close   (f);
  srandom (seed);
}

int main (int argc, char** argv) {
  uthread_init (1);
  mysrandomdev();
  struct Washroom* washroom = createWashroom();
  waitingHistogramMutex     = uthread_sem_create (1);
  
  for (int i = 0; i < NUM_PEOPLE; i++)
    uthread_detach (uthread_create (person, washroom));
  
  for (int i = 0; i < NUM_PEOPLE; i++)
    uthread_sem_wait (washroom->done);
  
  printf ("Times with 1 male    %d\n", occupancyHistogram [MALE]   [1]);
  printf ("Times with 2 males   %d\n", occupancyHistogram [MALE]   [2]);
  printf ("Times with 3 males   %d\n", occupancyHistogram [MALE]   [3]);
  printf ("Times with 1 female  %d\n", occupancyHistogram [FEMALE] [1]);
  printf ("Times with 2 females %d\n", occupancyHistogram [FEMALE] [2]);
  printf ("Times with 3 females %d\n", occupancyHistogram [FEMALE] [3]);
  printf ("Waiting Histogram\n");
  for (int i=0; i<WAITING_HISTOGRAM_SIZE; i++)
    if (waitingHistogram [i])
      printf ("  Number of times people waited for %d %s to enter: %d\n", i, i==1?"person":"people", waitingHistogram [i]);
  if (waitingHistogramOverflow)
    printf ("  Number of times people waited more than %d entries: %d\n", WAITING_HISTOGRAM_SIZE, waitingHistogramOverflow);
}