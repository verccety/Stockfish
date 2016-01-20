/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm> // For std::count
#include <cassert>

#include "movegen.h"
#include "search.h"
#include "thread.h"
#include "uci.h"

using namespace Search;

ThreadPool Threads; // Global object

/// Thread constructor launches the thread and then waits until it goes to sleep
/// in idle_loop().

Thread::Thread() {

  resetCalls = exit = false;
  maxPly = callsCnt = 0;
  history.clear();
  counterMoves.clear();
  idx = Threads.size(); // Start from 0

  std::unique_lock<Mutex> lk(mutex);
  searching = true;
  nativeThread = std::thread(&Thread::idle_loop, this);
  sleepCondition.wait(lk, [&]{ return !searching; });
}


/// Thread destructor waits for thread termination before returning

Thread::~Thread() {

  mutex.lock();
  exit = true;
  sleepCondition.notify_one();
  mutex.unlock();
  nativeThread.join();
}


<<<<<<< HEAD
/// Thread::wait_for_search_finished() waits on sleep condition
/// until not searching

void Thread::wait_for_search_finished() {
=======
// ThreadBase::wait_for() set the thread to sleep until 'condition' turns true

void ThreadBase::wait_for(volatile const bool& condition) {
>>>>>>> parent of ecc5ff6... Lazy SMP

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return !searching; });
}


<<<<<<< HEAD
/// Thread::wait() waits on sleep condition until condition is true

void Thread::wait(std::atomic_bool& condition) {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return bool(condition); });
}


/// Thread::start_searching() wakes up the thread that will start the search

void Thread::start_searching(bool resume) {

  std::unique_lock<Mutex> lk(mutex);

  if (!resume)
      searching = true;

  sleepCondition.notify_one();
}


/// Thread::idle_loop() is where the thread is parked when it has no work to do
=======
// Thread c'tor makes some init but does not launch any execution thread that
// will be started only when c'tor returns.

Thread::Thread() /* : splitPoints() */ { // Initialization of non POD broken in MSVC

  searching = false;
  maxPly = 0;
  splitPointsSize = 0;
  activeSplitPoint = nullptr;
  activePosition = nullptr;
  idx = Threads.size(); // Starts from 0
}


// Thread::cutoff_occurred() checks whether a beta cutoff has occurred in the
// current active split point, or in some ancestor of the split point.

bool Thread::cutoff_occurred() const {

  for (SplitPoint* sp = activeSplitPoint; sp; sp = sp->parentSplitPoint)
      if (sp->cutoff)
          return true;

  return false;
}


// Thread::can_join() checks whether the thread is available to join the split
// point 'sp'. An obvious requirement is that thread must be idle. With more than
// two threads, this is not sufficient: If the thread is the master of some split
// point, it is only available as a slave for the split points below his active
// one (the "helpful master" concept in YBWC terminology).

bool Thread::can_join(const SplitPoint* sp) const {

  if (searching)
      return false;

  // Make a local copy to be sure it doesn't become zero under our feet while
  // testing next condition and so leading to an out of bounds access.
  const size_t size = splitPointsSize;

  // No split points means that the thread is available as a slave for any
  // other thread otherwise apply the "helpful master" concept if possible.
  return !size || splitPoints[size - 1].slavesMask.test(sp->master->idx);
}


// Thread::split() does the actual work of distributing the work at a node between
// several available threads. If it does not succeed in splitting the node
// (because no idle threads are available), the function immediately returns.
// If splitting is possible, a SplitPoint object is initialized with all the
// data that must be copied to the helper threads and then helper threads are
// informed that they have been assigned work. This will cause them to instantly
// leave their idle loops and call search(). When all threads have returned from
// search() then split() returns.

void Thread::split(Position& pos, Stack* ss, Value alpha, Value beta, Value* bestValue,
                   Move* bestMove, Depth depth, int moveCount,
                   MovePicker* movePicker, int nodeType, bool cutNode) {

  assert(searching);
  assert(-VALUE_INFINITE < *bestValue && *bestValue <= alpha && alpha < beta && beta <= VALUE_INFINITE);
  assert(depth >= Threads.minimumSplitDepth);
  assert(splitPointsSize < MAX_SPLITPOINTS_PER_THREAD);

  // Pick and init the next available split point
  SplitPoint& sp = splitPoints[splitPointsSize];

  sp.spinlock.acquire(); // No contention here until we don't increment splitPointsSize

  sp.master = this;
  sp.parentSplitPoint = activeSplitPoint;
  sp.slavesMask = 0, sp.slavesMask.set(idx);
  sp.depth = depth;
  sp.bestValue = *bestValue;
  sp.bestMove = *bestMove;
  sp.alpha = alpha;
  sp.beta = beta;
  sp.nodeType = nodeType;
  sp.cutNode = cutNode;
  sp.movePicker = movePicker;
  sp.moveCount = moveCount;
  sp.pos = &pos;
  sp.nodes = 0;
  sp.cutoff = false;
  sp.ss = ss;
  sp.allSlavesSearching = true; // Must be set under lock protection

  ++splitPointsSize;
  activeSplitPoint = &sp;
  activePosition = nullptr;

  // Try to allocate available threads
  Thread* slave;

  while (    sp.slavesMask.count() < MAX_SLAVES_PER_SPLITPOINT
         && (slave = Threads.available_slave(&sp)) != nullptr)
  {
     slave->spinlock.acquire();

      if (slave->can_join(activeSplitPoint))
      {
          activeSplitPoint->slavesMask.set(slave->idx);
          slave->activeSplitPoint = activeSplitPoint;
          slave->searching = true;
      }

      slave->spinlock.release();
  }

  // Everything is set up. The master thread enters the idle loop, from which
  // it will instantly launch a search, because its 'searching' flag is set.
  // The thread will return from the idle loop when all slaves have finished
  // their work at this split point.
  sp.spinlock.release();

  Thread::idle_loop(); // Force a call to base class idle_loop()

  // In the helpful master concept, a master can help only a sub-tree of its
  // split point and because everything is finished here, it's not possible
  // for the master to be booked.
  assert(!searching);
  assert(!activePosition);

  // We have returned from the idle loop, which means that all threads are
  // finished. Note that decreasing splitPointsSize must be done under lock
  // protection to avoid a race with Thread::can_join().
  spinlock.acquire();

  searching = true;
  --splitPointsSize;
  activeSplitPoint = sp.parentSplitPoint;
  activePosition = &pos;

  spinlock.release();

  // Split point data cannot be changed now, so no need to lock protect
  pos.set_nodes_searched(pos.nodes_searched() + sp.nodes);
  *bestMove = sp.bestMove;
  *bestValue = sp.bestValue;
}


