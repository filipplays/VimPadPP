# Cross-compiler target triple: build Windows x86-64 binaries from macOS.
TARGET := x86_64-w64-mingw32
CXX := $(TARGET)-g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O0 -g -Iinclude -Isrc/backends/native -Isrc/backends/neovim

# Choose the backends compiled into the plugin.
BACKENDS ?= both
ifeq ($(filter $(BACKENDS),both native nvim),)
$(error BACKENDS must be both, native, or nvim)
endif
ifneq ($(words $(BACKENDS)),1)
$(error BACKENDS must be both, native, or nvim)
endif
NATIVE_SOURCES := $(addprefix src/backends/native/,vim_engine.cpp vim_commands.cpp vim_motions.cpp vim_actions.cpp vim_operators.cpp)
NEOVIM_SOURCES := src/backends/neovim/neovim_backend.cpp
BACKEND_SOURCES :=
# ifneq ($(filter $(BACKENDS),both native),)
BACKEND_SOURCES_NATIVE += $(NATIVE_SOURCES)
CXXFLAGS_NATIVE += -DENABLE_NATIVE_BACKEND
# endif
# ifneq ($(filter $(BACKENDS),both nvim),)
BACKEND_SOURCES_NEOVIM += $(NEOVIM_SOURCES)
# endif
CORE_SOURCES_NATIVE := src/vimmotions.cpp src/editor_text.cpp src/scintilla_editor.cpp $(BACKEND_SOURCES_NATIVE)
CORE_SOURCES_NEOVIM := src/vimmotions.cpp src/editor_text.cpp src/scintilla_editor.cpp $(BACKEND_SOURCES_NEOVIM)
CORE_HEADERS := $(wildcard include/*.h src/backends/native/*.h src/backends/neovim/*.h)
# -mwindows selects a GUI entry point and -municode selects wWinMain.
LDFLAGS := -mwindows -municode
# Link MinGW's C/C++ support code into VimMotions.dll so deployment does not
# require copying compiler-specific libstdc++ / libwinpthread DLLs beside it.
RUNTIME_LDFLAGS := -static -static-libgcc -static-libstdc++

BUILD_DIR := build/windows-x64/$(BACKENDS)
APP := $(BUILD_DIR)/ScintillaHost.exe
NEOVIM_DIR ?= dependencies/neovim
SCINTILLA_DIR ?= dependencies/scintilla
SCINTILLA_DLL := $(BUILD_DIR)/Scintilla.dll
VIM_MOTIONS_DLL := $(BUILD_DIR)/VimMotions.dll
NOTEPADPP_PLUGIN_DLL := $(BUILD_DIR)/VIM-Motions-for-NotepadPP.dll
NOTEPADPP_EXE ?= /Users/filipzevnik/.wine/drive_c/Program Files/Notepad++/notepad++.exe
NOTEPADPP_PLUGIN_DIR ?= /Users/filipzevnik/.wine/drive_c/Program Files/Notepad++/plugins/VIM-Motions-for-NotepadPP
TEST_APP := $(BUILD_DIR)/VimMotionsTests.exe
TEST_CASES_DIR := tests/cases
TEST_CASES_FORMATTER := $(TEST_CASES_DIR)/format_test_cases.py
TEST_CASES := $(shell find $(TEST_CASES_DIR) -type f -name '*.json' -print)
TEST_COMMON_SOURCES := tests/common/src/json_test_cases.cpp tests/common/src/test_runner.cpp
TEST_COMMON_HEADERS := tests/common/include/TestCase.h tests/common/include/JsonTestCases.h tests/common/include/TestRunner.h
TEST_FUNCTIONAL_SOURCES := tests/functional/src/main.cpp
TEST_FUNCTIONAL_HEADERS :=
REPLAY_CASES ?= tests_fuzzing/tmp/cases/generated.json
# Preserve literal dollar signs in CASE and quote the ID for the shell.
REPLAY_CASE_ARG = '$(subst ','"'"',$(value CASE))'
TEST_SOURCES := $(TEST_FUNCTIONAL_SOURCES) $(TEST_COMMON_SOURCES)
TEST_HEADERS := $(TEST_FUNCTIONAL_HEADERS) $(TEST_COMMON_HEADERS)
SCINTILLA_LICENSE := $(BUILD_DIR)/Scintilla-License.txt
PACKAGE_NAME := ScintillaVimHost-windows-x64
PACKAGE_DIR := dist/$(PACKAGE_NAME)
PACKAGE_ZIP := dist/$(PACKAGE_NAME).zip

.PHONY: all standalone standaloneinstall nvim stage-nvim nviminstall install run notepad runbuilt replay format-test-cases test test-build fuzz fuzz-seed fuzz-build package clean

# Native backend: build, install, run, and test.
# `make` builds the native Notepad++ plugin.
all: $(NOTEPADPP_PLUGIN_DLL)

# Build and install the native backend, matching nvim / nviminstall.
standaloneinstall: standalone
	mkdir -p "$(NOTEPADPP_PLUGIN_DIR)"
	cp "$(NOTEPADPP_PLUGIN_DLL)" "$(NOTEPADPP_PLUGIN_DIR)/"

# Preserve the existing install command as an alias.
install: standaloneinstall

standalone: $(APP) $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL) $(NOTEPADPP_PLUGIN_DLL) $(SCINTILLA_LICENSE)

$(NOTEPADPP_PLUGIN_DLL): src/notepadpp_plugin.cpp $(CORE_SOURCES_NATIVE) $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_NATIVE) -DVIM_MOTIONS_BUILD -shared src/notepadpp_plugin.cpp $(CORE_SOURCES_NATIVE) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

$(BUILD_DIR):
	mkdir -p $@

$(APP): src/main.cpp tests/common/src/json_test_cases.cpp tests/common/include/JsonTestCases.h tests/common/include/TestCase.h include/VimMotions.h include/ScintillaMessages.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_NATIVE) -Itests/common/include src/main.cpp tests/common/src/json_test_cases.cpp -o $@ $(LDFLAGS) -lshell32 $(RUNTIME_LDFLAGS)

$(VIM_MOTIONS_DLL): $(CORE_SOURCES_NATIVE) $(CORE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CXXFLAGS_NATIVE) -DVIM_MOTIONS_BUILD -shared $(CORE_SOURCES_NATIVE) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

run: standalone
	wine $(APP)

# Run an already-built application. This is the command to use after a Docker
# build on macOS/Linux: it needs Wine, but does not need a local MinGW compiler.
runbuilt:
	wine $(APP)

# Load a saved case's initial state; leave its keys for manual debugging.
replay: standalone
	@test -n $(REPLAY_CASE_ARG) || (echo "Usage: make replay CASE=<test-case-id> [REPLAY_CASES=<json-file>]" >&2; exit 2)
	@test -f "$(REPLAY_CASES)" || (echo "Replay cases not found: $(REPLAY_CASES). Run fuzzing first or set REPLAY_CASES." >&2; exit 2)
	wine $(APP) --replay "$(abspath $(REPLAY_CASES))" $(REPLAY_CASE_ARG)

# The functional test runner creates a real Scintilla control, dynamically
# loads VimMotions.dll, and sends the window messages handled by its subclass.
format-test-cases: $(TEST_CASES_FORMATTER) $(TEST_CASES)
	python3 $(TEST_CASES_FORMATTER)

test-build: format-test-cases $(TEST_APP) $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)

$(TEST_APP): $(TEST_SOURCES) $(TEST_HEADERS) include/VimMotions.h include/ScintillaMessages.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Itests/common/include $(TEST_SOURCES) -o $@ $(RUNTIME_LDFLAGS)

# Run from the binary directory so both runtime-loaded DLLs are found beside
# the test executable. The runner writes text and JSON reports to
# build/windows-x64/test-results/.
test: test-build $(TEST_CASES)
	cd $(BUILD_DIR) && wine VimMotionsTests.exe "$(abspath $(TEST_CASES_DIR))" test-results

fuzz-build: $(TEST_APP) $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL)

fuzz: fuzz-build
	python3 tests_fuzzing/differential/run_fuzz.py --test-exe "$(abspath $(TEST_APP))"

fuzz-seed: fuzz-build
	@test -n "$(SEED)" || (echo "Usage: make fuzz-seed SEED=<integer>" >&2; exit 2)
	python3 tests_fuzzing/differential/run_fuzz.py --seed "$(SEED)" --test-exe "$(abspath $(TEST_APP))"

# Neovim backend: build and install.
nviminstall: nvim
	mkdir -p "$(NOTEPADPP_PLUGIN_DIR)"
	cp "$(NVIM_PLUGIN_DLL)" "$(NOTEPADPP_PLUGIN_DIR)/"
	mkdir -p "$(NOTEPADPP_PLUGIN_DIR)/neovim"
	cp -R "$(NVIM_BUILD_DIR)/neovim/." "$(NOTEPADPP_PLUGIN_DIR)/neovim/"

# Keep the Neovim build separate so it cannot overwrite the native DLL.
NVIM_BUILD_DIR := build/windows-x64/neovim-build
NVIM_APP := $(NVIM_BUILD_DIR)/ScintillaHost.exe
NVIM_DLL := $(NVIM_BUILD_DIR)/VimMotions.dll
NVIM_PLUGIN_DLL := $(NVIM_BUILD_DIR)/VIM-Motions-for-NotepadPP.dll

nvim: $(NVIM_APP) $(NVIM_DLL) $(NVIM_PLUGIN_DLL) $(NVIM_BUILD_DIR)/Scintilla.dll $(NVIM_BUILD_DIR)/Scintilla-License.txt stage-nvim

$(NVIM_BUILD_DIR):
	mkdir -p $@

$(NVIM_APP): src/main.cpp tests/common/src/json_test_cases.cpp tests/common/include/JsonTestCases.h tests/common/include/TestCase.h include/VimMotions.h include/ScintillaMessages.h | $(NVIM_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Itests/common/include src/main.cpp tests/common/src/json_test_cases.cpp -o $@ $(LDFLAGS) -lshell32 $(RUNTIME_LDFLAGS)

$(NVIM_DLL): $(CORE_SOURCES_NEOVIM) $(CORE_HEADERS) | $(NVIM_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DVIM_MOTIONS_BUILD -shared $(CORE_SOURCES_NEOVIM) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

$(NVIM_PLUGIN_DLL): src/notepadpp_plugin.cpp $(CORE_SOURCES_NEOVIM) $(CORE_HEADERS) | $(NVIM_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DVIM_MOTIONS_BUILD -shared src/notepadpp_plugin.cpp $(CORE_SOURCES_NEOVIM) -o $@ -lcomctl32 $(RUNTIME_LDFLAGS)

$(NVIM_BUILD_DIR)/Scintilla.dll: $(SCINTILLA_DIR)/Scintilla.dll | $(NVIM_BUILD_DIR)
	cp "$<" "$@"

$(NVIM_BUILD_DIR)/Scintilla-License.txt: $(SCINTILLA_DIR)/License.txt | $(NVIM_BUILD_DIR)
	cp "$<" "$@"

stage-nvim: | $(NVIM_BUILD_DIR)
	@test -f "$(NEOVIM_DIR)/bin/nvim.exe" || (echo "Missing $(NEOVIM_DIR)/bin/nvim.exe." >&2; exit 1)
	mkdir -p "$(NVIM_BUILD_DIR)/neovim"
	cp -R "$(NEOVIM_DIR)/." "$(NVIM_BUILD_DIR)/neovim/"

# Shared Scintilla dependencies.
# Keep the supplied Windows x64 DLL outside the generated build directory.
$(SCINTILLA_DIR)/Scintilla.dll $(SCINTILLA_DIR)/License.txt:
	@echo "Missing $@. Place the Scintilla distribution file there." >&2
	@exit 1

$(SCINTILLA_DLL): $(SCINTILLA_DIR)/Scintilla.dll | $(BUILD_DIR)
	cp "$<" "$@"

# Keep the upstream license with the binary we redistribute.
$(SCINTILLA_LICENSE): $(SCINTILLA_DIR)/License.txt | $(BUILD_DIR)
	cp "$<" "$@"

# Launch the configured Notepad++ installation.
# Launch an existing Notepad++ installation without building or installing.
notepad:
	wine "$(NOTEPADPP_EXE)"

# Create a small, self-contained folder and ZIP suitable for a Windows user.
# Do not add source files or the full SciTE archive here: neither is needed to
# run this current host application.
package: standalone
	rm -rf $(PACKAGE_DIR) $(PACKAGE_ZIP)
	mkdir -p $(PACKAGE_DIR)/LICENSES
	cp $(APP) $(SCINTILLA_DLL) $(VIM_MOTIONS_DLL) $(PACKAGE_DIR)/
	cp $(SCINTILLA_LICENSE) $(PACKAGE_DIR)/LICENSES/
	cp ../README.md $(PACKAGE_DIR)/
	cd dist && zip -r $(PACKAGE_NAME).zip $(PACKAGE_NAME)

clean:
	rm -rf build