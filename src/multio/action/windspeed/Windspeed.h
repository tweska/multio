/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Kevin Nobel

/// @date Dec 2024

#pragma once

#include <iosfwd>
#include <string>
#include <map>

#include "multio/action/ChainedAction.h"

namespace multio::action {

class Windspeed : public ChainedAction {
public:
    explicit Windspeed(const ComponentConfiguration& compConf);
    void executeImpl(message::Message msg) override;

private:
    template <typename Precision>
    message::Message applyCalc(message::Message uMsg, message::Message vMsg) const;

    std::string genIdent(std::string paramId, std::int64_t step)const;
    std::string genOtherIdent(std::string paramId, std::int64_t step) const;

    void print(std::ostream& os) const override;

    std::string uParamId_;
    std::string vParamId_;
    std::string wParamId_;
    double missingValue_;
    std::map<std::string, message::Message> messageCache_;
};

}  // namespace multio::action