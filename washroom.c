
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__);
#else
#define VERBOSE_PRINT(S, ...) ;
#endif

#define MAX_OCCUPANCY      3
#define NUM_ITERATIONS     100
#define NUM_PEOPLE         20
#define FAIR_WAITING_COUNT 4

enum Sex {MALE = 0, FEMALE = 1};
const static enum Sex otherSex [] = {FEMALE, MALE};

struct Washroom {
  uthread_mutex_t mutex;
  uthread_cond_t  canEnter [2];
  int             occupantCount;
  enum Sex        occupantSex;
  int             waitersCount [2];
  int             outSexWaitingCount;
};

struct Washroom* createWashroom() {
  struct Washroom* washroom = malloc (sizeof (struct Washroom));
  washroom->mutex                 = uthread_mutex_create();
  washroom->canEnter [MALE]       = uthread_cond_create (washroom->mutex);
  washroom->canEnter [FEMALE]     = uthread_cond_create (washroom->mutex);
  washroom->occupantCount         = 0;
  washroom->occupantSex           = 0;
  washroom->waitersCount [MALE]   = 0;
  washroom->waitersCount [FEMALE] = 0;
  washroom->outSexWaitingCount    = 0;
  return washroom;
}

#define WAITING_HISTOGRAM_SIZE (NUM_ITERATIONS * NUM_PEOPLE)
int             entryTicker;
int             waitingHistogram         [WAITING_HISTOGRAM_SIZE];
int             waitingHistogramOverflow;
uthread_mutex_t waitingHistogrammutex;
int             occupancyHistogram       [2] [MAX_OCCUPANCY + 1];

void enterWashroom (struct Washroom* washroom, enum Sex Sex) {
  uthread_mutex_lock (washroom->mutex);
    while (1) {
      int isEmpty         = washroom->occupantCount                  == 0;
      int hasRoom         = washroom->occupantCount                  <  MAX_OCCUPANCY;
      int sameSex         = washroom->occupantSex                    == Sex;
      int otherSexWaiting = washroom->waitersCount [otherSex [Sex]]  >  0;
      int waitingNotFair  = washroom->outSexWaitingCount             >= FAIR_WAITING_COUNT;
      int otherSexsTurn   = otherSexWaiting && waitingNotFair;
      if (isEmpty || (hasRoom && sameSex && ! otherSexsTurn)) {
        if (sameSex)
          washroom->outSexWaitingCount ++;
        else
          washroom->outSexWaitingCount = 0;
        entryTicker ++;
        break;
      }
      VERBOSE_PRINT ("waiting to enter: %d %d %d %d %d %d\n", Sex, isEmpty, hasRoom, sameSex, otherSexWaiting, waitingNotFair);
      if (! sameSex && washroom->waitersCount [Sex] == 0)
        washroom->outSexWaitingCount = 0;
      washroom->waitersCount [Sex] ++;
      uthread_cond_wait (washroom->canEnter [Sex]);
      washroom->waitersCount [Sex] --;
    }
    VERBOSE_PRINT ("entering %d %d\n", Sex, washroom->occupantCount+1);
    assert (washroom->occupantCount == 0 || washroom->occupantSex == Sex);
    assert (washroom->occupantCount < MAX_OCCUPANCY);
    washroom->occupantSex = Sex;
    washroom->occupantCount += 1;
    occupancyHistogram [washroom->occupantSex] [washroom->occupantCount] ++;
  uthread_mutex_unlock (washroom->mutex);
}

void leaveWashroom (struct Washroom* washroom) {
  uthread_mutex_lock (washroom->mutex);
    washroom->occupantCount -= 1;
    enum Sex inSex          = washroom->occupantSex;
    enum Sex outSex         = otherSex [inSex];
    int      outSexWaiting  = washroom->waitersCount [outSex]  >  0;
    int      waitingNotFair = washroom->outSexWaitingCount     >= FAIR_WAITING_COUNT;
    int      inSexWaiting   = washroom->waitersCount [inSex]   >  0;
    VERBOSE_PRINT ("leaving %d %d %d %d\n", inSex, outSexWaiting, inSexWaiting, waitingNotFair);
    if (outSexWaiting && (waitingNotFair || ! inSexWaiting)) {
      if (washroom->occupantCount == 0) {
        for (int i = 0; i < MAX_OCCUPANCY; i++) {
          VERBOSE_PRINT ("signalling out Sex %d\n", outSex);
          uthread_cond_signal (washroom->canEnter [outSex]);
        }
      }
    } else if (inSexWaiting) {
      VERBOSE_PRINT ("signalling in Sex %d\n", inSex);
      uthread_cond_signal (washroom->canEnter [inSex]);
    }
  uthread_mutex_unlock (washroom->mutex);
}

void recordWaitingTime (int waitingTime) {
  uthread_mutex_lock (waitingHistogrammutex);
  if (waitingTime < WAITING_HISTOGRAM_SIZE)
    waitingHistogram [waitingTime] ++;
  else
    waitingHistogramOverflow ++;
  uthread_mutex_unlock (waitingHistogrammutex);
}

void* person (void* washroomv) {
  struct Washroom* washroom = washroomv;
  enum Sex         Sex      = random() & 1;
  
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int startTime = entryTicker;
    enterWashroom (washroom, Sex);
    recordWaitingTime (entryTicker - startTime - 1);
    for (int j = 0; j < NUM_PEOPLE; j++) uthread_yield();
    leaveWashroom (washroom);
    for (int j = 0; j < NUM_PEOPLE; j++) uthread_yield();
  }
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
  uthread_t        pt [NUM_PEOPLE];
  waitingHistogrammutex = uthread_mutex_create ();
  
  for (int i = 0; i < NUM_PEOPLE; i++)
    pt [i] = uthread_create (person, washroom);
  for (int i = 0; i < NUM_PEOPLE; i++)
    uthread_join (pt [i], 0);
    
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