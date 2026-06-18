QSB = /usr/lib/qt6/bin/qsb
FLAGS = --qt6

SRC_DIR = src
OUT_DIR = compiled

# Shader sources used by the wallpaper / sandbox
SHADERS = \
	advect_forward \
	diffuse \
	divergence \
	flow_lines \
	init_velocity \
	noise \
	passthrough \
	pressure \
	subtract_gradient \
	visualize

# Test shaders (sandbox only)
TEST_SHADERS = \
	binding_verify_test \
	switch_test \
	test_vertex_frag

TARGETS = $(addprefix $(OUT_DIR)/, $(addsuffix .qsb, $(SHADERS)))
TEST_TARGETS = $(addprefix $(OUT_DIR)/, $(addsuffix .qsb, $(TEST_SHADERS)))

.PHONY: all tests clean wallpaper

# Default: compile all production shaders
all: $(TARGETS)

# Also compile test shaders
tests: $(TARGETS) $(TEST_TARGETS)

# Just the shaders needed by shell.qml (wallpaper)
wallpaper: \
	$(OUT_DIR)/advect_forward.qsb \
	$(OUT_DIR)/diffuse.qsb \
	$(OUT_DIR)/divergence.qsb \
	$(OUT_DIR)/flow_lines.qsb \
	$(OUT_DIR)/noise.qsb \
	$(OUT_DIR)/pressure.qsb \
	$(OUT_DIR)/subtract_gradient.qsb

$(OUT_DIR)/%.qsb: $(SRC_DIR)/%.frag
	@echo "  QSB  $<"
	@$(QSB) $(FLAGS) -o $@ $<

clean:
	rm -f $(OUT_DIR)/*.qsb

# Show which shaders are out of date
status:
	@for f in $(SRC_DIR)/*.frag; do \
		base=$$(basename $$f .frag); \
		qsb=$(OUT_DIR)/$$base.qsb; \
		if [ ! -f "$$qsb" ]; then \
			echo "MISSING  $$qsb"; \
		elif [ "$$f" -nt "$$qsb" ]; then \
			echo "OUTDATED $$qsb"; \
		else \
			echo "OK       $$qsb"; \
		fi; \
	done
