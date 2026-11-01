# Cross-compiler target triple: build Windows x86-64 binaries from macOS.
TARGET := x86_64-w64-mingw32
CXX := $(TARGET)-g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O0 -g -Iinclude -Isrc/backends/common -Isrc/backends/native -Isrc/backends/neovim

# Select the native build directory suffix. Backend sources are selected by
# the native and nvim targets, independently of this variable.
BACKENDS ?= both
ifeq ($(filter $(BACKENDS),both native nvim),)
$(error BACKENDS must be both, native, or nvim)
endif
ifneq ($(words $(BACKENDS)),1)
$(error BACKENDS must be both, native, or nvim)
endif
NATIVE_SOURCES := $(addprefix src/backends/native/,native_backend.cpp vim_engine.cpp vim_commands.cpp vim_motions.cpp vim_actions.cpp vim_operators.cpp)
NEOVIM_SOURCES := src/backends/neovim/neovim_backend.cpp
CXXFLAGS_NATIVE += -DENABLE_NATIVE_BACKEND
CORE_SOURCES := src/backends/common/backend_manager.cpp src/backends/common/backend_common.cpp src/backends/common/editor_text.cpp src/backends/common/scintilla_editor.cpp
CORE_SOURCES_NATIVE := $(CORE_SOURCES) $(NATIVE_SOURCES)
CORE_SOURCES_NEOVIM := $(CORE_SOURCES) $(NEOVIM_SOURCES)
CORE_HEADERS := $(wildcard include/*.h src/backends/common/*.h src/backends/native/*.h src/backends/neovim/*.h)
# -mwindows selects a GUI entry point and -municode selects wWinMain.
LDFLAGS := -mwindows -municode
# Link MinGW's C/C++ support code into VimMotions.dll so deployment does not
# require copying compiler-specific libstdc++ / libwinpthread DLLs beside it.
RUNTIME_LDFLAGS := -static -static-libgcc -static-libstdc++

BUILD_DIR := build/windows-x64/$(BACKENDS)
APP := $(BUILD_DIR)/ScintillaHost.exe
NVIM_BUILD_DIR := build/windows-x64/neovim-build
NVIM_APP := $(NVIM_BUILD_DIR)/ScintillaHost.exe
NVIM_DLL := $(NVIM_BUILD_DIR)/VimMotions.dll
NVIM_PLUGIN_DLL := $(NVIM_BUILD_DIR)/VIM-Motions-for-NotepadPP.dll
NEOVIM_DIR ?= dependencies/neovim
SCINTILLA_DIR ?= dependencies/scintilla
SCINTILLA_DLL := $(BUILD_DIR)/Scintilla.dll
VIM_MOTIONS_DLL := $(BUILD_DIR)/VimMotions.dll
NOTEPADPP_PLUGIN_DLL := $(BUILD_DIR)/VIM-Motions-for-NotepadPP.dll
NOTEPADPP_EXE ?= /Users/filipzevnik/.wine/drive_c/Program Files/Notepad++/notepad++.exe
NOTEPADPP_PLUGIN_DIR ?= /Users/filipzevnik/.wine/drive_c/Program Files/Notepad++/plugins/VIM-Motions-for-NotepadPP
TEST_BUILD_DIR ?= $(BUILD_DIR)
TEST_APP := $(TEST_BUILD_DIR)/VimMotionsTests.exe
TEST_CASES_DIR := tests/cases
TEST_CASES_FORMATTER := $(TEST_CASES_DIR)/format_test_cases.py
TEST_CASES := $(shell find $(TEST_CASES_DIR) -type f -name '*.json' -print)
TEST_COMMON_SOURCES := tests/common/src/json_test_cases.cpp tests/common/src/test_runner.cpp
TEST_COMMON_HEADERS := tests/common/include/TestCase.h tests/common/include/json_test_cases.h tests/common/include/test_runner.h
TEST_FUNCTIONAL_SOURCES := tests/functional/src/main.cpp
TEST_FUNCTIONAL_HEADERS :=
REPLAY_CASES ?= tests_fuzzing/tmp/cases/generated.json
# Preserve literal dollar signs in CASE and quote the ID for the shell.
REPLAY_CASE_ARG = '$(subst ','"'"',$(value CASE))'
TEST_SOURCES := $(TEST_FUNCTIONAL_SOURCES) $(TEST_COMMON_SOURCES)
TEST_HEADERS := $(TEST_FUNCTIONAL_HEADERS) $(TEST_COMMON_HEADERS)
HOST_SOURCES := src/main.cpp tests/common/src/json_test_cases.cpp
HOST_HEADERS := tests/common/include/json_test_cases.h tests/common/include/TestCase.h src/backends/common/VimMotions.h src/backends/common/ScintillaMessages.h
SCINTILLA_LICENSE := $(BUILD_DIR)/Scintilla-License.txt
PACKAGE_NAME := ScintillaVimHost-windows-x64
PACKAGE_DIR := dist/$(PACKAGE_NAME)
PACKAGE_ZIP := dist/$(PACKAGE_NAME).zip

.PHONY: all native installnative nvim stagenvim installnvim
.PHONY: runnotepad runscintilla runscintillanative runscintillanvim runnotepadnative runnotepadnvim
.PHONY: testnative testnvim replay replaynative replaynvim run-replay format-test-cases test test-build run-tests
.PHONY: fuzz fuzz-seed fuzz-build fuzznative fuzznvim fuzz-seed-native fuzz-seed-nvim run-fuzz run-fuzz-seed package clean

# Global
all: nvim native

# Launch an existing Notepad++ installation without building or installing.
runnotepad:
	wine "$(NOTEPADPP_EXE)"

runscintilla:
	wine "$(RUN_SCINTILLA_APP)"

RUN_SCINTILLA_APP ?= $(APP)

# Native engine

native: $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL) $(NOTEPADPP_PLUGIN_DLL) $(SCINTILLA_LICENSE)

$(NOTEPADPP_PLUGIN_DLL): src/notepadpp_plugin.cpp $(CORE_SOURCES_NATIVE) $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_NATIVE) -DVIM_MOTIONS_BUILD -shared src/notepadpp_plugin.cpp $(CORE_SOURCES_NATIVE) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

$(BUILD_DIR) $(NVIM_BUILD_DIR):
	mkdir -p $@

$(VIM_MOTIONS_DLL): $(CORE_SOURCES_NATIVE) $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_NATIVE) -DVIM_MOTIONS_BUILD -shared $(CORE_SOURCES_NATIVE) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

# Build and install the native backend.
installnative: native
	mkdir -p "$(NOTEPADPP_PLUGIN_DIR)"
	cp "$(NOTEPADPP_PLUGIN_DLL)" "$(NOTEPADPP_PLUGIN_DIR)/"

runscintillanative: native $(APP)
	$(MAKE) runscintilla RUN_SCINTILLA_APP="$(APP)"

runnotepadnative: installnative
	$(MAKE) runnotepad

testnative: $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)
	$(MAKE) run-tests TEST_BUILD_DIR="$(BUILD_DIR)"

replaynative: $(APP) $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)
	$(MAKE) run-replay REPLAY_APP="$(APP)"

fuzznative: $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)
	$(MAKE) run-fuzz TEST_BUILD_DIR="$(BUILD_DIR)"

fuzz-seed-native: $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)
	$(MAKE) run-fuzz-seed TEST_BUILD_DIR="$(BUILD_DIR)"

# NVIM backend

nvim: $(NVIM_APP) $(NVIM_DLL) $(NVIM_PLUGIN_DLL) $(NVIM_BUILD_DIR)/Scintilla.dll $(NVIM_BUILD_DIR)/Scintilla-License.txt stagenvim

$(NVIM_DLL): $(CORE_SOURCES_NEOVIM) $(CORE_HEADERS) | $(NVIM_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DVIM_MOTIONS_BUILD -shared $(CORE_SOURCES_NEOVIM) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

$(NVIM_PLUGIN_DLL): src/notepadpp_plugin.cpp $(CORE_SOURCES_NEOVIM) $(CORE_HEADERS) | $(NVIM_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DVIM_MOTIONS_BUILD -shared src/notepadpp_plugin.cpp $(CORE_SOURCES_NEOVIM) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

# Neovim backend: build and install.
# Targeting notepad
installnvim: nvim
	mkdir -p "$(NOTEPADPP_PLUGIN_DIR)"
	cp "$(NVIM_PLUGIN_DLL)" "$(NOTEPADPP_PLUGIN_DIR)/"
	mkdir -p "$(NOTEPADPP_PLUGIN_DIR)/neovim"
	cp -R "$(NVIM_BUILD_DIR)/neovim/." "$(NOTEPADPP_PLUGIN_DIR)/neovim/"

# Targeting standalone Scintilla
stagenvim: | $(NVIM_BUILD_DIR)
	@test -f "$(NEOVIM_DIR)/bin/nvim.exe" || (echo "Missing $(NEOVIM_DIR)/bin/nvim.exe." >&2; exit 1)
	mkdir -p "$(NVIM_BUILD_DIR)/neovim"
	cp -R "$(NEOVIM_DIR)/." "$(NVIM_BUILD_DIR)/neovim/"

# Shared host and Scintilla dependencies.
$(APP): HOST_FLAGS := $(CXXFLAGS_NATIVE)
$(APP): | $(BUILD_DIR)
$(NVIM_APP): | $(NVIM_BUILD_DIR)
$(APP) $(NVIM_APP): $(HOST_SOURCES) $(HOST_HEADERS)
	$(CXX) $(CXXFLAGS) $(HOST_FLAGS) -Itests/common/include $(HOST_SOURCES) -o $@ $(LDFLAGS) -lshell32 $(RUNTIME_LDFLAGS)

# Keep the supplied Windows x64 DLL outside the generated build directory.
$(SCINTILLA_DIR)/Scintilla.dll $(SCINTILLA_DIR)/License.txt:
	@echo "Missing $@. Place the Scintilla distribution file there." >&2
	@exit 1

$(SCINTILLA_DLL) $(SCINTILLA_LICENSE): | $(BUILD_DIR)
$(NVIM_BUILD_DIR)/Scintilla.dll $(NVIM_BUILD_DIR)/Scintilla-License.txt: | $(NVIM_BUILD_DIR)

$(SCINTILLA_DLL) $(NVIM_BUILD_DIR)/Scintilla.dll: $(SCINTILLA_DIR)/Scintilla.dll
	cp "$<" "$@"

$(SCINTILLA_LICENSE) $(NVIM_BUILD_DIR)/Scintilla-License.txt: $(SCINTILLA_DIR)/License.txt
	cp "$<" "$@"

runscintillanvim: nvim
	$(MAKE) runscintilla RUN_SCINTILLA_APP="$(NVIM_APP)"

runnotepadnvim: installnvim
	$(MAKE) runnotepad

testnvim: $(NVIM_DLL) $(NVIM_BUILD_DIR)/Scintilla.dll stagenvim
	$(MAKE) run-tests TEST_BUILD_DIR="$(NVIM_BUILD_DIR)"

replaynvim: $(NVIM_APP) $(NVIM_DLL) $(NVIM_BUILD_DIR)/Scintilla.dll stagenvim
	$(MAKE) run-replay REPLAY_APP="$(NVIM_APP)"

fuzznvim: $(NVIM_DLL) $(NVIM_BUILD_DIR)/Scintilla.dll stagenvim
	$(MAKE) run-fuzz TEST_BUILD_DIR="$(NVIM_BUILD_DIR)"

fuzz-seed-nvim: $(NVIM_DLL) $(NVIM_BUILD_DIR)/Scintilla.dll stagenvim
	$(MAKE) run-fuzz-seed TEST_BUILD_DIR="$(NVIM_BUILD_DIR)"

# TESTING

# Load a saved case's initial state; leave its keys for manual debugging.
REPLAY_APP ?= $(APP)
replay: replaynative

run-replay:
	@test -n $(REPLAY_CASE_ARG) || (echo "Usage: make replay CASE=<test-case-id> [REPLAY_CASES=<json-file>]" >&2; exit 2)
	@test -f "$(REPLAY_CASES)" || (echo "Replay cases not found: $(REPLAY_CASES). Run fuzzing first or set REPLAY_CASES." >&2; exit 2)
	wine "$(REPLAY_APP)" --replay "$(abspath $(REPLAY_CASES))" $(REPLAY_CASE_ARG)

# The functional test runner creates a real Scintilla control, dynamically
# loads VimMotions.dll, and sends the window messages handled by its subclass.
format-test-cases: $(TEST_CASES_FORMATTER) $(TEST_CASES)
	python3 $(TEST_CASES_FORMATTER)

test-build: format-test-cases $(BUILD_DIR)/VimMotionsTests.exe $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)

$(TEST_APP): $(TEST_SOURCES) $(TEST_HEADERS) src/backends/common/VimMotions.h src/backends/common/ScintillaMessages.h
	mkdir -p "$(TEST_BUILD_DIR)"
	$(CXX) $(CXXFLAGS) -Itests/common/include $(TEST_SOURCES) -o $@ $(RUNTIME_LDFLAGS)

# Run from the binary directory so both runtime-loaded DLLs are found beside
# the test executable. The runner writes text and JSON reports to
# the selected build directory's test-results/ folder.
test: testnative

run-tests: format-test-cases $(TEST_APP) $(TEST_CASES)
	cd "$(TEST_BUILD_DIR)" && wine VimMotionsTests.exe "$(abspath $(TEST_CASES_DIR))" test-results

fuzz-build: $(TEST_APP) $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)

fuzz: fuzznative

fuzz-seed: fuzz-seed-native

run-fuzz: $(TEST_APP)
	python3 tests_fuzzing/differential/run_fuzz.py --test-exe "$(abspath $(TEST_APP))"

run-fuzz-seed: $(TEST_APP)
	@test -n "$(SEED)" || (echo "Usage: make fuzz-seed SEED=<integer>" >&2; exit 2)
	python3 tests_fuzzing/differential/run_fuzz.py --seed "$(SEED)" --test-exe "$(abspath $(TEST_APP))"

# MISC

# Create a small, self-contained folder and ZIP suitable for a Windows user.
# Do not add source files or the full SciTE archive here: neither is needed to
# run this current host application.
package: native $(APP)
	rm -rf $(PACKAGE_DIR) $(PACKAGE_ZIP)
	mkdir -p $(PACKAGE_DIR)/LICENSES
	cp $(APP) $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL) $(PACKAGE_DIR)/
	cp $(SCINTILLA_LICENSE) $(PACKAGE_DIR)/LICENSES/
	cp ../README.md $(PACKAGE_DIR)/
	cd dist && zip -r $(PACKAGE_NAME).zip $(PACKAGE_NAME)

clean:
	rm -rf build
