/*         ______   ___    ___
 *        /\  _  \ /\_ \  /\_ \
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *	Internal cross-platform threading API for Windows.
 *
 *	By Peter Wang.
 *
 *      See readme.txt for copyright information.
 */


#include "allegro.h"
#include "allegro/internal/aintern.h"
#include ALLEGRO_INTERNAL_HEADER

#ifndef SCAN_DEPEND
   #include <process.h>
#endif



/* threads */

static void thread_proc_trampoline(void *data)
{
   _AL_THREAD *thread = data;
   (*thread->proc)(thread, thread->arg);
}


void _al_thread_create(_AL_THREAD *thread, void (*proc)(_AL_THREAD*, void*), void *arg)
{
   ASSERT(thread);
   ASSERT(proc);
   {
      int status;

      InitializeCriticalSection(&thread->cs);

      thread->should_stop = false;
      thread->proc = proc;
      thread->arg = arg;

      thread->thread = (void *)_beginthread(thread_proc_trampoline, 0, thread);
   }
}


void _al_thread_join(_AL_THREAD *thread)
{
   ASSERT(thread);

   EnterCriticalSection(&thread->cs);
   {
      thread->should_stop = true;
   }
   LeaveCriticalSection(&thread->cs);
   WaitForSingleObject(thread->thread, INFINITE);

   DeleteCriticalSection(&thread->cs);
}


/* mutexes */

void _al_mutex_init(_AL_MUTEX *mutex)
{
   ASSERT(mutex);

   InitializeCriticalSection(&mutex->cs);
   mutex->inited = true;
}


void _al_mutex_init_recursive(_AL_MUTEX *mutex)
{
   /* These are the same on Windows. */
   _al_mutex_init(mutex);
}


void _al_mutex_destroy(_AL_MUTEX *mutex)
{
   ASSERT(mutex);
   ASSERT(mutex->inited);

   DeleteCriticalSection(&mutex->cs);
   mutex->inited = false;
}


/* condition variables */

/*
 * The algorithm used was originally designed for the pthread-win32
 * package.  I translated the pseudo-code below line for line.
 *
 * --- From pthread-win32: ---
 * Algorithm:
 * The algorithm used in this implementation is that developed by
 * Alexander Terekhov in colaboration with Louis Thomas. The bulk
 * of the discussion is recorded in the file README.CV, which contains
 * several generations of both colaborators original algorithms. The final
 * algorithm used here is the one referred to as
 *
 *     Algorithm 8a / IMPL_SEM,UNBLOCK_STRATEGY == UNBLOCK_ALL
 * 
 * presented below in pseudo-code as it appeared:
 *
 * [snip]
 * -------------------------------------------------------------
 *
 *     Algorithm 9 / IMPL_SEM,UNBLOCK_STRATEGY == UNBLOCK_ALL
 * 
 * presented below in pseudo-code; basically 8a...
 *                                      ...BUT W/O "spurious wakes" prevention:
 *
 *
 * given:
 * semBlockLock - bin.semaphore
 * semBlockQueue - semaphore
 * mtxExternal - mutex or CS
 * mtxUnblockLock - mutex or CS
 * nWaitersGone - int
 * nWaitersBlocked - int
 * nWaitersToUnblock - int
 * 
 * wait( timeout ) {
 * 
 *   [auto: register int result          ]     // error checking omitted
 *   [auto: register int nSignalsWasLeft ]
 * 
 *   sem_wait( semBlockLock );
 *   ++nWaitersBlocked;
 *   sem_post( semBlockLock );
 * 
 *   unlock( mtxExternal );
 *   bTimedOut = sem_wait( semBlockQueue,timeout );
 * 
 *   lock( mtxUnblockLock );
 *   if ( 0 != (nSignalsWasLeft = nWaitersToUnblock) ) {
 *     --nWaitersToUnblock;
 *   }
 *   else if ( INT_MAX/2 == ++nWaitersGone ) { // timeout/canceled or
 *                                             // spurious semaphore :-)
 *     sem_wait( semBlockLock );
 *     nWaitersBlocked -= nWaitersGone;        // something is going on here
 *                                             //  - test of timeouts? :-)
 *     sem_post( semBlockLock );
 *     nWaitersGone = 0;
 *   }
 *   unlock( mtxUnblockLock );
 * 
 *   if ( 1 == nSignalsWasLeft ) {
 *     sem_post( semBlockLock );               // open the gate
 *   }
 * 
 *   lock( mtxExternal );
 * 
 *   return ( bTimedOut ) ? ETIMEOUT : 0;
 * }
 * 
 * signal(bAll) {
 * 
 *   [auto: register int result         ]
 *   [auto: register int nSignalsToIssue]
 * 
 *   lock( mtxUnblockLock );
 * 
 *   if ( 0 != nWaitersToUnblock ) {        // the gate is closed!!!
 *     if ( 0 == nWaitersBlocked ) {        // NO-OP
 *       return unlock( mtxUnblockLock );
 *     }
 *     if (bAll) {
 *       nWaitersToUnblock += nSignalsToIssue=nWaitersBlocked;
 *       nWaitersBlocked = 0;
 *     }
 *     else {
 *       nSignalsToIssue = 1;
 *       ++nWaitersToUnblock;
 *       --nWaitersBlocked;
 *     }
 *   }
 *   else if ( nWaitersBlocked > nWaitersGone ) { // HARMLESS RACE CONDITION!
 *     sem_wait( semBlockLock );                  // close the gate
 *     if ( 0 != nWaitersGone ) {
 *       nWaitersBlocked -= nWaitersGone;
 *       nWaitersGone = 0;
 *     }
 *     if (bAll) {
 *       nSignalsToIssue = nWaitersToUnblock = nWaitersBlocked;
 *       nWaitersBlocked = 0;
 *     }
 *     else {
 *       nSignalsToIssue = nWaitersToUnblock = 1;
 *       --nWaitersBlocked;
 *     }
 *   }
 *   else { // NO-OP
 *     return unlock( mtxUnblockLock );
 *   }
 *
 *   unlock( mtxUnblockLock );
 *   sem_post( semBlockQueue,nSignalsToIssue );
 *   return result;
 * }
 * -------------------------------------------------------------
 */


