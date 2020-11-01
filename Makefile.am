AUTOMAKE_OPTIONS = foreign subdir-objects

bin_PROGRAMS = openzoo
openzoo_SOURCES = \
	src/editor.cpp \
	src/elements.cpp \
	src/game.cpp \
	src/high_scores.cpp \
	src/input.cpp \
	src/oop.cpp \
	src/sounds.cpp \
	src/txtwind.cpp \
	src/utils.cpp \
	src/video.cpp \
	src/audio_simulator.cpp \
	src/main.cpp

if DRIVER_SDL2
openzoo_SOURCES += src/driver_sdl2.cpp
endif

openzoo_CFLAGS = $(DRIVER_CFLAGS)
openzoo_CXXFLAGS = $(DRIVER_CFLAGS)
openzoo_LDFLAGS = $(DRIVER_LDFLAGS)