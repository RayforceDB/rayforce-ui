# Makefile
CC = clang
CXX = clang++
STD = c17

ifeq ($(OS),)
OS := $(shell uname -s | tr "[:upper:]" "[:lower:]")
endif

IS_WINDOWS := $(filter Windows_NT,$(OS))$(findstring mingw,$(OS))$(findstring msys,$(OS))

# Build rayforce library first
RAYFORCE_DIR = deps/rayforce
ifneq (,$(IS_WINDOWS))
RAYFORCE_LIB = $(RAYFORCE_DIR)/librayforce.exe.a
else
RAYFORCE_LIB = $(RAYFORCE_DIR)/librayforce.a
endif

# ImGui/ImPlot directories
IMGUI_DIR = deps/imgui
IMPLOT_DIR = deps/implot
FILEDIALOG_DIR = deps/ImGuiFileDialog

# GLFW directory
GLFW_DIR = deps/glfw

# ImGui source files (excluding imgui_demo.cpp - not needed for production)
IMGUI_SRC = $(IMGUI_DIR)/imgui.cpp \
            $(IMGUI_DIR)/imgui_draw.cpp \
            $(IMGUI_DIR)/imgui_tables.cpp \
            $(IMGUI_DIR)/imgui_widgets.cpp \
            $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
            $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

# ImPlot source files
IMPLOT_SRC = $(IMPLOT_DIR)/implot.cpp \
             $(IMPLOT_DIR)/implot_items.cpp

# ImGuiFileDialog source
FILEDIALOG_SRC = $(FILEDIALOG_DIR)/ImGuiFileDialog.cpp

# GLFW source files - common
GLFW_SRC_COMMON = $(GLFW_DIR)/src/context.c \
                  $(GLFW_DIR)/src/init.c \
                  $(GLFW_DIR)/src/input.c \
                  $(GLFW_DIR)/src/monitor.c \
                  $(GLFW_DIR)/src/platform.c \
                  $(GLFW_DIR)/src/vulkan.c \
                  $(GLFW_DIR)/src/window.c \
                  $(GLFW_DIR)/src/egl_context.c \
                  $(GLFW_DIR)/src/osmesa_context.c \
                  $(GLFW_DIR)/src/null_init.c \
                  $(GLFW_DIR)/src/null_monitor.c \
                  $(GLFW_DIR)/src/null_window.c \
                  $(GLFW_DIR)/src/null_joystick.c

# ImGui object files
IMGUI_OBJ = $(IMGUI_SRC:.cpp=.o)
IMPLOT_OBJ = $(IMPLOT_SRC:.cpp=.o)
FILEDIALOG_OBJ = $(FILEDIALOG_SRC:.cpp=.o)

# ImGui includes
IMGUI_INCLUDES = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(IMPLOT_DIR)

# GLFW includes
GLFW_INCLUDES = -I$(GLFW_DIR)/include

# Platform-specific settings
ifeq ($(OS),linux)
# GLFW Linux-specific sources (X11)
GLFW_SRC_PLATFORM = $(GLFW_DIR)/src/x11_init.c \
                    $(GLFW_DIR)/src/x11_monitor.c \
                    $(GLFW_DIR)/src/x11_window.c \
                    $(GLFW_DIR)/src/xkb_unicode.c \
                    $(GLFW_DIR)/src/posix_poll.c \
                    $(GLFW_DIR)/src/posix_time.c \
                    $(GLFW_DIR)/src/posix_thread.c \
                    $(GLFW_DIR)/src/posix_module.c \
                    $(GLFW_DIR)/src/glx_context.c \
                    $(GLFW_DIR)/src/linux_joystick.c

GLFW_DEFINES = -D_GLFW_X11
# Note: Use full path for libraries without .so symlinks
GLFW_LIBS = -lX11 -lXrandr -lXinerama -lXcursor -lXi /lib/x86_64-linux-gnu/libXxf86vm.so.1

CFLAGS = -include $(RAYFORCE_DIR)/core/def.h -fPIC -Wall -Wextra -std=$(STD) -g -O0 -march=native -fsigned-char -DDEBUG -m64
LIBS = -lm -ldl -lpthread -lGL /usr/lib/x86_64-linux-gnu/libstdc++.so.6 $(GLFW_LIBS)
endif

ifeq ($(OS),darwin)
# GLFW macOS-specific sources
GLFW_SRC_PLATFORM = $(GLFW_DIR)/src/cocoa_init.m \
                    $(GLFW_DIR)/src/cocoa_monitor.m \
                    $(GLFW_DIR)/src/cocoa_window.m \
                    $(GLFW_DIR)/src/cocoa_joystick.m \
                    $(GLFW_DIR)/src/cocoa_time.c \
                    $(GLFW_DIR)/src/nsgl_context.m \
                    $(GLFW_DIR)/src/posix_thread.c \
                    $(GLFW_DIR)/src/posix_module.c

