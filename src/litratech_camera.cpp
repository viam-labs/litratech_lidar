#include "litratech_camera.hpp"

#include "scan_points.hpp"

#include <arpa/inet.h>

#include <chrono>
#include <cstdint>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <ldcp/device.h>

#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/common/mime_types.hpp>
#include <viam/sdk/log/logging.hpp>

namespace litratech {
namespace {

constexpr int k_default_port = 2105;
constexpr int k_io_timeout_ms = 1000;
constexpr int k_reconnect_failures = 5;

const char* error_string(ldcp_sdk::error_t error) {
    switch (error) {
        case ldcp_sdk::no_error:
            return "no_error";
        case ldcp_sdk::connection_refused:
            return "connection_refused";
        case ldcp_sdk::timed_out:
            return "timed_out";
        case ldcp_sdk::address_in_use:
            return "address_in_use";
        case ldcp_sdk::protocol_error:
            return "protocol_error";
        case ldcp_sdk::not_supported:
            return "not_supported";
        case ldcp_sdk::invalid_params:
            return "invalid_params";
        case ldcp_sdk::device_error:
            return "device_error";
        case ldcp_sdk::unknown:
            return "unknown";
    }
    return "unknown";
}

const viam::sdk::ProtoValue* find_attr(const viam::sdk::ProtoStruct& attrs, const std::string& key) {
    const auto found = attrs.find(key);
    if (found == attrs.end()) {
        return nullptr;
    }
    return &found->second;
}

int require_int(const viam::sdk::ProtoStruct& attrs, const std::string& key, int fallback, bool required) {
    const viam::sdk::ProtoValue* value = find_attr(attrs, key);
    if (value == nullptr) {
        if (required) {
            throw viam::sdk::Exception(key + " is required");
        }
        return fallback;
    }
    const double* number = value->get<double>();
    if (number == nullptr || *number != std::trunc(*number)) {
        throw viam::sdk::Exception(key + " must be an integer");
    }
    return static_cast<int>(*number);
}

double require_number(const viam::sdk::ProtoStruct& attrs, const std::string& key, double fallback) {
    const viam::sdk::ProtoValue* value = find_attr(attrs, key);
    if (value == nullptr) {
        return fallback;
    }
    const double* number = value->get<double>();
    if (number == nullptr) {
        throw viam::sdk::Exception(key + " must be a number");
    }
    return *number;
}

struct Config {
    std::string host;
    int port = k_default_port;
    bool has_scan_frequency = false;
    int scan_frequency = 0;
    bool has_shadow_filter_strength = false;
    int shadow_filter_strength = 0;
    double range_min_m = 0.05;
    double range_max_m = 30.0;
    bool invert_frame = false;

    static Config parse(const viam::sdk::ResourceConfig& cfg) {
        const viam::sdk::ProtoStruct& attrs = cfg.attributes();
        Config parsed;

        const viam::sdk::ProtoValue* host_value = find_attr(attrs, "host");
        if (host_value == nullptr || host_value->get<std::string>() == nullptr ||
            host_value->get<std::string>()->empty()) {
            throw viam::sdk::Exception("host is required and must be a non-empty string");
        }
        parsed.host = *host_value->get<std::string>();

        const auto colon = parsed.host.rfind(':');
        if (colon != std::string::npos && parsed.host.find(':') == colon) {
            try {
                parsed.port = std::stoi(parsed.host.substr(colon + 1));
            } catch (const std::exception&) {
                throw viam::sdk::Exception("host port is not an integer");
            }
            parsed.host = parsed.host.substr(0, colon);
        }
        if (find_attr(attrs, "port") != nullptr) {
            parsed.port = require_int(attrs, "port", k_default_port, true);
        }
        if (parsed.port <= 0 || parsed.port > 65535) {
            throw viam::sdk::Exception("port must be between 1 and 65535");
        }

        if (find_attr(attrs, "scan_frequency") != nullptr) {
            parsed.scan_frequency = require_int(attrs, "scan_frequency", 0, true);
            parsed.has_scan_frequency = true;
            const bool supported = parsed.scan_frequency >= 10 && parsed.scan_frequency <= 30 &&
                                   parsed.scan_frequency % 5 == 0;
            if (!supported) {
                throw viam::sdk::Exception("scan_frequency must be 10, 15, 20, 25, or 30");
            }
        }

        if (find_attr(attrs, "shadow_filter_strength") != nullptr) {
            parsed.shadow_filter_strength = require_int(attrs, "shadow_filter_strength", 0, true);
            parsed.has_shadow_filter_strength = true;
            if (parsed.shadow_filter_strength < 0 || parsed.shadow_filter_strength > 100) {
                throw viam::sdk::Exception("shadow_filter_strength must be between 0 and 100");
            }
        }

        parsed.range_min_m = require_number(attrs, "range_min_m", parsed.range_min_m);
        parsed.range_max_m = require_number(attrs, "range_max_m", parsed.range_max_m);
        if (!(parsed.range_min_m >= 0 && parsed.range_min_m < parsed.range_max_m)) {
            throw viam::sdk::Exception("range_min_m must be >= 0 and less than range_max_m");
        }

        if (const viam::sdk::ProtoValue* invert = find_attr(attrs, "invert_frame")) {
            const bool* flag = invert->get<bool>();
            if (flag == nullptr) {
                throw viam::sdk::Exception("invert_frame must be a boolean");
            }
            parsed.invert_frame = *flag;
        }

        if (::inet_addr(parsed.host.c_str()) == INADDR_NONE) {
            throw viam::sdk::Exception("host is not an IPv4 address");
        }
        return parsed;
    }
};

}  // namespace

struct LitratechCamera::State {
    explicit State(Config config) : config(std::move(config)) {}

