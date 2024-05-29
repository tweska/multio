#include "multio_c.h"

#include "multio_c_cpp_utils.h"

#include "eckit/exception/Exceptions.h"
#include "eckit/runtime/Main.h"

#include "multio/config/ComponentConfiguration.h"
#include "multio/config/MultioConfiguration.h"
#include "multio/config/PathConfiguration.h"
#include "multio/domain/MaskCompression.h"
#include "multio/message/Metadata.h"
#include "multio/multio_version.h"
#include "multio/server/MultioClient.h"
#include "multio/server/MultioServer.h"
#include "multio/util/FailureHandling.h"

#include <functional>
#include <optional>

using multio::message::Message;
using multio::message::Metadata;
using multio::message::Peer;

using multio::config::ComponentConfiguration;
using multio::config::configuration_file;
using multio::config::configuration_file_name;
using multio::config::configuration_path_name;
using multio::config::MPIInitInfo;
using multio::config::MultioConfiguration;
using multio::util::FailureAwareException;

extern "C" {

struct multio_failure_info_t {
    std::string lastErrorString{""};
};

struct multio_failure_context_t {
    multio_failure_handler_t handler{nullptr};
    void* usercontext{nullptr};
    multio_failure_info_t info;
};

static multio_failure_info_t g_failure_info;

const char* multio_error_string_info(int err, multio_failure_info_t* info) {
#if !defined(MULTIO_DUMMY_API)
    switch (err) {
        case MULTIO_SUCCESS:
            return "Success";
        case MULTIO_ERROR_ECKIT_EXCEPTION:
        case MULTIO_ERROR_GENERAL_EXCEPTION:
        case MULTIO_ERROR_UNKNOWN_EXCEPTION:
            return info->lastErrorString.c_str();
        default:
            return "<unknown>";
    };
#else
    return "DUMMY API ENABLED";
#endif
}

const char* multio_error_string(int err) {
#if !defined(MULTIO_DUMMY_API)
    return multio_error_string_info(err, &g_failure_info);
#else
    return "DUMMY API ENABLED";
#endif
}

struct multio_configuration_t : public MultioConfiguration {
    multio_configuration_t(const eckit::PathName& fileName) : MultioConfiguration{fileName} {}

    multio_configuration_t() : MultioConfiguration{} {}

    std::unique_ptr<multio_failure_context_t> failureContext;
};

struct multio_handle_t : public multio::server::MultioClient {
    using multio::server::MultioClient::MultioClient;
    multio_handle_t() : MultioClient{} {}

    multio_handle_t(MultioConfiguration&& multioConf) : MultioClient{std::move(multioConf)} {}

    std::unique_ptr<multio_failure_context_t> failureContext;
};

struct multio_data_t : public eckit::Buffer {
    multio_handle_t* mio;
};

struct multio_metadata_t : public multio::message::Metadata {
    using multio::message::Metadata::Metadata;
    multio_handle_t* mio;
};

}  // extern "C"


