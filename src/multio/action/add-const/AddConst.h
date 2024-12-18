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

#include "multio/action/ChainedAction.h"

namespace multio::action {

class AddConst : public ChainedAction {
public:
    explicit AddConst(const ComponentConfiguration& compConf);
    void executeImpl(message::Message msg) override;

private:
    template <typename Precision>
    message::Message applyAddConst(message::Message& msg) const;

    void print(std::ostream& os) const override;

    double constant_;
    std::string paramIs_;
    std::string mapToParam_;
};

}  // namespace multio::action