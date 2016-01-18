/*
 * (C) Copyright 1996-2015 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Simon Smart
/// @date Dec 2015

#include <sys/time.h>

#include "eckit/io/Buffer.h"
#include "eckit/io/DataHandle.h"

#include "multio/Journal.h"
#include "multio/JournalRecord.h"
#include "multio/SharableBuffer.h"

using namespace eckit;

namespace multio {

// -------------------------------------------------------------------------------------------------

const FixedString<4> JournalRecord::TerminationMarker("END!");
const unsigned char JournalRecord::CurrentTagVersion = 1;



// -------------------------------------------------------------------------------------------------

/*
 * TODO: SDS. For more complicated cases, it may well be woth changing utilised_ into
 *       writeOnDestruct_, and then having the JournalRecord write itself.
 *
 * --> This will require passing in the Journal object
 * --> Will require some careful consideration of locking for the journal.
 */

JournalRecord::JournalRecord(Journal &journal, RecordType type) :
    journal_(journal),
    utilised_(false) {

    if (type != JournalRecord::Uninitialised) {

        initialise(type);
    }
}


JournalRecord::~JournalRecord() {
}


/// Initialise a (new) Journal record, such that it will be valid for writing
/// (once payload data has been added as appropriate).
void JournalRecord::initialise(RecordType type) {

    eckit::zero(head_);
    head_.tag_ = type;
    head_.tagVersion_ = JournalRecord::CurrentTagVersion;
    head_.numEntries_ = 0;

    SYSCALL(::gettimeofday(&head_.timestamp_, NULL));

    marker_ = JournalRecord::TerminationMarker;
}

void JournalRecord::addWriteEntry(const DataBlobPtr& blob, int sinkId)
{
    // Ensure that the JournalEntry has a copy of the data. Note that this may
    // already have been done by another DataSink (in which case this is a NOP).
    addData(blob);

    // Add the entry here. By default there is no additional (DataSink-specific)
    // information, so the payload length is zero
    addJournalEntry(JournalRecord::JournalEntry::Write, sinkId);
}


/// Write the journal record, consisting of three parts:
/// i)   The header
/// ii)  The JournalEntries
/// iii) The end-of-record marker
void JournalRecord::writeRecord(DataHandle& handle) {

    Log::info() << "[" << *this << "] Writing record" << std::endl;

    handle.write(&head_, sizeof(head_));

    ASSERT(size_t(head_.numEntries_) == entries_.size());

    for (std::list<JournalEntry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
        handle.write(&it->head_, sizeof(it->head_));

        // If there is data associated with the journal entry then it should be appended below
        // the header information.
        if (it->data_) {
            size_t length = it->data_->length();
            ASSERT(it->head_.payload_length_ == length);
            const void* data = it->data_->buffer();
            handle.write(data, length);
        } else {
            ASSERT(it->head_.payload_length_ == 0);
        }
    }
    
    handle.write(&marker_, sizeof(marker_));
}


void JournalRecord::addData(const DataBlobPtr& blob) {

    // n.b. The data must be the first thing added to the Journal Record
    if (entries_.empty()) {

        Log::info() << "[" << *this << "] Adding data element" << std::endl;

        entries_.push_back(JournalEntry());

        JournalEntry& entry = entries_.back();

        eckit::zero(entry.head_);
        entry.head_.tag_ = JournalEntry::Data;
        entry.head_.payload_length_ = blob->length();
        SYSCALL(::gettimeofday(&entry.head_.timestamp_, NULL));

        entry.data_.reset( blob );

        // Now that something has been added, we should certainly write this entry on exit!
        utilised_ = true;
        head_.numEntries_++;

    } else {
        // We don't need to duplicate the data if multiple DataSinks are reporting to the journal
        ASSERT(utilised_);
        ASSERT(entries_.front().head_.tag_ == JournalEntry::Data);
        ASSERT(entries_.front().head_.payload_length_ == uint64_t(blob->length()));
    }
}

void JournalRecord::addJournalEntry(JournalRecord::JournalEntry::EntryType type, int sinkId) {

    Log::info() << "[" << *this << "] Adding journal entry ("
                << EntryTypeName(type) << ")"<< std::endl;

    // Before we add a journal entry, the data MUST have been added already
    ASSERT(utilised_);
    ASSERT(entries_.front().head_.tag_ == JournalEntry::Data);

    // These are (currently) default journal entries with no attached data.
    // This needs to be extended.
    entries_.push_back(JournalEntry());
    JournalEntry& entry = entries_.back();

    eckit::zero(entry.head_);
    entry.head_.tag_ = type;
    entry.head_.payload_length_ = 0;
    SYSCALL(::gettimeofday(&entry.head_.timestamp_, NULL));

    if (sinkId != -1) {
        entry.head_.id_ = unsigned(sinkId);
        ASSERT(entry.head_.id_ == sinkId);
    }

    // Add an entry!
    head_.numEntries_++;
}


const std::string& JournalRecord::RecordTypeName(RecordType type) {

    const static std::string names[] = {
        "Uninitialised",
        "End of Journal",
        "Journal entry",
        "Configuration"
    };

    ASSERT(type >= 0 && type <= Configuration);

    return names[type];
}


const char * JournalRecord::EntryTypeName(JournalEntry::EntryType type) {

    const char * name;

    switch(type) {
    case JournalEntry::Data:
        name = "Data";
        break;

    case JournalEntry::Write:
        name = "Write";
        break;

    case JournalEntry::End:
        name = "End";
        break;

    default:
        name = "** Unknown **";
        break;
    };

    return name;
}

void JournalRecord::print(std::ostream& os) const
{
    os << "JournalRecord(" << RecordTypeName(RecordType(head_.tag_)) << ")";
}

// -------------------------------------------------------------------------------------------------

} // namespace multio