    Config config;
    std::mutex device_mu;
    std::unique_ptr<ldcp_sdk::Device> device;

    std::mutex cloud_mu;
    std::vector<unsigned char> pcd;
    size_t point_count = 0;
    bool has_cloud = false;

    std::mutex info_mu;
    bool connected = false;
    std::string model;
    std::string serial;
    std::string firmware;
    std::string hardware;
    std::string last_error;
    float scan_hz = 0;

    std::mutex stop_mu;
    std::condition_variable stop_cv;
    bool stop = false;
    std::thread worker;
};

std::vector<std::string> LitratechCamera::validate(const viam::sdk::ResourceConfig& cfg) {
    Config::parse(cfg);
    return {};
}

LitratechCamera::LitratechCamera(const viam::sdk::ResourceConfig& cfg)
    : Camera(cfg.name()), state_(std::make_unique<State>(Config::parse(cfg))) {
    state_->worker = std::thread([this] { run(); });
}

LitratechCamera::~LitratechCamera() {
    {
        std::lock_guard<std::mutex> lock(state_->stop_mu);
        state_->stop = true;
    }
    state_->stop_cv.notify_all();
    if (state_->worker.joinable()) {
        state_->worker.join();
    }
}

void LitratechCamera::run() {
    auto stop_requested = [this] {
        std::lock_guard<std::mutex> lock(state_->stop_mu);
        return state_->stop;
    };
    auto wait_backoff = [this] {
        std::unique_lock<std::mutex> lock(state_->stop_mu);
        state_->stop_cv.wait_for(lock, std::chrono::seconds(2), [this] { return state_->stop; });
    };
    auto note_error = [this](const std::string& message) {
        VIAM_RESOURCE_LOG(warn) << message;
        std::lock_guard<std::mutex> lock(state_->info_mu);
        state_->last_error = message;
        state_->connected = false;
    };

    while (!stop_requested()) {
        ldcp_sdk::NetworkLocation location(::inet_addr(state_->config.host.c_str()),
                                           htons(static_cast<uint16_t>(state_->config.port)));
        bool opened_ok = false;
        {
            std::lock_guard<std::mutex> lock(state_->device_mu);
            state_->device = std::make_unique<ldcp_sdk::Device>(location);
            state_->device->setTimeout(k_io_timeout_ms);
            const ldcp_sdk::error_t opened = state_->device->open();
            opened_ok = opened == ldcp_sdk::no_error;
            if (!opened_ok) {
                state_->device.reset();
                note_error(std::string("waiting for ") + state_->config.host + ": " + error_string(opened));
            }
        }
        if (!opened_ok) {
            wait_backoff();
            continue;
        }

        VIAM_RESOURCE_LOG(info) << "Connected to " << state_->config.host << ":" << state_->config.port;

        auto query_string = [this](auto&& query, std::string& dest) {
            std::string value;
            if (query(value) == ldcp_sdk::no_error) {
                dest = std::move(value);
            }
        };
        {
            std::lock_guard<std::mutex> device_lock(state_->device_mu);
            std::lock_guard<std::mutex> info_lock(state_->info_mu);
            query_string([&](std::string& out) { return state_->device->queryModel(out); }, state_->model);
            query_string([&](std::string& out) { return state_->device->querySerial(out); }, state_->serial);
            query_string([&](std::string& out) { return state_->device->queryFirmwareVersion(out); },
                         state_->firmware);
            query_string([&](std::string& out) { return state_->device->queryHardwareVersion(out); },
                         state_->hardware);
            int frequency = 0;
            if (state_->config.has_scan_frequency) {
                const ldcp_sdk::error_t set =
                    state_->device->setScanFrequency(state_->config.scan_frequency);
                if (set != ldcp_sdk::no_error) {
                    VIAM_RESOURCE_LOG(warn)
                        << "Unable to set scan frequency: " << error_string(set);
                }
            }
            if (state_->device->getScanFrequency(frequency) == ldcp_sdk::no_error) {
                state_->scan_hz = static_cast<float>(frequency);
            }
            if (state_->config.has_shadow_filter_strength) {
                const ldcp_sdk::error_t set =
                    state_->device->setShadowFilterStrength(state_->config.shadow_filter_strength);
                if (set != ldcp_sdk::no_error) {
                    VIAM_RESOURCE_LOG(warn)
                        << "Unable to set shadow filter strength: " << error_string(set);
                }
            }
            const bool streaming = state_->device->startMeasurement() == ldcp_sdk::no_error &&
                                   state_->device->startStreaming() == ldcp_sdk::no_error;
            if (!streaming) {
                state_->last_error = "device did not start streaming";
                state_->connected = false;
                state_->device->close();
                state_->device.reset();
            } else {
                state_->connected = true;
                state_->last_error.clear();
            }
        }
        if (!state_->device) {
            VIAM_RESOURCE_LOG(warn) << "device did not start streaming";
            wait_backoff();
            continue;
        }

        int failures = 0;
        while (!stop_requested()) {
            std::vector<ldcp_sdk::ScanBlock> frame;
            ldcp_sdk::ScanBlock block;
            ldcp_sdk::error_t result = ldcp_sdk::no_error;
            {
                std::lock_guard<std::mutex> lock(state_->device_mu);
                do {
                    result = state_->device->readScanBlock(block);
                } while (!stop_requested() && result == ldcp_sdk::no_error && block.block_index != 0);
            }
            if (stop_requested()) {
                break;
            }
            if (result != ldcp_sdk::no_error) {
                ++failures;
            } else {
                frame.push_back(block);
                while (!stop_requested() && block.block_index < block.block_count - 1) {
                    std::lock_guard<std::mutex> lock(state_->device_mu);
                    result = state_->device->readScanBlock(block);
                    if (result != ldcp_sdk::no_error || block.block_index == 0) {
                        break;
                    }
                    frame.push_back(block);
                }
                if (result == ldcp_sdk::no_error && !frame.empty() &&
                    frame.back().block_index == frame.back().block_count - 1) {
                    failures = 0;
                    const std::vector<Point> points = points_from_blocks(
                        frame, state_->config.range_min_m, state_->config.range_max_m,
                        state_->config.invert_frame);
                    std::lock_guard<std::mutex> lock(state_->cloud_mu);
                    state_->pcd = encode_pcd(points);
                    state_->point_count = points.size();
                    state_->has_cloud = true;
                    continue;
                }
                ++failures;
            }

            if (failures >= k_reconnect_failures) {
                note_error(std::string("scan read failed: ") + error_string(result));
                break;
            }
        }

        std::lock_guard<std::mutex> lock(state_->device_mu);
        if (state_->device) {
            state_->device->stopStreaming();
            state_->device->stopMeasurement();
            state_->device->close();
            state_->device.reset();
        }
        {
            std::lock_guard<std::mutex> info_lock(state_->info_mu);
            state_->connected = false;
        }
    }
}

viam::sdk::ProtoStruct LitratechCamera::status_struct() const {
    std::lock_guard<std::mutex> info_lock(state_->info_mu);
    std::lock_guard<std::mutex> cloud_lock(state_->cloud_mu);
    return {
        {"connected", state_->connected},
        {"host", state_->config.host},
        {"port", static_cast<double>(state_->config.port)},
        {"model", state_->model},
        {"serial", state_->serial},
        {"firmware", state_->firmware},
        {"hardware", state_->hardware},
        {"scan_frequency_hz", static_cast<double>(state_->scan_hz)},
        {"point_count", static_cast<double>(state_->point_count)},
        {"last_error", state_->last_error},
    };
}

viam::sdk::ProtoStruct LitratechCamera::do_command(const viam::sdk::ProtoStruct&) {
    return status_struct();
}

viam::sdk::ProtoStruct LitratechCamera::get_status() {
    return status_struct();
}

LitratechCamera::image_collection LitratechCamera::get_images(std::vector<std::string>,
                                                             const viam::sdk::ProtoStruct&) {
    throw viam::sdk::Exception(viam::sdk::ErrorCondition::k_not_supported,
                               "LitraTech lidar does not produce images; use GetPointCloud");
}

LitratechCamera::point_cloud LitratechCamera::get_point_cloud(std::string,
                                                             const viam::sdk::ProtoStruct&) {
    std::lock_guard<std::mutex> lock(state_->cloud_mu);
    if (!state_->has_cloud) {
        throw viam::sdk::Exception("no scan received from the lidar yet");
    }
    return {viam::sdk::kMimeTypePCD, state_->pcd};
}

std::vector<viam::sdk::GeometryConfig> LitratechCamera::get_geometries(const viam::sdk::ProtoStruct&) {
    return {};
}

LitratechCamera::properties LitratechCamera::get_properties() {
    properties props;
    props.supports_pcd = true;
    props.mime_types = {viam::sdk::kMimeTypePCD};
    std::lock_guard<std::mutex> lock(state_->info_mu);
    props.frame_rate = state_->scan_hz;
    return props;
}

}  // namespace litratech
