/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

/// @author Domokos Sarmany
/// @author Simon Smart
/// @author Tiago Quintino

/// @date Mar 2019

#ifndef multio_sandbox_TcpTransport_H
#define multio_sandbox_TcpTransport_H

#include <iosfwd>

#include "eckit/net/TCPSocket.h"

#include "sandbox/Transport.h"

namespace eckit {
class Configuration;
class TCPSocket;
}  // namespace eckit

namespace multio {
namespace sandbox {

class TcpTransport final : public Transport {
public:
    TcpTransport(const eckit::Configuration& config);

private:
    Message receive() override;

    void send(const Message& message) override;

    void print(std::ostream& os) const override;

    Peer localPeer() const override;

    std::vector<std::unique_ptr<eckit::TCPSocket>> connections_;

};

}  // namespace sandbox
}  // namespace multio

#endif