GLFW_DEFINES = -D_GLFW_COCOA
GLFW_LIBS =

CFLAGS = -include $(RAYFORCE_DIR)/core/def.h -fPIC -Wall -Wextra -std=$(STD) -g -O0 -march=native -fsigned-char -DDEBUG -m64
LIBS = -lm -ldl -lpthread -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -lc++
endif

ifneq (,$(IS_WINDOWS))
AR = llvm-ar
# GLFW Windows-specific sources
GLFW_SRC_PLATFORM = $(GLFW_DIR)/src/win32_init.c \
                    $(GLFW_DIR)/src/win32_monitor.c \
                    $(GLFW_DIR)/src/win32_window.c \
                    $(GLFW_DIR)/src/win32_joystick.c \
                    $(GLFW_DIR)/src/win32_time.c \
                    $(GLFW_DIR)/src/win32_thread.c \
                    $(GLFW_DIR)/src/win32_module.c \
                    $(GLFW_DIR)/src/wgl_context.c

GLFW_DEFINES = -D_GLFW_WIN32
GLFW_LIBS =

CFLAGS = -include $(RAYFORCE_DIR)/core/def.h -Wall -Wextra -std=$(STD) -g -O0 -D_CRT_SECURE_NO_WARNINGS -DDEBUG
LIBS = -fuse-ld=lld -static -Wl,/subsystem:windows -Wl,/entry:mainCRTStartup -lws2_32 -lmswsock -lkernel32 -lopengl32 -lgdi32 -luser32 -lshell32
LIBS_DEBUG = -ldbghelp
TARGET = rayforce-ui.exe
endif

# GLFW C flags (separate from main CFLAGS to avoid def.h force-include)
ifneq (,$(IS_WINDOWS))
GLFW_CFLAGS = -Wall -std=c99 -g -O0 $(GLFW_DEFINES) -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src
else
# Note: _GNU_SOURCE needed for clock_gettime, CLOCK_MONOTONIC, O_CLOEXEC, etc.
GLFW_CFLAGS = -fPIC -Wall -std=c99 -g -O0 -D_GNU_SOURCE $(GLFW_DEFINES) -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src
endif

# All GLFW sources
GLFW_SRC = $(GLFW_SRC_COMMON) $(GLFW_SRC_PLATFORM)
GLFW_OBJ = $(patsubst %.m,%.o,$(GLFW_SRC:.c=.o))

# C++ flags for ImGui (includes GLFW headers)
# Note: C++ files should NOT include rayforce/core directly (shadows system string.h)
ifneq (,$(IS_WINDOWS))
CXXFLAGS = -std=c++17 -D_CRT_SECURE_NO_WARNINGS $(IMGUI_INCLUDES) $(GLFW_INCLUDES) -Wall -Wextra -g -O0
else
GCC_VER := $(shell ls /usr/include/c++/ 2>/dev/null | sort -V | tail -1)
GCC_CXX_INCLUDES := $(if $(GCC_VER),-cxx-isystem /usr/include/c++/$(GCC_VER) -cxx-isystem /usr/include/x86_64-linux-gnu/c++/$(GCC_VER))
CXXFLAGS = -std=c++11 $(GCC_CXX_INCLUDES) $(IMGUI_INCLUDES) $(GLFW_INCLUDES) -fPIC -Wall -Wextra -g -O0
endif

# Includes for C files (can include rayforce core)
INCLUDES_C = -Iinclude -I$(RAYFORCE_DIR)/core $(IMGUI_INCLUDES) $(GLFW_INCLUDES)

# Includes for C++ files (exclude rayforce core to avoid shadowing system headers)
INCLUDES_CXX = -Iinclude $(IMGUI_INCLUDES) $(GLFW_INCLUDES) -Ideps/nanosvg -I$(FILEDIALOG_DIR)

# C source files
SRC_C = src/main.c src/queue.c src/widget.c src/context.c src/rayforce_thread.c
OBJ_C = $(SRC_C:.c=.o)

# C++ source files (rayforce-ui)
SRC_CXX = src/ui.cpp src/widget_registry.cpp src/grid_renderer.cpp src/chart_renderer.cpp src/text_renderer.cpp src/toast.cpp src/repl_renderer.cpp src/syntax.cpp src/theme.cpp src/logo.cpp
OBJ_CXX = $(SRC_CXX:.cpp=.o)

ifeq (,$(IS_WINDOWS))
TARGET = rayforce-ui
endif

default: $(TARGET)
debug: $(TARGET)

EMBED_ASSETS = assets/fonts/FiraCode-Bold.ttf assets/fonts/fa-solid-900.otf assets/images/logo.svg assets/images/icon.svg

src/embed_assets.h: $(EMBED_ASSETS) scripts/embed.sh
	sh scripts/embed.sh $(EMBED_ASSETS) > $@

$(OBJ_CXX): src/embed_assets.h

