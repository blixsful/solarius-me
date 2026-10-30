CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BUILD := build
CORE_SRC := $(wildcard core/*.cpp)
MODULE_SRC := $(wildcard modules/*.cpp)
HAVE_SDL := $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAVE_SDL),1)
MODULE_SRC += $(wildcard modules/display_sdl/*.cpp)
IMGUI_SRC := $(wildcard third_party/imgui/*.cpp) $(wildcard third_party/imgui/backends/imgui_impl_sdl2.cpp) $(wildcard third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp)
MODULE_SRC += $(IMGUI_SRC)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2) -DSOLME_HAVE_SDL=1 -Ithird_party/imgui -Imodules/display_sdl
SDL_LIBS := $(shell pkg-config --libs sdl2)
endif
ARCHES := $(notdir $(wildcard arch/*))
TARGETS := $(addprefix solme-,$(ARCHES))

all: $(TARGETS)

define ARCH_RULES
ARCH_SRC_$(1) := $$(wildcard arch/$(1)/*.cpp)
OBJ_$(1) := $$(patsubst %.cpp,$(BUILD)/$(1)/%.o,$$(CORE_SRC) $$(MODULE_SRC) $$(ARCH_SRC_$(1)))
solme-$(1): $$(OBJ_$(1))
	$$(CXX) -o $$@ $$^ $$(SDL_LIBS)
$(BUILD)/$(1)/%.o: %.cpp
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) $$(SDL_CFLAGS) -DSOLME_ARCH='"$(1)"' -Icore -Imodules -Iarch/$(1) -MMD -MP -c $$< -o $$@
-include $$(OBJ_$(1):.o=.d)
endef

$(foreach a,$(ARCHES),$(eval $(call ARCH_RULES,$(a))))

bios:
	arch/8086/bios/mkrom.sh arch/8086/bios/solbios.S arch/8086/solbios_rom.hpp

clean:
	rm -rf $(BUILD) $(TARGETS)

.PHONY: all clean bios

tests/singlestep: tests/singlestep.cpp core/address_space.cpp arch/8086/cpu8086.cpp
	$(CXX) $(CXXFLAGS) -Icore -Iarch/8086 -o $@ $^

test-cpu: tests/singlestep
	./tests/singlestep -v 0 $(SST_DIR)/*.txt

.PHONY: test-cpu