namespace {

// Template magic to provide a consistent error-handling approach

int innerWrapFn(std::function<void()> f) {
    f();
    return MULTIO_SUCCESS;
}

MultioErrorValues errorValue(const FailureAwareException& e) {
    return MULTIO_ERROR_ECKIT_EXCEPTION;
}
MultioErrorValues errorValue(const eckit::Exception& e) {
    return MULTIO_ERROR_ECKIT_EXCEPTION;
}
MultioErrorValues errorValue(const std::exception& e) {
    return MULTIO_ERROR_GENERAL_EXCEPTION;
}
template <class E>
MultioErrorValues errorValue(const E& e) {
    return MULTIO_ERROR_UNKNOWN_EXCEPTION;
}

template <class E>
MultioErrorValues getNestedErrorValue(const E& e) {
    try {
        std::rethrow_if_nested(e);
        // Return passed error value if not nested
        return errorValue(e);
    }
    catch (const FailureAwareException& nestedException) {
        return getNestedErrorValue(nestedException);
    }
    catch (const eckit::Exception& nestedException) {
        return getNestedErrorValue(nestedException);
    }
    catch (const std::exception& nestedException) {
        return getNestedErrorValue(nestedException);
    }
    catch (...) {
        return MULTIO_ERROR_UNKNOWN_EXCEPTION;
    }
}

void callFailureHandler(multio_failure_context_t* fctx, int err) {
    fctx->handler(fctx->usercontext, err, &fctx->info);
}

template <typename FN>
int wrapApiFunction(FN f, multio_failure_context_t* fh = nullptr) {
    try {
        return innerWrapFn(f);
    }
    catch (FailureAwareException& e) {
        std::ostringstream oss;
        oss << "Caught a nested exception on C-C++ API boundary: ";
        oss << e;

        MultioErrorValues error = getNestedErrorValue(e);
        g_failure_info.lastErrorString = oss.str();
        if (fh && fh->handler) {
            fh->info.lastErrorString = oss.str();
            callFailureHandler(fh, error);
            return error;
        }

        // Print to cerr and cout to make sure the user knows his problem
        std::cerr << oss.str() << std::endl;
        std::cout << oss.str() << std::endl;
        return error;
    }
    catch (eckit::Exception& e) {
        int error = MULTIO_ERROR_ECKIT_EXCEPTION;
        std::ostringstream oss;
        oss << "Caught eckit exception on C-C++ API boundary: " << e.what();

        g_failure_info.lastErrorString = oss.str();
        if (fh && fh->handler) {
            fh->info.lastErrorString = oss.str();
            callFailureHandler(fh, error);
            return error;
        }

        // Print to cerr and cout to make sure the user knows his problem
        std::cerr << oss.str() << std::endl;
        std::cout << oss.str() << std::endl;
        return error;
    }
    catch (std::exception& e) {
        int error = MULTIO_ERROR_GENERAL_EXCEPTION;
        std::ostringstream oss;
        oss << "Caught exception on C-C++ API boundary: " << e.what();

        g_failure_info.lastErrorString = oss.str();
        if (fh && fh->handler) {
            fh->info.lastErrorString = oss.str();
            callFailureHandler(fh, error);
            return error;
        }

        // Print to cerr and cout to make sure the user knows his problem
        std::cerr << oss.str() << std::endl;
        std::cout << oss.str() << std::endl;
        return error;
    }
    catch (...) {
        int error = MULTIO_ERROR_UNKNOWN_EXCEPTION;
        std::string errStr = "Caugth unkown exception on C-C++ API boundary";

        g_failure_info.lastErrorString = errStr;
        if (fh && fh->handler) {
            fh->info.lastErrorString = errStr;
            callFailureHandler(fh, error);
            return error;
        }

        // Print to cerr and cout to make sure the user knows his problem
        std::cerr << errStr << std::endl;
        std::cout << errStr << std::endl;
        return error;
    }

    ASSERT(false);
}

template <typename FN>
int wrapApiFunction(FN&& f, multio_configuration_t* cc) {
    return wrapApiFunction(std::forward<FN>(f), cc ? cc->failureContext.get() : nullptr);
}

template <typename FN>
int wrapApiFunction(FN&& f, multio_handle_t* mio) {
    return wrapApiFunction(std::forward<FN>(f), mio ? mio->failureContext.get() : nullptr);
}

template <typename FN>
int wrapApiFunction(FN&& f, multio_metadata_t* md) {
    return wrapApiFunction(std::forward<FN>(f), (md && md->mio) ? md->mio->failureContext.get() : nullptr);
}

template <typename FN>
int wrapApiFunction(FN&& f, multio_data_t* d) {
    return wrapApiFunction(std::forward<FN>(f), (d && d->mio) ? d->mio->failureContext.get() : nullptr);
}

}  // namespace

