/**************************************************************************
 *
 * Copyright 2011 Jose Fonseca
 * Copyright (C) 2013 Intel Corporation. All rights reversed.
 * Author: Shuang He <shuang.he@intel.com>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/


#include <string.h>
#include <limits.h> // for CHAR_MAX
#include <iostream>
#include <getopt.h>
#ifndef _WIN32
#include <unistd.h> // for isatty()
#endif

#include "os_binary.hpp"
#include "os_time.hpp"
#include "os_thread.hpp"
#include "image.hpp"
#include "trace_callset.hpp"
#include "trace_dump.hpp"
#include "trace_option.hpp"
#include "play.hpp"


static bool waitOnFinish = false;
static bool loopOnFinish = false;

static const char *snapshotPrefix = NULL;
static enum {
  PNM_FMT,
    RAW_RGB,
    RAW_MD5
} snapshotFormat = PNM_FMT;

static trace::CallSet snapshotFrequency;
static trace::ParseBookmark lastFrameStart;

static unsigned dumpStateCallNo = ~0;

play::Player player;

#define ASYNC_READER_SLEEP 1000
#define ASYNC_READER_CALLS 100000

namespace play {

  os::mutex destroyerMutex;
  std::vector< trace::Call * > retired;
  os::thread * destroyerThread = NULL;

  void delete_retired_calls( ThreadedParser * tp ) {
    os::unique_lock<os::mutex> lock(destroyerMutex);
    std::deque< trace::Call * >::iterator end = tp->retiredCalls.end();
    if( tp->bookmark != ~0 ) {
      for( size_t i = 0; i < tp->retiredCalls.size(); i++ ) {
        if( tp->retiredCalls[i]->no == tp->bookmark ) {
          end = tp->retiredCalls.begin() + i;
          break;
        }
      }
    }
    retired.insert( retired.end(), tp->retiredCalls.begin(), end );
    tp->retiredCalls.erase( tp->retiredCalls.begin(), end );
  }
  
  void async_destroyer() {
    for(;;) {
      os::unique_lock<os::mutex> lock(destroyerMutex);
      for( size_t i = 0; i < retired.size(); i++ ) {
        delete retired[i];
      }
      retired.clear();
    }
  }
  
  
  os::mutex readerMutex;
  ThreadedParser *inbox;
  std::map< ThreadedParser *, std::vector< trace::Call *> > outbox;
  os::thread * readerThread = NULL;
  bool enqueue_read( ThreadedParser * tp ) {
    os::unique_lock<os::mutex> lock(readerMutex);
    if( inbox != 0 ) {
      return false;
    }
    inbox = tp;
    return true;
  }

  bool fetch_read( ThreadedParser * tp ) {
    os::unique_lock<os::mutex> lock(readerMutex);
    if( outbox.count( tp ) == 0 ) {
        return false;
    }
    std::vector< trace::Call * > &calls = outbox[ tp ];
    tp->queuedCalls.insert( tp->queuedCalls.end(), calls.begin(), calls.end() );
    outbox.erase( tp );
    return true;
  }

  void async_reader() {
    for(;;) {
      os::unique_lock<os::mutex> lock(readerMutex);
      if( inbox == NULL ) {
        os::sleep( ASYNC_READER_SLEEP );
      } else {
        ThreadedParser *tp = inbox;
        inbox = NULL;
        assert( outbox.count( tp ) == 0 );
        outbox[ tp ] = std::vector< trace::Call * >();
        std::vector< trace::Call * > &calls = outbox[ tp ];

        while( calls.size() < ASYNC_READER_CALLS ) {
          calls.push_back( tp->parser.parse_call() );
          if( calls.back() == NULL ) {
            calls.pop_back();
            break;
          }
        }
      }
    }
  }

  bool ThreadedParser::open( const char * file ) {
    if( readerThread == NULL ) {
      readerThread = new os::thread( async_reader, NULL );
    }
    if( destroyerThread == NULL ) {
      destroyerThread = new os::thread( async_destroyer, NULL );
    }
    bool ret = parser.open(file);
    if( ret ) {
      enqueue_read( this );
      os::sleep(100000);
      fetch_read( this );
    }
    return ret;
  }
  void ThreadedParser::close() {
    parser.close();
  }
  void ThreadedParser::getBookmark( trace::ParseBookmark & bm ) {
    parser.getBookmark(bm);
    bookmark = bm.next_call_no;
  }
  void ThreadedParser::setBookmark( const trace::ParseBookmark & bm ) {
    parser.setBookmark(bm);
    delete_retired_calls( this );
    queuedCalls.insert( queuedCalls.begin(), retiredCalls.begin(), retiredCalls.end() );
    retiredCalls.clear();
  }
  trace::Call * ThreadedParser::parse_call() {
    if( queuedCalls.size() == ASYNC_READER_CALLS ) {
        enqueue_read( this );
    }
    if( queuedCalls.size() == 2 ) {
        fetch_read( this );
    }
    if( queuedCalls.size() == 0 ) {
        delete_retired_calls( this );
        return NULL;
    }
    trace::Call * call = queuedCalls.front();
    queuedCalls.pop_front();
    if( retiredCalls.size() >= ASYNC_READER_CALLS ) {
      delete_retired_calls( this );
    }
    retiredCalls.push_back( call );
    return call;
  }

  ThreadedParser parser;


  int verbosity = 0;
  bool debug = true;
  bool dumpingState = false;

  Driver driver = DRIVER_DEFAULT;
  const char *driverModule = NULL;

  bool doubleBuffer = true;
  unsigned samples = 1;

  bool profiling = false;
  bool profilingGpuTimes = false;
  bool profilingCpuTimes = false;
  bool profilingPixelsDrawn = false;
  bool profilingMemoryUsage = false;
  bool useCallNos = true;
  bool singleThread = false;

  unsigned frameNo = 0;
  unsigned callNo = 0;


  void
    frameComplete(trace::Call &call) {
      ++frameNo;
    }


  static Dumper defaultDumper;

  Dumper *dumper = &defaultDumper;


  /**
   * Take snapshots.
   */
  static void
    takeSnapshot(unsigned call_no) {
      static unsigned snapshot_no = 0;

      assert(snapshotPrefix);

      image::Image *src = dumper->getSnapshot();
      if (!src) {
        std::cerr << call_no << ": warning: failed to get snapshot\n";
        return;
      }

      if (snapshotPrefix) {
        if (snapshotPrefix[0] == '-' && snapshotPrefix[1] == 0) {
          char comment[21];
          snprintf(comment, sizeof comment, "%u",
              useCallNos ? call_no : snapshot_no);
          switch (snapshotFormat) {
            case PNM_FMT:
              src->writePNM(std::cout, comment);
              break;
            case RAW_RGB:
              src->writeRAW(std::cout);
              break;
            case RAW_MD5:
              src->writeMD5(std::cout);
              break;
            default:
              assert(0);
              break;
          }
        } else {
          os::String filename = os::String::format("%s%010u.png",
              snapshotPrefix,
              useCallNos ? call_no : snapshot_no);
          if (src->writePNG(filename) && play::verbosity >= 0) {
            std::cout << "Wrote " << filename << "\n";
          }
        }
      }

      delete src;

      snapshot_no++;

      return;
    }


  /**
   * Play one call.
   *
   * Take snapshots before/after retracing (as appropriate) and dispatch it to
   * the respective handler.
   */
  static void
    playCall(trace::Call *call) {
      bool swapRenderTarget = call->flags &
        trace::CALL_FLAG_SWAP_RENDERTARGET;
      bool doSnapshot = snapshotFrequency.contains(*call);

      // For calls which cause rendertargets to be swaped, we take the
      // snapshot _before_ swapping the rendertargets.
      if (doSnapshot && swapRenderTarget) {
        if (call->flags & trace::CALL_FLAG_END_FRAME) {
          // For swapbuffers/presents we still use this
          // call number, spite not have been executed yet.
          takeSnapshot(call->no);
        } else {
          // Whereas for ordinate fbo/rendertarget changes we
          // use the previous call's number.
          takeSnapshot(call->no - 1);
        }
      }

      callNo = call->no;
      player.play(*call);

      if (doSnapshot && !swapRenderTarget)
        takeSnapshot(call->no);

      if (call->no >= dumpStateCallNo &&
          dumper->dumpState(std::cout)) {
        exit(0);
      }
    }


  class RelayRunner;


  /**
   * Implement multi-threading by mimicking a relay race.
   */
  class RelayRace
  {
    private:
      /**
       * Runners indexed by the leg they run (i.e, the thread_ids from the
       * trace).
       */
      std::vector<RelayRunner*> runners;

    public:
      RelayRace();

      ~RelayRace();

      RelayRunner *
        getRunner(unsigned leg);

      inline RelayRunner *
        getForeRunner() {
          return getRunner(0);
        }

      void
        run(void);

      void
        passBaton(trace::Call *call);

      void
        finishLine();

      void
        stopRunners();
  };


  /**
   * Each runner is a thread.
   *
   * The fore runner doesn't have its own thread, but instead uses the thread
   * where the race started.
   */
  class RelayRunner
  {
    private:
      friend class RelayRace;

      RelayRace *race;

      unsigned leg;

      os::mutex mutex;
      os::condition_variable wake_cond;

      /**
       * There are protected by the mutex.
       */
      bool finished;
      trace::Call *baton;

      os::thread thread;

      static void *
        runnerThread(RelayRunner *_this);

    public:
      RelayRunner(RelayRace *race, unsigned _leg) :
        race(race),
        leg(_leg),
        finished(false),
        baton(0)
    {
      /* The fore runner does not need a new thread */
      if (leg) {
        thread = os::thread(runnerThread, this);
      }
    }

      ~RelayRunner() {
        if (thread.joinable()) {
          thread.join();
        }
      }

      /**
       * Thread main loop.
       */
      void
        runRace(void) {
          os::unique_lock<os::mutex> lock(mutex);

          while (1) {
            while (!finished && !baton) {
              wake_cond.wait(lock);
            }

            if (finished) {
              break;
            }

            assert(baton);
            trace::Call *call = baton;
            baton = 0;

            runLeg(call);
          }

          if (0) std::cerr << "leg " << leg << " actually finishing\n";

          if (leg == 0) {
            race->stopRunners();
          }
        }

      /**
       * Interpret successive calls.
       */
      void
        runLeg(trace::Call *call) {

          /* Consume successive calls for this thread. */
          do {
            bool callEndsFrame = false;
            static trace::ParseBookmark frameStart;

            assert(call);
            assert(call->thread_id == leg);

            if (loopOnFinish && call->flags & trace::CALL_FLAG_END_FRAME) {
              callEndsFrame = true;
              parser.getBookmark(frameStart);
            }

            playCall(call);
            call = parser.parse_call();

            /* Restart last frame if looping is requested. */
            if (loopOnFinish) {
              if (!call) {
                parser.setBookmark(lastFrameStart);
                call = parser.parse_call();
              } else if (callEndsFrame) {
                lastFrameStart = frameStart;
              }
            }

          } while (call && call->thread_id == leg);

          if (call) {
            /* Pass the baton */
            assert(call->thread_id != leg);
            flushRendering();
            race->passBaton(call);
          } else {
            /* Reached the finish line */
            if (0) std::cerr << "finished on leg " << leg << "\n";
            if (leg) {
              /* Notify the fore runner */
              race->finishLine();
            } else {
              /* We are the fore runner */
              finished = true;
            }
          }
        }

      /**
       * Called by other threads when relinquishing the baton.
       */
      void
        receiveBaton(trace::Call *call) {
          assert (call->thread_id == leg);

          mutex.lock();
          baton = call;
          mutex.unlock();

          wake_cond.signal();
        }

      /**
       * Called by the fore runner when the race is over.
       */
      void
        finishRace() {
          if (0) std::cerr << "notify finish to leg " << leg << "\n";

          mutex.lock();
          finished = true;
          mutex.unlock();

          wake_cond.signal();
        }
  };


  void *
    RelayRunner::runnerThread(RelayRunner *_this) {
      _this->runRace();
      return 0;
    }


  RelayRace::RelayRace() {
    runners.push_back(new RelayRunner(this, 0));
  }


  RelayRace::~RelayRace() {
    assert(runners.size() >= 1);
    std::vector<RelayRunner*>::const_iterator it;
    for (it = runners.begin(); it != runners.end(); ++it) {
      RelayRunner* runner = *it;
      if (runner) {
        delete runner;
      }
    }
  }


  /**
   * Get (or instantiate) a runner for the specified leg.
   */
  RelayRunner *
    RelayRace::getRunner(unsigned leg) {
      RelayRunner *runner;

      if (leg >= runners.size()) {
        runners.resize(leg + 1);
        runner = 0;
      } else {
        runner = runners[leg];
      }
      if (!runner) {
        runner = new RelayRunner(this, leg);
        runners[leg] = runner;
      }
      return runner;
    }


  /**
   * Start the race.
   */
  void
    RelayRace::run(void) {
      trace::Call *call;
      call = parser.parse_call();
      if (!call) {
        /* Nothing to do */
        return;
      }

      /* If the user wants to loop we need to get a bookmark target. We
       * usually get this after replaying a call that ends a frame, but
       * for a trace that has only one frame we need to get it at the
       * beginning. */
      if (loopOnFinish) {
        parser.getBookmark(lastFrameStart);
      }

      RelayRunner *foreRunner = getForeRunner();
      if (call->thread_id == 0) {
        /* We are the forerunner thread, so no need to pass baton */
        foreRunner->baton = call;
      } else {
        passBaton(call);
      }

      /* Start the forerunner thread */
      foreRunner->runRace();
    }


  /**
   * Pass the baton (i.e., the call) to the appropriate thread.
   */
  void
    RelayRace::passBaton(trace::Call *call) {
      if (0) std::cerr << "switching to thread " << call->thread_id << "\n";
      RelayRunner *runner = getRunner(call->thread_id);
      runner->receiveBaton(call);
    }


  /**
   * Called when a runner other than the forerunner reaches the finish line.
   *
   * Only the fore runner can finish the race, so inform him that the race is
   * finished.
   */
  void
    RelayRace::finishLine(void) {
      RelayRunner *foreRunner = getForeRunner();
      foreRunner->finishRace();
    }


  /**
   * Called by the fore runner after finish line to stop all other runners.
   */
  void
    RelayRace::stopRunners(void) {
      std::vector<RelayRunner*>::const_iterator it;
      for (it = runners.begin() + 1; it != runners.end(); ++it) {
        RelayRunner* runner = *it;
        if (runner) {
          runner->finishRace();
        }
      }
    }


  static void
    mainLoop() {
      addCallbacks(player);

      long long startTime = 0; 
      frameNo = 0;

      startTime = os::getTime();

      RelayRace race;
      race.run();
      finishRendering();

      long long endTime = os::getTime();
      float timeInterval = (endTime - startTime) * (1.0 / os::timeFrequency);

      if ((play::verbosity >= -1) || (play::profiling)) {
        std::cout << 
          "Rendered " << frameNo << " frames"
          " in " <<  timeInterval << " secs,"
          " average of " << (frameNo/timeInterval) << " fps\n";
      }

      if (waitOnFinish) {
        waitForInput();
      } else {
        return;
      }
    }


} /* namespace play */


