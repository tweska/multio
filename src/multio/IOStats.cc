/*
 * (C) Copyright 1996-2015 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Tiago Quintino
/// @author Simon Smart
/// @date Dec 2015

#include "eckit/io/Length.h"
#include "eckit/log/Bytes.h"
#include "eckit/log/Statistics.h"
#include "eckit/log/Timer.h"
#include "eckit/thread/AutoLock.h"

#include "multio/IOStats.h"

#include <cmath>


using namespace eckit;

namespace multio {

/// @note We do not need to provide locking for access to the IOStats.
///
///       Within the MultIO, all reads/writes/etc. are locked
///         --> locking occurs at a higher level than this!

//----------------------------------------------------------------------------------------------------------------------


IOStats::IOStats() :
    numReads_(0),
    bytesRead_(0),
    sumBytesReadSquared_(0),
    sumReadTimesSquared_(0),
    numWrites_(0),
    bytesWritten_(0),
    sumBytesWrittenSquared_(0),
    sumWriteTimesSquared_(0),
    numiinitfdb_(0),
    numiopenfdb_(0),
    numiclosefdb_(0),
    numiflushfdb_(0),
    numiwritefdb_(0),
    numireadfdb_(0),
    sumTimingSquaresiinitfdb_(0),
    sumTimingSquaresiopenfdb_(0),
    sumTimingSquaresiclosefdb_(0),
    sumTimingSquaresiflushfdb_(0),
    sumTimingSquaresiwritefdb_(0),
    sumTimingSquaresireadfdb_(0) {}


IOStats::~IOStats() {}


void IOStats::logRead(const Length &size, Timer& timer) {

    numReads_++;
    bytesRead_ += size;
    sumBytesReadSquared_ += (size * size);
    readTiming_ += timer;
    sumReadTimesSquared_ += timer.elapsed() * timer.elapsed();
}


void IOStats::logWrite(const Length &size, Timer& timer) {

    numWrites_++;
    bytesWritten_ += size;
    sumBytesWrittenSquared_ += (size * size);
    writeTiming_ += timer;
    sumWriteTimesSquared_ += timer.elapsed() * timer.elapsed();
}


void IOStats::logiinitfdb_(eckit::Timer& timer) {

    numiinitfdb_++;
    timingiinitfdb_ += timer;
    sumTimingSquaresiinitfdb_ += timer.elapsed() * timer.elapsed();
}


void IOStats::logiopenfdb_(eckit::Timer& timer) {

    numiopenfdb_++;
    timingiopenfdb_ += timer;
    sumTimingSquaresiopenfdb_ += timer.elapsed() * timer.elapsed();
}


void IOStats::logiclosefdb_(eckit::Timer& timer) {

    numiclosefdb_++;
    timingiclosefdb_ += timer;
    sumTimingSquaresiclosefdb_ += timer.elapsed() * timer.elapsed();
}


void IOStats::logiflushfdb_(eckit::Timer& timer) {

    numiflushfdb_++;
    timingiflushfdb_ += timer;
    sumTimingSquaresiflushfdb_ += timer.elapsed() * timer.elapsed();
}


void IOStats::logiwritefdb_(eckit::Timer& timer) {

    numiwritefdb_++;
    timingiwritefdb_ += timer;
    sumTimingSquaresiwritefdb_ += timer.elapsed() * timer.elapsed();
}


void IOStats::logireadfdb_(eckit::Timer& timer) {

    numireadfdb_++;
    timingireadfdb_ += timer;
    sumTimingSquaresireadfdb_ += timer.elapsed() * timer.elapsed();
}


static double stdDeviation(double sum, double sumSquares, size_t count) {

    if (count == 0)
        return 0;

    return std::sqrt((count * sumSquares) - (sum* sum)) / count;
}


static double timeStdDeviation(const Timing& sum, double sumSquares, size_t count) {
    return stdDeviation(sum.elapsed_, sumSquares, count);
}


static double average(double sum, size_t count) {

    if (count == 0)
        return 0;
    return sum / count;
}


static double timeAverage(const Timing& sum, size_t count) {
    return average(sum.elapsed_, count);
}


void IOStats::report(std::ostream& s) const {

    // Write statistics

    Statistics::reportCount(s, "Multio num writes", numWrites_, "", true);
    Statistics::reportBytes(s, "Multio bytes written", bytesWritten_, "", true);
    Statistics::reportBytes(s, "Multio average write size", average(bytesWritten_, numWrites_), "", true);
    Statistics::reportBytes(s, "Multio write size std. dev.",
                            stdDeviation(bytesWritten_, sumBytesWrittenSquared_, numWrites_),
                            "", true);
    Statistics::reportTime(s, "Multio write time: ", writeTiming_, "", true);
    Statistics::reportTime(s, "Multio average write time", timeAverage(writeTiming_, numWrites_), "", true);
    Statistics::reportTime(s, "Multio write time std. dev",
                           timeStdDeviation(writeTiming_, sumWriteTimesSquared_, numWrites_)
                           , "", true);

    // Read statistics

    Statistics::reportCount(s, "Multio num reads", numReads_, "", true);
    Statistics::reportBytes(s, "Multio bytes read", bytesRead_, "", true);
    Statistics::reportBytes(s, "Multio average read size", average(bytesRead_, numReads_), "", true);
    Statistics::reportBytes(s, "Multio read size std. dev.",
                            stdDeviation(bytesRead_, sumBytesReadSquared_, numReads_),
                            "", true);
    Statistics::reportTime(s, "Multio read time", timeAverage(readTiming_, numReads_), "", true);
    Statistics::reportTime(s, "Multio read time std. dev",
                           timeStdDeviation(readTiming_, sumReadTimesSquared_, numReads_)
                           , "", true);

    Timing timingCopyRead = readTiming_;
    timingCopyRead /= numReads_;
    Statistics::reportTime(s, "Multio average read time", timingCopyRead);

    // Output for legacy interface statistics

    Statistics::reportCount(s, "Multio num iinitfdb", numiinitfdb_, "", true);
    Statistics::reportTime(s, "Multio time iinitfdb", timingiinitfdb_, "", true);
    Statistics::reportTime(s, "Multio average time iinitfdb", timeAverage(timingiinitfdb_, numiinitfdb_), "", true);
    Statistics::reportTime(s, "Multio time std. dev iinitfdb",
                           timeStdDeviation(timingiinitfdb_, sumTimingSquaresiinitfdb_, numiinitfdb_)
                           , "", true);

    Statistics::reportCount(s, "Multio num iopenfdb", numiopenfdb_, "", true);
    Statistics::reportTime(s, "Multio time iopenfdb", timingiopenfdb_, "", true);
    Statistics::reportTime(s, "Multio average time iopenfdb", timeAverage(timingiopenfdb_, numiopenfdb_), "", true);
    Statistics::reportTime(s, "Multio time std. dev iopenfdb",
                           timeStdDeviation(timingiopenfdb_, sumTimingSquaresiopenfdb_, numiopenfdb_)
                           , "", true);

    Statistics::reportCount(s, "Multio num iclosefdb", numiclosefdb_, "", true);
    Statistics::reportTime(s, "Multio time iclosefdb", timingiclosefdb_, "", true);
    Statistics::reportTime(s, "Multio average time iclosefdb", timeAverage(timingiclosefdb_, numiclosefdb_), "", true);
    Statistics::reportTime(s, "Multio time std. dev iclosefdb",
                           timeStdDeviation(timingiclosefdb_, sumTimingSquaresiclosefdb_, numiclosefdb_)
                           , "", true);

    Statistics::reportCount(s, "Multio num iflushfdb", numiflushfdb_, "", true);
    Statistics::reportTime(s, "Multio time iflushfdb", timingiflushfdb_, "", true);
    Statistics::reportTime(s, "Multio average time iflushfdb", timeAverage(timingiflushfdb_, numiflushfdb_), "", true);
    Statistics::reportTime(s, "Multio time std. dev iflushfdb",
                           timeStdDeviation(timingiflushfdb_, sumTimingSquaresiflushfdb_, numiflushfdb_)
                           , "", true);

    Statistics::reportCount(s, "Multio num iwritefdb", numiwritefdb_, "", true);
    Statistics::reportTime(s, "Multio time iwritefdb", timingiwritefdb_, "", true);
    Statistics::reportTime(s, "Multio average time iwritefdb", timeAverage(timingiwritefdb_, numiwritefdb_), "", true);
    Statistics::reportTime(s, "Multio time std. dev iwritefdb",
                           timeStdDeviation(timingiwritefdb_, sumTimingSquaresiwritefdb_, numiwritefdb_)
                           , "", true);

    Statistics::reportCount(s, "Multio num ireadfdb", numireadfdb_, "", true);
    Statistics::reportTime(s, "Multio time ireadfdb", timingireadfdb_, "", true);
    Statistics::reportTime(s, "Multio average time ireadfdb", timeAverage(timingireadfdb_, numireadfdb_), "", true);
    Statistics::reportTime(s, "Multio time std. dev ireadfdb",
                           timeStdDeviation(timingireadfdb_, sumTimingSquaresireadfdb_, numireadfdb_)
                           , "", true);
}


void IOStats::print(std::ostream &s) const {
    s << "IOStats()";
}


//----------------------------------------------------------------------------------------------------------------------

}  // namespace multio
