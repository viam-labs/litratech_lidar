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
	$(CMAKE) -G Ninja -B $(BUILD_DIR) -DLITRATECH_BUILD_MODULE=ON -DCMAKE_BUILD_TYPE=Release .
	jobs=$$(nproc 2>/dev/null || sysctl -n hw.ncpu); \
	if [ "$$(uname)" = Linux ] && [ -r /proc/meminfo ]; then \
	  mem_kb=$$(awk '/MemAvailable/ {print $$2}' /proc/meminfo); \
	  cap=$$((mem_kb / 1800000)); \
	  if [ "$$cap" -lt 1 ]; then cap=1; fi; \
	  if [ "$$jobs" -gt "$$cap" ]; then jobs=$$cap; fi; \
	  echo "building with $$jobs jobs ($$mem_kb kB available)"; \
	fi; \
	$(CMAKE) --build $(BUILD_DIR) --target litratech-lidar --parallel "$$jobs"

module: $(BUILD_DIR)/litratech-lidar

module.tar.gz: $(BUILD_DIR)/litratech-lidar run.sh scripts/bundle_libs.py
	rm -rf $(BUILD_DIR)/pack
	mkdir -p $(BUILD_DIR)/pack
	cp run.sh $(BUILD_DIR)/pack/
	python3 scripts/bundle_libs.py $(BUILD_DIR)/litratech-lidar $(BUILD_DIR)/pack $(BUILD_DIR)
	tar -C $(BUILD_DIR)/pack -czf $@ .

clean:
	rm -rf $(BUILD_DIR) module.tar.gz
