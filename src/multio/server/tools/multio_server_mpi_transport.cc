
#include <algorithm>

#include "eckit/mpi/Comm.h"

#include "eckit/config/YAMLConfiguration.h"
#include "eckit/log/Log.h"
#include "eckit/option/CmdArgs.h"
#include "eckit/option/SimpleOption.h"

#include "multio/server/Listener.h"
#include "multio/server/LocalIndices.h"
#include "multio/server/Message.h"
#include "multio/server/MultioServerTool.h"
#include "multio/server/PlanConfigurations.h"
#include "multio/server/Peer.h"
#include "multio/server/print_buffer.h"
#include "multio/server/TestData.h"
#include "multio/server/Transport.h"

using eckit::Log;
using namespace multio::server;

//----------------------------------------------------------------------------------------------------------------------

class MpiExample final : public multio::server::MultioServerTool {
public:  // methods

    MpiExample(int argc, char** argv);

private:
    void usage(const std::string& tool) const override {
        Log::info() << std::endl
                    << "Usage: " << tool << " [options]" << std::endl
                    << std::endl
                    << std::endl
                    << "Examples:" << std::endl
                    << "=========" << std::endl
                    << std::endl
                    << tool << " --nbservers=4" << std::endl
                    << std::endl;
    }

    void init(const eckit::option::CmdArgs& args) override;

    void execute(const eckit::option::CmdArgs& args) override;

    std::vector<Peer> spawnServers(const eckit::Configuration& config,
                                   std::shared_ptr<Transport> transport);

    void spawnClients(std::shared_ptr<Transport> transport, const std::vector<Peer>& serverPeers);

    size_t nbClients_ = 1;

    eckit::YAMLConfiguration config_;
};

//----------------------------------------------------------------------------------------------------------------------

MpiExample::MpiExample(int argc, char** argv) :
    multio::server::MultioServerTool(argc, argv),
    config_(mpi_plan_configurations()) {}

void MpiExample::init(const eckit::option::CmdArgs& args) {
    MultioServerTool::init(args);
    auto domain_size = eckit::mpi::comm(config_.getString("domain").c_str()).size();
    ASSERT(nbServers_ < domain_size);
    nbClients_ = domain_size - nbServers_;
}

//----------------------------------------------------------------------------------------------------------------------

std::vector<Peer> MpiExample::spawnServers(const eckit::Configuration& config,
                                           std::shared_ptr<Transport> transport) {
    std::vector<Peer> serverPeers;

    auto domain = config.getString("domain");

    auto size = eckit::mpi::comm(domain.c_str()).size();
    for (auto ii = size - nbServers_; ii != size; ++ii) {
        serverPeers.push_back(Peer{domain.c_str(), ii});
    }

    auto it = std::find(begin(serverPeers), end(serverPeers), transport->localPeer());
    if (it != end(serverPeers)) {
        Listener listener(config, *transport);

        listener.listen();
    }

    return serverPeers;
}

void MpiExample::spawnClients(std::shared_ptr<Transport> transport,
                              const std::vector<Peer>& serverPeers) {
    // Do nothing if current rank is a server rank
    if (find(begin(serverPeers), end(serverPeers), transport->localPeer()) != end(serverPeers)) {
        return;
    }

    Peer client = transport->localPeer();

    // open all servers
    for (auto& server : serverPeers) {
        Message open{{Message::Tag::Open, client, server}, std::string("open")};
        transport->send(open);
    }

    auto idxm = generate_index_map(client.id_, nbClients_);
    eckit::Buffer buffer(reinterpret_cast<const char*>(idxm.data()), idxm.size() * sizeof(size_t));
    LocalIndices index_map{std::move(idxm)};

    // send partial mapping
    for (auto& server : serverPeers) {

        Message msg{{Message::Tag::Mapping, client, server, "scattered", nbClients_}, buffer};

        transport->send(msg);
    }

    // send N messages
    const int nfields = 13;
    for (int ii = 0; ii < nfields; ++ii) {
        auto field_id = std::string("temperature::step::") + std::to_string(ii);
        std::vector<double> field;

        auto& global_field = global_test_field(field_id, field_size(), "Mpi", client.id_);
        index_map.to_local(global_field, field);

        if (root() == client.id_) {
            eckit::Log::info() << "   ---   Field: " << field_id << ", values: " << std::flush;
            print_buffer(global_field, eckit::Log::info(), " ");
            eckit::Log::info() << std::endl;
        }

        // Choose server
        auto id = std::hash<std::string>{}(field_id) % nbServers_;
        ASSERT(id < serverPeers.size());

        eckit::Buffer buffer(reinterpret_cast<const char*>(field.data()),
                             field.size() * sizeof(double));

        Message msg{{Message::Tag::Field, client, serverPeers[id], "scattered", nbClients_,
                     "prognostic", field_id, field_size()},
                    buffer};

        transport->send(msg);
    }

    // close all servers
    for (auto& server : serverPeers) {
        Message close{{Message::Tag::Close, client, server}, std::string("close")};
        transport->send(close);
    }
}

//----------------------------------------------------------------------------------------------------------------------

void MpiExample::execute(const eckit::option::CmdArgs&) {
    std::cout << "Clients: " << nbClients_ << std::endl << "Servers: " << nbServers_ << std::endl;

    std::shared_ptr<Transport> transport{TransportFactory::instance().build("Mpi", config_)};

    std::cout << *transport << std::endl;

    field_size() = 29;
    new_random_data_each_run() = true;

    auto serverPeers = spawnServers(config_, transport);

    spawnClients(transport, serverPeers);
}

//----------------------------------------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    MpiExample tool(argc, argv);
    return tool.start();
}