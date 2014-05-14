#include "cpolltaskscheduler.h"

#include "BasicUsageEnvironment.hh"
#include "HandlerSet.hh"
#include <stdio.h>
#include <QMap>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//	Poll() based task scheduler for live555 code
//	Author : Sergey Kuprienko / demthedj@gmail.com
//	
//	Use instead of stock BasicTaskScheduler to achieve more than 1024 file descriptors for your application
//	Built and teste only under linux 32/64 version
//
//////////////////////////////////////////////////////////////////////////////////////////////////////


////////// CPollTaskScheduler //////////

CPollTaskScheduler* CPollTaskScheduler::createNew(unsigned maxSchedulerGranularity) {
    return new CPollTaskScheduler(maxSchedulerGranularity);
}

CPollTaskScheduler::CPollTaskScheduler(unsigned maxSchedulerGranularity)
  : fMaxSchedulerGranularity(maxSchedulerGranularity)
//    fMaxNumSockets(0),
  //  pollfdsCount(0)
{
    socketsDescs.reserve(CPollTaskScheduler_MAX_SOCKETS_COUNT + 1);
  //FD_ZERO(&fReadSet);
  //FD_ZERO(&fWriteSet);
  //FD_ZERO(&fExceptionSet);

  if (maxSchedulerGranularity > 0) schedulerTickTask(); // ensures that we handle events frequently
}

CPollTaskScheduler::~CPollTaskScheduler() {
}

void CPollTaskScheduler::schedulerTickTask(void* clientData) {
  ((CPollTaskScheduler*)clientData)->schedulerTickTask();
}

void CPollTaskScheduler::schedulerTickTask() {
  scheduleDelayedTask(fMaxSchedulerGranularity, schedulerTickTask, this);
}

#ifndef MILLION
#define MILLION 1000000
#endif

int CPollTaskScheduler::LookupSocketIndex_( int socket ) {
    int socketIndex  = -1;
    for (int z=0; z < this->socketsDescs.count();z++) {
        if (this->socketsDescs.at(z).socket == socket) {
            socketIndex = z;
            break;
        }
    }
    return socketIndex;
}

int CPollTaskScheduler::GetResultconditionSet_( int socket ) {
    int socketIndex = this->LookupSocketIndex_(socket);
    int resultConditionSet = 0;
    if (socketIndex != -1) {
        // bydem : urgent data is smth we're not ready to process - so it must fail !
        if ((pollFds[socketIndex].revents & (POLLIN )) != 0x00) resultConditionSet |= SOCKET_READABLE;
        if ((pollFds[socketIndex].revents & (POLLOUT)) != 0x00) resultConditionSet |= SOCKET_WRITABLE;
        if ((pollFds[socketIndex].revents & (POLLERR | POLLHUP | POLLNVAL | POLLPRI)) != 0x00) resultConditionSet |= SOCKET_EXCEPTION;
    }
    return resultConditionSet;
}

int CPollTaskScheduler::GetResultconditionSetFromMap_( int socket, QMap<int,struct pollfd*> & map ) {
    struct pollfd * fd = map.value(socket,0);
    int resultConditionSet = 0;
    if (fd != 0) {
        // bydem : urgent data is smth we're not ready to process - so it must fail !
        if ((fd->revents & (POLLIN )) != 0x00) resultConditionSet |= SOCKET_READABLE;
        if ((fd->revents & (POLLOUT)) != 0x00) resultConditionSet |= SOCKET_WRITABLE;
        if ((fd->revents & (POLLERR | POLLHUP | POLLNVAL | POLLPRI)) != 0x00) resultConditionSet |= SOCKET_EXCEPTION;
    }
    return resultConditionSet;
}