extern "C" {

int multio_initialise() {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([] {
        static bool initialised = false;

        if (initialised) {
            eckit::Log::warning() << "Initialising MultIO library twice" << std::endl;
        }

        if (!initialised) {
            const char* argv[2] = {"multio-api", 0};
            eckit::Main::initialise(1, const_cast<char**>(argv));
            initialised = true;
        }
    });
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_version(const char** version) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([version]() { (*version) = multio_version_str(); });
#else
    *version = "MULTIO DUMMY API";
    return 0;
#endif
}

int multio_vcs_version(const char** sha1) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([sha1]() { (*sha1) = multio_git_sha1(); });
#else
    *sha1 = "MULTIO DUMMY API";
    return 0;
#endif
}

int multio_config_set_failure_handler(multio_configuration_t* cc, multio_failure_handler_t handler, void* usercontext) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [handler, usercontext, cc] {
            if (!cc->failureContext) {
                cc->failureContext = std::make_unique<multio_failure_context_t>();
            }
            auto f = cc->failureContext.get();
            f->handler = handler;
            f->usercontext = usercontext;
            eckit::Log::info() << "MultIO setting failure handler callable" << std::endl;
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_handle_set_failure_handler(multio_handle_t* mio, multio_failure_handler_t handler, void* usercontext) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [handler, usercontext, mio] {
            if (!mio->failureContext) {
                mio->failureContext = std::make_unique<multio_failure_context_t>();
            }
            auto f = mio->failureContext.get();
            f->handler = handler;
            f->usercontext = usercontext;
            eckit::Log::info() << "MultIO setting failure handler callable" << std::endl;
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_new_configuration(multio_configuration_t** cc) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([cc]() { (*cc) = new multio_configuration_t{}; });
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_new_configuration_from_filename(multio_configuration_t** cc, const char* conf_file_name) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([cc, conf_file_name]() {
        ASSERT(conf_file_name);
        (*cc) = new multio_configuration_t{eckit::PathName{conf_file_name}};
    });
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_delete_configuration(multio_configuration_t* cc) {
#if !defined(MULTIO_DUMMY_API)
    // delete without failurehandler. Configuration is assumed to have be invalid after movement
    return wrapApiFunction([cc]() {
        ASSERT(cc);
        delete cc;
    });
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_config_set_path(multio_configuration_t* cc, const char* configuration_path) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [cc, configuration_path]() {
            ASSERT(cc);
            if (configuration_path != nullptr) {
                cc->setConfigDir(eckit::PathName(configuration_path));
            }
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_mpi_allow_world_default_comm(multio_configuration_t* cc, bool allow) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [cc, allow]() {
            ASSERT(cc);
            if (!cc->getMPIInitInfo()) {
                cc->setMPIInitInfo(std::optional<MPIInitInfo>{MPIInitInfo{}});
            }
            cc->getMPIInitInfo().value().allowWorldAsDefault = allow;
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_mpi_parent_comm(multio_configuration_t* cc, int parent_comm) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [cc, parent_comm]() {
            ASSERT(cc);
            if (!cc->getMPIInitInfo()) {
                cc->setMPIInitInfo(std::optional<MPIInitInfo>{MPIInitInfo{}});
            }
            cc->getMPIInitInfo().value().parentComm = parent_comm;
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_mpi_return_client_comm(multio_configuration_t* cc, int* return_client_comm) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [cc, return_client_comm]() {
            ASSERT(cc);
            if (!cc->getMPIInitInfo()) {
                cc->setMPIInitInfo(std::optional<MPIInitInfo>{MPIInitInfo{}});
            }
            cc->getMPIInitInfo().value().returnClientComm = return_client_comm;
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_mpi_return_server_comm(multio_configuration_t* cc, int* return_server_comm) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [cc, return_server_comm]() {
            ASSERT(cc);
            if (!cc->getMPIInitInfo()) {
                cc->setMPIInitInfo(std::optional<MPIInitInfo>{MPIInitInfo{}});
            }
            cc->getMPIInitInfo().value().returnServerComm = return_server_comm;
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_new_handle(multio_handle_t** mio, multio_configuration_t* cc) {
#if !defined(MULTIO_DUMMY_API)
    // Failurehandler and its string location is preserved through shared_ptr...
    return wrapApiFunction(
        [mio, cc]() {
            ASSERT(cc);
            (*mio) = new multio_handle_t{std::move(*cc)};

            // Finally move failureContext (should not throw...)
            (**mio).failureContext = std::move(cc->failureContext);
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_new_handle_default(multio_handle_t** mio) {
#if !defined(MULTIO_DUMMY_API)
    // Failurehandler and its string location is preserved through shared_ptr...
    return wrapApiFunction([mio]() { (*mio) = new multio_handle_t{}; });
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_delete_handle(multio_handle_t* mio) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([mio]() {
        ASSERT(mio);
        // std::cout << "multio_delete_handle" << std::endl;
        delete mio;
    });
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_start_server(multio_configuration_t* cc) {
#if !defined(MULTIO_DUMMY_API)
    // Failurehandler and its string location is preserved through shared_ptr...
    return wrapApiFunction(
        [cc]() {
            ASSERT(cc);
            multio::server::MultioServer{std::move(*cc)};
        },
        cc);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_open_connections(multio_handle_t* mio) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio]() {
            ASSERT(mio);

            mio->openConnections();
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_close_connections(multio_handle_t* mio) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio]() {
            ASSERT(mio);

            mio->closeConnections();
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_flush(multio_handle_t* mio, multio_metadata_t* md) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md]() {
            ASSERT(mio);
            ASSERT(md);

            mio->dispatch(*md, eckit::Buffer{0}, Message::Tag::Flush);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_notify(multio_handle_t* mio, multio_metadata_t* md) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md]() {
            ASSERT(mio);
            ASSERT(md);

            mio->dispatch(*md, eckit::Buffer{0}, Message::Tag::Notification);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_write_domain(multio_handle_t* mio, multio_metadata_t* md, int* data, int size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md, data, size]() {
            ASSERT(mio);
            ASSERT(md);

            eckit::Buffer domain_def{reinterpret_cast<const char*>(data), size * sizeof(int)};
            mio->dispatch(*md, std::move(domain_def), Message::Tag::Domain);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_write_mask_float(multio_handle_t* mio, multio_metadata_t* md, const float* data, int size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md, data, size]() {
            ASSERT(mio);
            ASSERT(md);

            eckit::Buffer mask_vals = multio::domain::encodeMask(data, size);

            mio->dispatch(*md, std::move(mask_vals), Message::Tag::Mask);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_write_mask_double(multio_handle_t* mio, multio_metadata_t* md, const double* data, int size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md, data, size]() {
            ASSERT(mio);
            ASSERT(md);

            eckit::Buffer mask_vals = multio::domain::encodeMask(data, size);

            mio->dispatch(*md, std::move(mask_vals), Message::Tag::Mask);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_write_field_float(multio_handle_t* mio, multio_metadata_t* md, const float* data, int size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md, data, size]() {
            ASSERT(mio);
            ASSERT(md);

            md->set("precision", "single");

            eckit::Buffer field_vals{reinterpret_cast<const char*>(data), size * sizeof(float)};

            mio->dispatch(*md, std::move(field_vals), Message::Tag::Field);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_write_field_double(multio_handle_t* mio, multio_metadata_t* md, const double* data, int size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md, data, size]() {
            ASSERT(mio);
            ASSERT(md);

            md->set("precision", "double");

            eckit::Buffer field_vals{reinterpret_cast<const char*>(data), size * sizeof(double)};

            mio->dispatch(*md, std::move(field_vals), Message::Tag::Field);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_write_field_buffer(multio_handle_t* mio, multio_metadata_t* md, multio_data_t* d, int byte_size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md, d, byte_size]() {
            ASSERT(mio);
            ASSERT(md);
            ASSERT(d);
            if (byte_size == 4) {
                md->set("precision", "single");
            }
            else if (byte_size == 8) {
                md->set("precision", "double");
            }
            else {
                ASSERT(false);
            }

            eckit::Buffer* tmp = reinterpret_cast<eckit::Buffer*>(d);

            mio->dispatch(*md, std::move(*tmp), Message::Tag::Field);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_write_grib_encoded(multio_handle_t* mio, void* gribdata, int gribsize) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, gribdata, gribsize]() {
            ASSERT(mio);
            ASSERT(gribdata);

            multio::message::Metadata md;
            md.set("format", "grib");

            mio->dispatch(std::move(md), eckit::Buffer{gribdata, gribsize * sizeof(char)}, Message::Tag::Field);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_new_metadata(multio_metadata_t** md, multio_handle_t* mio) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([md]() { (*md) = new multio_metadata_t{}; }, mio);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_copy_metadata(multio_metadata_t** md, multio_metadata_t* mdFrom) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction([md, mdFrom]() { (*md) = new multio_metadata_t{*mdFrom}; }, mdFrom);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_delete_metadata(multio_metadata_t* md) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md]() {
            ASSERT(md);
            delete md;
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int(multio_metadata_t* md, const char* key, int value) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value]() {
            ASSERT(md);
            ASSERT(key);

            md->set(key, value);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_metadata_set_long(multio_metadata_t* md, const char* key, long value) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value]() {
            ASSERT(md);
            ASSERT(key);

            md->set(key, value);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_metadata_set_longlong(multio_metadata_t* md, const char* key, long long value) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value]() {
            ASSERT(md);
            ASSERT(key);

            md->set(key, value);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}




int multio_metadata_set_int8(multio_metadata_t* md, const char* key, int8_t value) {
#if !defined(MULTIO_DUMMY_API)
    return multio_metadata_set_int(md, key, static_cast<int>(value));
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int16(multio_metadata_t* md, const char* key, int16_t value) {
#if !defined(MULTIO_DUMMY_API)
    return multio_metadata_set_int(md, key, static_cast<int>(value));
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int32(multio_metadata_t* md, const char* key, int32_t value) {
#if !defined(MULTIO_DUMMY_API)
    return multio_metadata_set_int(md, key, static_cast<int>(value));
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int64(multio_metadata_t* md, const char* key, int64_t value) {
#if !defined(MULTIO_DUMMY_API)
    return multio_metadata_set_long(md, key, static_cast<long>(value));
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int8_array(multio_metadata_t* md, const char* key, int8_t* value, size_t count) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value, count]() {
            ASSERT(md);
            ASSERT(key);
            std::vector<int> vec(count);
            for ( size_t i=0; i<count; i++ ){
                vec[i] = static_cast<int>(value[i]);
            }
            md->set(key, vec);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int16_array(multio_metadata_t* md, const char* key, int16_t* value, size_t count) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value, count]() {
            ASSERT(md);
            ASSERT(key);
            std::vector<int> vec(count);
            for ( size_t i=0; i<count; i++ ){
                vec[i] = static_cast<int>(value[i]);
            }
            md->set(key, vec);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int32_array(multio_metadata_t* md, const char* key, int32_t* value, size_t count) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value, count]() {
            ASSERT(md);
            ASSERT(key);
            std::vector<int> vec(count);
            for ( size_t i=0; i<count; i++ ){
                vec[i] = static_cast<int>(value[i]);
            }
            md->set(key, vec);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_metadata_set_int64_array(multio_metadata_t* md, const char* key, int64_t* value, size_t count) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value, count]() {
            ASSERT(md);
            ASSERT(key);
            std::vector<long> vec(count);
            for ( size_t i=0; i<count; i++ ){
                vec[i] = static_cast<long>(value[i]);
            }
            md->set(key, vec);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}



int multio_metadata_set_string(multio_metadata_t* md, const char* key, const char* value) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value]() {
            ASSERT(md);
            ASSERT(key);
            ASSERT(value);

            md->set(key, value);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_metadata_set_bool(multio_metadata_t* md, const char* key, bool value) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value]() {
            ASSERT(md);
            ASSERT(key);

            md->set(key, value);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_metadata_set_float(multio_metadata_t* md, const char* key, float value) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value]() {
            ASSERT(md);
            ASSERT(key);

            md->set(key, value);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_metadata_set_double(multio_metadata_t* md, const char* key, double value) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value]() {
            ASSERT(md);
            ASSERT(key);
            md->set(key, value);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_metadata_set_float_array(multio_metadata_t* md, const char* key, float *value, size_t count) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value, count]() {
            ASSERT(md);
            ASSERT(key);
            std::vector<float> vec(count);
            for ( size_t i=0; i<count; i++ ){
                vec[i] = static_cast<float>(value[i]);
            }
            md->set(key, vec);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_metadata_set_double_array(multio_metadata_t* md, const char* key, double *value, size_t count) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [md, key, value, count]() {
            ASSERT(md);
            ASSERT(key);
            std::vector<double> vec(count);
            for ( size_t i=0; i<count; i++ ){
                vec[i] = static_cast<double>(value[i]);
            }
            md->set(key, vec);
        },
        md);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_data_new(multio_data_t** d, multio_handle_t* mio) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [d]() {
            (*d) = new multio_data_t();
            return 0;
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
};

int multio_data_delete(multio_data_t* d) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [d]() {
            ASSERT(d);
            delete d;
            return 0;
        },
        d);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_data_zero(multio_data_t* d) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [d]() {
            d->zero();
            return 0;
        },
        d);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_data_resize(multio_data_t* d, int new_size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [d, new_size]() {
            d->resize(new_size);
            return 0;
        },
        d);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_data_size(multio_data_t* d, int* size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [d, size]() {
            *size = d->size();
            return 0;
        },
        d);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_data_set_float_scalar(multio_data_t* d, float* value, int pos) {
#if !defined(MULTIO_DUMMY_API)
    return multio_data_set_float_chunk(d, value, pos, 1);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_data_set_double_scalar(multio_data_t* d, double* value, int pos) {
#if !defined(MULTIO_DUMMY_API)
    return multio_data_set_double_chunk(d, value, pos, 1);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_data_set_float_chunk(multio_data_t* d, float* value, int pos, int size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [d, value, pos, size]() {
            ASSERT(value);
            ASSERT(pos >= 0);
            ASSERT(pos * sizeof(float) < d->size());
            float* val = static_cast<float*>(d->data());
            for (int i = pos; i < pos + size; ++i) {
                val[i] = value[i - pos];
            }
            return 0;
        },
        d);
#else
    return MULTIO_SUCCESS;
#endif
}

int multio_data_set_double_chunk(multio_data_t* d, double* value, int pos, int size) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [d, value, pos, size]() {
            ASSERT(value);
            ASSERT(pos >= 0);
            ASSERT(pos * sizeof(double) < d->size());
            double* val = static_cast<double*>(d->data());
            for (int i = pos; i < pos + size; ++i) {
                val[i] = value[i - pos];
            }
            return 0;
        },
        d);
#else
    return MULTIO_SUCCESS;
#endif
}


int multio_field_accepted(multio_handle_t* mio, const multio_metadata_t* md, bool* accepted) {
#if !defined(MULTIO_DUMMY_API)
    return wrapApiFunction(
        [mio, md, accepted]() {
            ASSERT(mio);
            ASSERT(md);
            ASSERT(accepted);

            *accepted = mio->isFieldMatched(*md);
        },
        mio);
#else
    return MULTIO_SUCCESS;
#endif
}


}  // extern "C"


// Casting between cpp and c type for testing

Metadata* multio_from_c(multio_metadata_t* md) {
    return static_cast<Metadata*>(md);
}

multio_metadata_t* multio_to_c(Metadata* md) {
    return static_cast<multio_metadata_t*>(md);
}