ifneq (,$(IS_WINDOWS))
release: CFLAGS = -include $(RAYFORCE_DIR)/core/def.h -Wall -Wextra -std=$(STD) -O3 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS
release: CXXFLAGS = -std=c++17 -D_CRT_SECURE_NO_WARNINGS $(IMGUI_INCLUDES) $(GLFW_INCLUDES) -Wall -Wextra -O3 -DNDEBUG
release: GLFW_CFLAGS = -Wall -std=c99 -O3 $(GLFW_DEFINES) -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src
release: LIBS_DEBUG =
else
release: CFLAGS = -include $(RAYFORCE_DIR)/core/def.h -fPIC -Wall -Wextra -std=$(STD) -O3 -DNDEBUG -march=native -fsigned-char -m64
release: CXXFLAGS = -std=c++11 $(GCC_CXX_INCLUDES) $(IMGUI_INCLUDES) $(GLFW_INCLUDES) -fPIC -Wall -Wextra -O3 -DNDEBUG
release: GLFW_CFLAGS = -fPIC -Wall -std=c99 -O3 -D_GNU_SOURCE $(GLFW_DEFINES) -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src
endif
release: RAYFORCE_MAKE_TARGET = lib
release: $(TARGET)

# Default to debug build of rayforce
RAYFORCE_MAKE_TARGET ?= lib-debug

# Build rayforce lib (always delegates to submake)
$(RAYFORCE_LIB):
	$(MAKE) -C $(RAYFORCE_DIR) $(RAYFORCE_MAKE_TARGET)

# Use C++ linker since we have C++ objects
ifneq (,$(IS_WINDOWS))
$(TARGET): $(OBJ_C) $(OBJ_CXX) $(IMGUI_OBJ) $(IMPLOT_OBJ) $(FILEDIALOG_OBJ) $(GLFW_OBJ) $(RAYFORCE_LIB)
	$(CXX) -Wl,/map:$@.map -o $@ $(filter-out $(RAYFORCE_LIB),$^) $(RAYFORCE_LIB) $(LIBS) $(LIBS_DEBUG)
else
$(TARGET): $(OBJ_C) $(OBJ_CXX) $(IMGUI_OBJ) $(IMPLOT_OBJ) $(FILEDIALOG_OBJ) $(GLFW_OBJ) $(RAYFORCE_LIB)
	$(CXX) -nostdlib++ -rdynamic -o $@ $(filter-out $(RAYFORCE_LIB),$^) $(RAYFORCE_LIB) $(LIBS)
endif

# C source compilation for rayforce-ui
src/%.o: src/%.c | deps
	$(CC) $(CFLAGS) $(INCLUDES_C) -c $< -o $@

# C++ source compilation for rayforce-ui
src/%.o: src/%.cpp | deps
	$(CXX) $(CXXFLAGS) $(INCLUDES_CXX) -c $< -o $@

# C++ source compilation for ImGui
$(IMGUI_DIR)/%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(IMGUI_DIR)/backends/%.o: $(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(IMPLOT_DIR)/%.o: $(IMPLOT_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(IMGUI_DIR) -c $< -o $@

$(FILEDIALOG_DIR)/%.o: $(FILEDIALOG_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C source compilation for GLFW
$(GLFW_DIR)/src/%.o: $(GLFW_DIR)/src/%.c
	$(CC) $(GLFW_CFLAGS) -c $< -o $@

# Objective-C source compilation for GLFW (macOS)
$(GLFW_DIR)/src/%.o: $(GLFW_DIR)/src/%.m
	$(CC) $(GLFW_CFLAGS) -c $< -o $@

# Fetch dependencies
deps:
	@if [ ! -d "$(RAYFORCE_DIR)" ]; then \
		echo "Cloning Rayforce..."; \
		git clone --depth 1 https://github.com/RayforceDB/rayforce.git $(RAYFORCE_DIR); \
	fi

# Extension directories
EXT_DIR = deps/rayforce/ext
EXT_RAYKX = $(EXT_DIR)/raykx

# Build extensions and copy to ext/
ext:
	$(MAKE) -C $(EXT_RAYKX) release
	@mkdir -p ext
	@cp $(EXT_RAYKX)/libraykx.so ext/ 2>/dev/null || cp $(EXT_RAYKX)/libraykx.dylib ext/ 2>/dev/null || true

clean:
	rm -f src/embed_assets.h
	rm -f $(OBJ_C) $(OBJ_CXX) $(IMGUI_OBJ) $(IMPLOT_OBJ) $(FILEDIALOG_OBJ) $(GLFW_OBJ) $(TARGET)
	@if [ -d "$(RAYFORCE_DIR)" ]; then $(MAKE) -C $(RAYFORCE_DIR) clean; fi
	rm -f $(RAYFORCE_LIB)
	$(MAKE) -C $(EXT_RAYKX) clean 2>/dev/null || true
	rm -rf ext/

.PHONY: default debug release clean deps ext