static void
usage(const char *argv0) {
  std::cout << 
    "Usage: " << argv0 << " [OPTION] TRACE [...]\n"
    "Replay TRACE.\n"
    "\n"
    "      --help              print this message\n"
    "      --loop              continuously loop, replaying final frame\n";
}

const static char *
shortOptions = "hl";

const static struct option
longOptions[] = {
  {"help", no_argument, 0, 'h'},
  {"loop", no_argument, 0, 'l'},
  {0, 0, 0, 0}
};


static void exceptionCallback(void)
{
  std::cerr << play::callNo << ": error: caught an unhandled exception\n";
}


  extern "C"
int main(int argc, char **argv)
{
  using namespace play;
  int i;

  int opt;
  while  ((opt = getopt_long_only(argc, argv, shortOptions, longOptions, NULL)) != -1) {
    switch (opt) {
      case 'h':
        usage(argv[0]);
        return 0;
      case 'b':
        play::debug = false;
        play::verbosity = -1;
        break;
      default:
        std::cerr << "error: unknown option " << opt << "\n";
        usage(argv[0]);
        return 1;
    }
  }

#ifndef _WIN32
  if (!isatty(STDOUT_FILENO)) {
    dumpFlags |= trace::DUMP_FLAG_NO_COLOR;
  }
#endif

  play::setUp();

  os::setExceptionCallback(exceptionCallback);

  for (i = optind; i < argc; ++i) {
    if (!play::parser.open(argv[i])) {
      return 1;
    }

    play::mainLoop();

    play::parser.close();
  }

  os::resetExceptionCallback();

  // XXX: X often hangs on XCloseDisplay
  //play::cleanUp();

  return 0;
}

