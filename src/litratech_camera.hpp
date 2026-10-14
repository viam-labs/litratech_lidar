#pragma once

#include <memory>
#include <string>
#include <vector>

#include <viam/sdk/components/camera.hpp>
#include <viam/sdk/config/resource.hpp>

namespace litratech {

class LitratechCamera : public viam::sdk::Camera {
   public:
    explicit LitratechCamera(const viam::sdk::ResourceConfig& cfg);
    ~LitratechCamera() override;

    static std::vector<std::string> validate(const viam::sdk::ResourceConfig& cfg);

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    viam::sdk::ProtoStruct get_status() override;
    image_collection get_images(std::vector<std::string> filter_source_names,
                                const viam::sdk::ProtoStruct& extra) override;
    point_cloud get_point_cloud(std::string mime_type, const viam::sdk::ProtoStruct& extra) override;
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra) override;
    properties get_properties() override;

   private:
    struct State;

    void run();
    viam::sdk::ProtoStruct status_struct() const;

    std::unique_ptr<State> state_;
};

}  // namespace litratech
