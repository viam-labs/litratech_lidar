#include "litratech_camera.hpp"

#include <iostream>
#include <memory>
#include <vector>

#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/log/logging.hpp>
#include <viam/sdk/module/service.hpp>
#include <viam/sdk/registry/registry.hpp>

int main(int argc, char** argv) try {
    viam::sdk::Instance instance;

    VIAM_MODULE_LOG(info) << "Starting LitraTech lidar module";

    const viam::sdk::Model model("viam-labs", "lidar", "litratech");
    auto registration = std::make_shared<viam::sdk::ModelRegistration>(
        viam::sdk::API::get<viam::sdk::Camera>(),
        model,
        [](viam::sdk::Dependencies, viam::sdk::ResourceConfig cfg) {
            return std::make_shared<litratech::LitratechCamera>(std::move(cfg));
        },
        &litratech::LitratechCamera::validate);

    auto module = std::make_shared<viam::sdk::ModuleService>(
        argc, argv, std::vector<std::shared_ptr<viam::sdk::ModelRegistration>>{registration});
    module->serve();
    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    std::cerr << "litratech lidar module failed: " << error.what() << "\n";
    return EXIT_FAILURE;
}
