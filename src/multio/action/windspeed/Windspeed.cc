/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#include "Windspeed.h"

#include <cmath>
#include <string>

#include "eckit/config/Configuration.h"
#include "eckit/exception/Exceptions.h"

#include "multio/message/Glossary.h"
#include "multio/util/PrecisionTag.h"

#include "multio/action/scale/MetadataUtils.h"

namespace multio::action {

Windspeed::Windspeed(const ComponentConfiguration& compConf):
    ChainedAction(compConf),
    uParamId_{compConf.parsedConfig().getString("u", "131")},
    vParamId_{compConf.parsedConfig().getString("v", "132")},
    wParamId_{compConf.parsedConfig().getString("w", "10")},
    missingValue_{compConf.parsedConfig().getDouble("missing-value", -1)} {}

void Windspeed::executeImpl(message::Message msg) {
    // Clear cache on flush messages
    if (msg.tag() == message::Message::Tag::Flush) {
        messageCache_.clear();
        executeNext(std::move(msg));
        return;
    }

    // Skip non-field messages
    if (msg.tag() != message::Message::Tag::Field) {
        executeNext(std::move(msg));
        return;
    }

    // Skip messages with the wrong paramId
    std::string paramId = extractParam(msg.metadata());
    if (paramId != uParamId_ && paramId != vParamId_) {
        executeNext(std::move(msg));
        return;
    }

    // Grab the identifier
    auto step = msg.metadata().get<std::int64_t>(message::glossary().step);

    // Cache the current message if the other message has not arrived yet
    auto otherMsgSearch = messageCache_.find(genOtherIdent(paramId, step));
    if (otherMsgSearch == messageCache_.end()) {
        messageCache_.insert({genIdent(paramId, step), std::move(msg)});
        return;
    }

    // Extract the old message
    message::Message otherMsg = otherMsgSearch->second;
    messageCache_.erase(otherMsgSearch);  // Pop

    executeNext(util::dispatchPrecisionTag(msg.precision(), [&](auto pt) -> message::Message {
        using Precision = typename decltype(pt)::type;
        // Note: you can swap the u and v message, it doesn't affect the calculation!
        return applyCalc<Precision>(msg, otherMsg);
    }));
}

template <typename Precision>
message::Message Windspeed::applyCalc(message::Message uMsg, message::Message vMsg) const {
    // Check precision of both messages
    if (uMsg.precision() != vMsg.precision()) {
        throw eckit::SeriousBug{" Payloads have different precision: Windspeed Action: " +
                                uMsg.metadata().toString() + ", " + vMsg.metadata().toString(), Here()};
    }

    const size_t uSize = uMsg.payload().size() / sizeof(Precision);
    const size_t vSize = vMsg.payload().size() / sizeof(Precision);
    if (uSize == 0 || uSize != vSize) {
        throw eckit::SeriousBug{" Payload is empty or of different sizes: Windspeed Action: " +
                                uMsg.metadata().toString() + ", " + vMsg.metadata().toString(), Here()};
    }

    uMsg.acquire();
    vMsg.acquire();

    auto uData = static_cast<Precision*>(uMsg.payload().modifyData());
    auto vData = static_cast<Precision*>(vMsg.payload().modifyData());
    if (!uData || !vData) {
        throw eckit::SeriousBug{" Payload data could not be modified: Windspeed Action: " +
                                uMsg.metadata().toString() + ", " + vMsg.metadata().toString(), Here()};
    }

    // Check for missing value data
    const bool uBitmapPresent = uMsg.metadata().getOpt<bool>(message::glossary().bitmapPresent).value_or(false);
    const bool vBitmapPresent = vMsg.metadata().getOpt<bool>(message::glossary().bitmapPresent).value_or(false);
    const double uMissing = uBitmapPresent ? uMsg.metadata().get<double>(message::glossary().missingValue) : -1;
    const double vMissing = vBitmapPresent ? vMsg.metadata().get<double>(message::glossary().missingValue) : -1;

    // Apply the calculation
    if (uBitmapPresent || vBitmapPresent) {
        std::transform(uData, uData + uSize, vData, uData, [uBitmapPresent, vBitmapPresent, uMissing, vMissing, this](Precision uValue, Precision vValue) -> double {
            if (uBitmapPresent && uValue == uMissing) { return missingValue_; }
            if (vBitmapPresent && vValue == vMissing) { return missingValue_; }
            return std::hypot(uValue, vValue);  // sqrt(u^2 + v^2)
        });
    } else {
        std::transform(uData, uData + uSize, vData, uData, [](Precision uValue, Precision vValue) -> double {
            return std::hypot(uValue, vValue);  // sqrt(u^2 + v^2)
        });
    }

    // Apply the mapping
    auto uMeta = uMsg.modifyMetadata();
    uMeta.set(message::glossary().paramId, std::stoll(wParamId_));
    uMeta.set(message::glossary().param, wParamId_.c_str());
    uMeta.erase(message::glossary().name);
    uMeta.erase(message::glossary().shortName);

    // Set the (new) missing value as well, only if there are missing values in the input
    if (uBitmapPresent || vBitmapPresent) {
        uMeta.set(message::glossary().missingValue, missingValue_);
    };

    return {
        message::Message::Header{message::Message::Tag::Field, uMsg.source(), uMsg.destination(), std::move(uMeta)},
        eckit::Buffer(reinterpret_cast<const char*>(uData), uSize * sizeof(Precision))
    };
}

std::string Windspeed::genIdent(std::string paramId, std::int64_t step) const {
   return "ws:" + paramId + ":" + std::to_string(step);
}

std::string Windspeed::genOtherIdent(std::string paramId, std::int64_t step) const {
    return genIdent(paramId == uParamId_ ? vParamId_ : uParamId_, step);
}

void Windspeed::print(std::ostream& os) const {
    os << "Windspeed(u=" << uParamId_ << ", v=\"" << vParamId_ << "\", w=\"" << wParamId_ << "\")";
}

static ActionBuilder<Windspeed> WindspeedBuilder("windspeed");

}  // namespace multio::action