void CPollTaskScheduler::SingleStep(unsigned maxDelayTime) {
    //

  //QMap<int,struct pollfd*> fdsCacheMap;//bydem : map is slower that vector due to fill overhead
  memset(this->pollFds, 0, sizeof(struct pollfd)* CPollTaskScheduler_MAX_SOCKETS_COUNT);
  for (int z=0; z < this->socketsDescs.count(); z++) {
      PCPollTaskSchedulerSocketDesc desc = &this->socketsDescs[z];

      //fdsCacheMap[desc->socket] = &pollFds[z];
      pollFds[z].fd = desc->socket;
      int conditionSet = desc->conditionSet;
      if (conditionSet & SOCKET_READABLE)  { pollFds[z].events |= POLLIN | POLLPRI; };
      if (conditionSet & SOCKET_WRITABLE)  { pollFds[z].events |= POLLOUT; };
      if (conditionSet & SOCKET_EXCEPTION)  { pollFds[z].events |= POLLERR | POLLHUP; };
    }

  DelayInterval const& timeToDelay = fDelayQueue.timeToNextAlarm();
  int timeoutMs = timeToDelay.seconds() * 1000 + timeToDelay.useconds() / 1000;
  if (timeoutMs > maxDelayTime / 1000) {
      timeoutMs = maxDelayTime / 1000;
  }
  if (timeoutMs == 0) {
      timeoutMs = 1;
  }

  int eventsCount = poll(this->pollFds, this->socketsDescs.count(), timeoutMs);

  if (eventsCount < 0) {
      if (errno != EINTR && errno != EAGAIN) {
          fprintf(stderr,"CPollTaskSchedulerSocket::SingleStep() : poll failed, errno %d, sockets : ", errno);
          for (int z=0; z < this->socketsDescs.count();z++) {
              fprintf(stderr,"%d ", this->socketsDescs.at(z));
          }
          fprintf(stderr,"\n");
          internalError();
      }
  }


  // Call the handler function for one readable socket:
  HandlerIterator iter(*fHandlers);
  HandlerDescriptor* handler = 0;

  if (eventsCount) {
      // To ensure forward progress through the handlers, begin past the last
      // socket number that we handled:
      if (fLastHandledSocketNum >= 0) {
        while ((handler = iter.next()) != NULL) {
          if (handler->socketNum == fLastHandledSocketNum) break;
        }
        if (handler == NULL) {
          fLastHandledSocketNum = -1;
          iter.reset(); // start from the beginning instead
        }
      }
      while ((handler = iter.next()) != NULL) {
        int sock = handler->socketNum; // alias
        int resultConditionSet = this->GetResultconditionSet_(sock);//this->GetResultconditionSetFromMap_(sock,fdsCacheMap);
        if ((resultConditionSet&handler->conditionSet) != 0 && handler->handlerProc != NULL) {
          fLastHandledSocketNum = sock;
              // Note: we set "fLastHandledSocketNum" before calling the handler,
              // in case the handler calls "doEventLoop()" reentrantly.
          (*handler->handlerProc)(handler->clientData, resultConditionSet);
          break;
        }
      }
      if (handler == NULL && fLastHandledSocketNum >= 0) {
        // We didn't call a handler, but we didn't get to check all of them,
        // so try again from the beginning:
        iter.reset();
        while ((handler = iter.next()) != NULL) {
          int sock = handler->socketNum; // alias
          int resultConditionSet = this->GetResultconditionSet_(sock);//this->GetResultconditionSetFromMap_(sock,fdsCacheMap);
          if ((resultConditionSet&handler->conditionSet) != 0 && handler->handlerProc != NULL) {
                fLastHandledSocketNum = sock;
                // Note: we set "fLastHandledSocketNum" before calling the handler,
                // in case the handler calls "doEventLoop()" reentrantly.
                (*handler->handlerProc)(handler->clientData, resultConditionSet);
                break;
            }
        }// while
      }
    }//events count ?

  if (handler == NULL) fLastHandledSocketNum = -1;//because we didn't call a handler


  // Also handle any newly-triggered event (Note that we do this *after* calling a socket handler,
  // in case the triggered event handler modifies The set of readable sockets.)
  if (fTriggersAwaitingHandling != 0) {
    if (fTriggersAwaitingHandling == fLastUsedTriggerMask) {
      // Common-case optimization for a single event trigger:
      fTriggersAwaitingHandling = 0;
      if (fTriggeredEventHandlers[fLastUsedTriggerNum] != NULL) {
    (*fTriggeredEventHandlers[fLastUsedTriggerNum])(fTriggeredEventClientDatas[fLastUsedTriggerNum]);
      }
    } else {
      // Look for an event trigger that needs handling (making sure that we make forward progress through all possible triggers):
      unsigned i = fLastUsedTriggerNum;
      EventTriggerId mask = fLastUsedTriggerMask;

      do {
    i = (i+1)%MAX_NUM_EVENT_TRIGGERS;
    mask >>= 1;
    if (mask == 0) mask = 0x80000000;

    if ((fTriggersAwaitingHandling&mask) != 0) {
      fTriggersAwaitingHandling &=~ mask;
      if (fTriggeredEventHandlers[i] != NULL) {
        (*fTriggeredEventHandlers[i])(fTriggeredEventClientDatas[i]);
      }

      fLastUsedTriggerMask = mask;
      fLastUsedTriggerNum = i;
      break;
    }
      } while (i != fLastUsedTriggerNum);
    }
  }

  // Also handle any delayed event that may have come due.
  fDelayQueue.handleAlarm();
}

void CPollTaskScheduler
  ::setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc* handlerProc, void* clientData) {

//    fprintf(stderr,"CPollTaskScheduler::setBackgroundHandling(%d socketNum, %d conditionSet) \n", socketNum, conditionSet);

  if (socketNum < 0) return;

  if (this->socketsDescs.count() >= CPollTaskScheduler_MAX_SOCKETS_COUNT) {
      fprintf(stderr,"CPollTaskSchedulerSocket::setBackgroundHandling() : CPollTaskScheduler_MAX_SOCKETS_COUNT (%d) reached !\n", CPollTaskScheduler_MAX_SOCKETS_COUNT);
      internalError();
  }
  int socketIndex = this->LookupSocketIndex_(socketNum);
  if (conditionSet == 0) {
      if (socketIndex != -1) {
          this->socketsDescs.remove(socketIndex);
      }
    fHandlers->clearHandler(socketNum);
  } else {
      if (socketIndex != -1) {
          this->socketsDescs[socketIndex].conditionSet = conditionSet;
      } else {
          CPollTaskSchedulerSocketDesc socket;
          socket.socket = socketNum;
          socket.conditionSet = conditionSet;
          this->socketsDescs.append(socket);
    }

    fHandlers->assignHandler(socketNum, conditionSet, handlerProc, clientData);
  }
}

void CPollTaskScheduler::moveSocketHandling(int oldSocketNum, int newSocketNum) {
//    fprintf(stderr,"CPollTaskScheduler::moveSocketHandling(%d oldSocketNum, %d newSocketNum)\n", oldSocketNum, newSocketNum);
  if (oldSocketNum < 0 || newSocketNum < 0) return; // sanity check
  int socketIndex = this->LookupSocketIndex_(oldSocketNum);
  if (socketIndex != -1) {
      this->socketsDescs[socketIndex].socket = newSocketNum;
  }

  fHandlers->moveHandler(oldSocketNum, newSocketNum);
}