// TimerThread::idle_loop() is where the timer thread waits Resolution milliseconds
// and then calls check_time(). When not searching, thread sleeps until it's woken up.
>>>>>>> parent of ecc5ff6... Lazy SMP

void TimerThread::idle_loop() {

  while (!exit)
  {
      std::unique_lock<Mutex> lk(mutex);

<<<<<<< HEAD
      searching = false;

      while (!searching && !exit)
=======
      if (!exit)
          sleepCondition.wait_for(lk, std::chrono::milliseconds(run ? Resolution : INT_MAX));

      lk.unlock();

      if (run)
          check_time();
  }
}


// MainThread::idle_loop() is where the main thread is parked waiting to be started
// when there is a new search. The main thread will launch all the slave threads.

void MainThread::idle_loop() {

  while (!exit)
  {
      std::unique_lock<Mutex> lk(mutex);

      thinking = false;

      while (!thinking && !exit)
>>>>>>> parent of ecc5ff6... Lazy SMP
      {
          sleepCondition.notify_one(); // Wake up any waiting thread
          sleepCondition.wait(lk);
      }

      lk.unlock();

      if (!exit)
<<<<<<< HEAD
          search();
=======
      {
          searching = true;

          Search::think();

          assert(searching);

          searching = false;
      }
>>>>>>> parent of ecc5ff6... Lazy SMP
  }
}


<<<<<<< HEAD
/// ThreadPool::init() creates and launches requested threads that will go
/// immediately to sleep. We cannot use a constructor because Threads is a
/// static object and we need a fully initialized engine at this point due to
/// allocation of Endgames in the Thread constructor.
=======
// MainThread::join() waits for main thread to finish the search

void MainThread::join() {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return !thinking; });
}


// ThreadPool::init() is called at startup to create and launch requested threads,
// that will go immediately to sleep. We cannot use a c'tor because Threads is a
// static object and we need a fully initialized engine at this point due to
// allocation of Endgames in Thread c'tor.
>>>>>>> parent of ecc5ff6... Lazy SMP

void ThreadPool::init() {

  push_back(new MainThread);
  read_uci_options();
}


/// ThreadPool::exit() terminates threads before the program exits. Cannot be
/// done in destructor because threads must be terminated before deleting any
/// static objects while still in main().

void ThreadPool::exit() {

  while (size())
      delete back(), pop_back();
}


/// ThreadPool::read_uci_options() updates internal threads parameters from the
/// corresponding UCI options and creates/destroys threads to match requested
/// number. Thread objects are dynamically allocated.

void ThreadPool::read_uci_options() {

<<<<<<< HEAD
  size_t requested = Options["Threads"];
=======
  minimumSplitDepth = Options["Min Split Depth"] * ONE_PLY;
  size_t requested  = Options["Threads"];
>>>>>>> parent of ecc5ff6... Lazy SMP

  assert(requested > 0);

  while (size() < requested)
      push_back(new Thread);

  while (size() > requested)
      delete back(), pop_back();
}


<<<<<<< HEAD
/// ThreadPool::nodes_searched() returns the number of nodes searched
=======
// ThreadPool::available_slave() tries to find an idle thread which is available
// to join SplitPoint 'sp'.

Thread* ThreadPool::available_slave(const SplitPoint* sp) const {
>>>>>>> parent of ecc5ff6... Lazy SMP

  for (Thread* th : *this)
      if (th->can_join(sp))
          return th;

<<<<<<< HEAD
  int64_t nodes = 0;
  for (Thread* th : *this)
      nodes += th->rootPos.nodes_searched();
  return nodes;
=======
  return nullptr;
>>>>>>> parent of ecc5ff6... Lazy SMP
}


/// ThreadPool::start_thinking() wakes up the main thread sleeping in idle_loop()
/// and starts a new search, then returns immediately.

void ThreadPool::start_thinking(const Position& pos, const LimitsType& limits,
                                StateStackPtr& states) {

  main()->wait_for_search_finished();

  Signals.stopOnPonderhit = Signals.stop = false;

  RootMoves.clear();
  RootPos = pos;
  Limits = limits;
  if (states.get()) // If we don't set a new position, preserve current state
  {
      SetupStates = std::move(states); // Ownership transfer here
      assert(!states.get());
  }

  for (const auto& m : MoveList<LEGAL>(pos))
      if (   limits.searchmoves.empty()
          || std::count(limits.searchmoves.begin(), limits.searchmoves.end(), m))
          RootMoves.push_back(RootMove(m));

  main()->start_searching();
}
