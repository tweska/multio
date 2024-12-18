/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#include "AddConst.h"

#include "eckit/config/Configuration.h"
#include "eckit/exception/Exceptions.h"
#include "eckit/log/Log.h"

#include "multio/message/Glossary.h"
#include "multio/util/PrecisionTag.h"

#include "multio/action/scale/MetadataUtils.h"

namespace multio::action {

AddConst::AddConst(const ComponentConfiguration& compConf) :
    ChainedAction(compConf),
    constant_{compConf.parsedConfig().getDouble("constant", 273.15)},
    paramIs_{compConf.parsedConfig().getString("param-is")},
    mapToParam_{compConf.parsedConfig().getString("map-to-param")} {}

void AddConst::executeImpl(message::Message msg) {
    // Skip non-field messages
    if (msg.tag() != message::Message::Tag::Field) {
        executeNext(std::move(msg));
        return;
    }

    // Skip messages with a different paramId
    if (paramIs_ != extractParam(msg.metadata())) {
        executeNext(std::move(msg));
        return;
    }

    // Apply the addition action
    executeNext(util::dispatchPrecisionTag(msg.precision(), [&](auto pt) -> message::Message {
        using Precision = typename decltype(pt)::type;
        return applyAddConst<Precision>(msg);
    }));
}

template <typename Precision>
message::Message AddConst::applyAddConst(message::Message& msg) const {
    const size_t size = msg.payload().size() / sizeof(Precision);
    if (size == 0) {
        throw eckit::SeriousBug{" Payload is empty: AddConst Action: " + msg.metadata().toString(), Here()};
    }

    msg.acquire();

    auto data = static_cast<Precision*>(msg.payload().modifyData());
    if (!data) {
        throw eckit::SeriousBug{" Payload data could not be modified: Scaling Action: " + msg.metadata().toString(), Here()};
    }

    // Apply the addition
    const double c = constant_;
    if (msg.metadata().getOpt<bool>(message::glossary().bitmapPresent).value_or(false)) {
        const double m = msg.metadata().get<double>(message::glossary().missingValue);
        std::transform(data, data + size, data, [c, m](Precision value) -> double {
            return static_cast<Precision>(m == value ? m : value + c);
        });
    } else {
        std::transform(data, data + size, data, [c](Precision value) -> double {
            return static_cast<Precision>(value + c);
        });
    }

    // Apply the mapping
    auto meta = msg.modifyMetadata();
    meta.set(message::glossary().paramId, std::stoll(mapToParam_));
    meta.set(message::glossary().param, mapToParam_.c_str());

    // Construct a new message from the updated metadata and data
    return {
        message::Message::Header{message::Message::Tag::Field, msg.source(), msg.destination(), std::move(meta)},
        eckit::Buffer(reinterpret_cast<const char*>(data), size * sizeof(Precision))
    };
}

void AddConst::print(std::ostream& os) const {
    os << "AddConst(constant=" << constant_ << ", param-is=\"" << paramIs_ << "\", map-to-param=\"" << mapToParam_ << "\")";
}

static ActionBuilder<AddConst> AddConstBuilder("add-const");

}  // namespace multio::action