void _al_cond_init(_AL_COND *cond)
{
   cond->nWaitersBlocked = 0;
   cond->nWaitersGone = 0;
   cond->nWaitersToUnblock = 0;
   cond->semBlockQueue = CreateSemaphore(NULL, 0, INT_MAX, NULL);
   InitializeCriticalSection(&cond->semBlockLock);
   InitializeCriticalSection(&cond->mtxUnblockLock);
}


void _al_cond_destroy(_AL_COND *cond)
{
   DeleteCriticalSection(&cond->mtxUnblockLock);
   DeleteCriticalSection(&cond->semBlockLock);
   CloseHandle(cond->semBlockQueue);
}


/* returns -1 on timeout */
static int cond_wait(_AL_COND *cond, _AL_MUTEX *mtxExternal, DWORD timeout)
{
   int nSignalsWasLeft;
   bool bTimedOut;
   DWORD dwWaitResult;

   EnterCriticalSection(&cond->semBlockLock);
   ++cond->nWaitersBlocked;
   LeaveCriticalSection(&cond->semBlockLock);

   _al_mutex_unlock(mtxExternal);

   dwWaitResult = WaitForSingleObject(cond->semBlockQueue, timeout);
   if (dwWaitResult == WAIT_TIMEOUT)
      bTimedOut = true;
   else if (dwWaitResult == WAIT_OBJECT_0)
      bTimedOut = false;
   else {
      /* bad! what to do? */
      _al_mutex_lock(mtxExternal);
      ASSERT(false);
      return 0;
   }

   EnterCriticalSection(&cond->mtxUnblockLock);
   if (0 != (nSignalsWasLeft = cond->nWaitersToUnblock)) {
      --(cond->nWaitersToUnblock);
   }
   else if (INT_MAX/2 == ++(cond->nWaitersGone)) { /* timeout/canceled or spurious semaphore :-) */
      EnterCriticalSection(&cond->semBlockLock);
      cond->nWaitersBlocked -= cond->nWaitersGone; /* something is going on here
                                                      - test of timeouts? :-) */
      LeaveCriticalSection(&cond->semBlockLock);
      cond->nWaitersGone = 0;
   }
   LeaveCriticalSection(&cond->mtxUnblockLock);

   if (1 == nSignalsWasLeft) {
      LeaveCriticalSection(&cond->semBlockLock); /* open the gate */
   }

   _al_mutex_lock(mtxExternal);

   return bTimedOut ? -1 : 0;
}


void _al_cond_wait(_AL_COND *cond, _AL_MUTEX *mtxExternal)
{
   ASSERT(cond);
   ASSERT(mtxExternal);
   {
      int result = cond_wait(cond, mtxExternal, INFINITE);

      ASSERT(result != -1);
   }
}


int _al_cond_timedwait(_AL_COND *cond, _AL_MUTEX *mtxExternal, unsigned long abstime)
{
   ASSERT(cond);
   ASSERT(mtxExternal);
   {
      DWORD reltime = abstime - al_current_time();

      if (reltime < 0)
         return -1;

      return cond_wait(cond, mtxExternal, reltime);
   }
}


static void cond_signal(_AL_COND *cond, bool bAll)
{
   int nSignalsToIssue;

   EnterCriticalSection(&cond->mtxUnblockLock);

   if (0 != cond->nWaitersToUnblock) {   /* the gate is closed!!! */
      if (0 == cond->nWaitersBlocked) {  /* NO-OP */
         LeaveCriticalSection(&cond->mtxUnblockLock);
         return;
      }
      if (bAll) {
         cond->nWaitersToUnblock += (nSignalsToIssue = cond->nWaitersBlocked);
         cond->nWaitersBlocked = 0;
      }
      else {
         nSignalsToIssue = 1;
         ++cond->nWaitersToUnblock;
         --cond->nWaitersBlocked;
      }
   }
   else if (cond->nWaitersBlocked > cond->nWaitersGone) { /* HARMLESS RACE CONDITION! */
      EnterCriticalSection(&cond->semBlockLock); /* close the gate */
      if (0 != cond->nWaitersGone) {
         cond->nWaitersBlocked -= cond->nWaitersGone;
         cond->nWaitersGone = 0;
      }
      if (bAll) {
         nSignalsToIssue = (cond->nWaitersToUnblock = cond->nWaitersBlocked);
         cond->nWaitersBlocked = 0;
      }
      else {
         nSignalsToIssue = cond->nWaitersToUnblock = 1;
         --cond->nWaitersBlocked;
      }
   }
   else { /* NO-OP */
      LeaveCriticalSection(&cond->mtxUnblockLock);
      return;
   }

   LeaveCriticalSection(&cond->mtxUnblockLock);
   ReleaseSemaphore(cond->semBlockQueue, nSignalsToIssue, NULL);
   return;
}


void _al_cond_broadcast(_AL_COND *cond)
{
   cond_signal(cond, true);
}


void _al_cond_signal(_AL_COND *cond)
{
   cond_signal(cond, false);
}


/*
 * Local Variables:
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
