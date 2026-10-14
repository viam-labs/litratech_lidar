BUILD_DIR ?= build
CMAKE ?= cmake

.PHONY: setup test module module.tar.gz clean

setup:
	./setup.sh

$(BUILD_DIR)/scan_points_test:
	$(CMAKE) -G Ninja -B $(BUILD_DIR) -DLITRATECH_BUILD_MODULE=OFF .
	$(CMAKE) --build $(BUILD_DIR) --target scan_points_test

test: $(BUILD_DIR)/scan_points_test
	$(BUILD_DIR)/scan_points_test

$(BUILD_DIR)/litratech-lidar:
	$(CMAKE) -G Ninja -B $(BUILD_DIR) -DLITRATECH_BUILD_MODULE=ON .
	$(CMAKE) --build $(BUILD_DIR) --target litratech-lidar

module: $(BUILD_DIR)/litratech-lidar

module.tar.gz: $(BUILD_DIR)/litratech-lidar run.sh scripts/bundle_libs.py
	rm -rf $(BUILD_DIR)/pack
	mkdir -p $(BUILD_DIR)/pack
	cp run.sh $(BUILD_DIR)/pack/
	python3 scripts/bundle_libs.py $(BUILD_DIR)/litratech-lidar $(BUILD_DIR)/pack $(BUILD_DIR)
	tar -C $(BUILD_DIR)/pack -czf $@ .

clean:
	rm -rf $(BUILD_DIR) module.tar.gz
