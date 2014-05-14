#ifndef CPOLLTASKSCHEDULER_H
#define CPOLLTASKSCHEDULER_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//	Poll() based task scheduler for live555 code
//	Author : Sergey Kuprienko / demthedj@gmail.com
//	
//	Use instead of stock BasicTaskScheduler to achieve more than 1024 file descriptors for your application
//	Built and teste only under linux 32/64 version
//
//////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BasicUsageEnvironment0.hh"
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <QVector>
#include <QMap>

#define CPollTaskScheduler_MAX_SOCKETS_COUNT (1000)
///< per scheduler. 1K is big enough, usually <100 is used.

typedef struct _CPollTaskSchedulerSocketDesc {
    int socket;
    int conditionSet;
} CPollTaskSchedulerSocketDesc, * PCPollTaskSchedulerSocketDesc;

class CPollTaskScheduler: public BasicTaskScheduler0 {
public:
  static CPollTaskScheduler* createNew(unsigned maxSchedulerGranularity = 10000/*microseconds*/);
    // "maxSchedulerGranularity" (default value: 10 ms) specifies the maximum time that we wait (in "select()") before
    // returning to the event loop to handle non-socket or non-timer-based events, such as 'triggered events'.
    // You can change this is you wish (but only if you know what you're doing!), or set it to 0, to specify no such maximum time.
    // (You should set it to 0 only if you know that you will not be using 'event triggers'.)
  virtual ~CPollTaskScheduler();

protected:
  CPollTaskScheduler(unsigned maxSchedulerGranularity);
      // called only by "createNew()"

  static void schedulerTickTask(void* clientData);
  void schedulerTickTask();

protected:
  // Redefined virtual functions:
  virtual void SingleStep(unsigned maxDelayTime);

  virtual void setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc* handlerProc, void* clientData);
  virtual void moveSocketHandling(int oldSocketNum, int newSocketNum);

protected:
  unsigned fMaxSchedulerGranularity;

  // To implement background operations:
//  int fMaxNumSockets;
//  fd_set fReadSet;
//  fd_set fWriteSet;
//  fd_set fExceptionSet;
  QVector<CPollTaskSchedulerSocketDesc> socketsDescs;
  struct pollfd pollFds [ CPollTaskScheduler_MAX_SOCKETS_COUNT ];
private:
  int GetResultconditionSet_( int socket );
  int LookupSocketIndex_( int socket );
  int GetResultconditionSetFromMap_(int socket, QMap<int,struct pollfd*> & map);
};

#endif // CPOLLTASKSCHEDULER_H
