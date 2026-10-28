#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS, relative to the Makefile (Optional)
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# APP_TITLEID is the titleID of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.jpg
#     - icon.jpg
#     - <libnx folder>/default_icon.jpg
#
# CONFIG_JSON is the filename of the NPDM config file (.json), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.json
#     - config.json
#   If a JSON file is provided or autodetected, an ExeFS PFS0 (.nsp) is built instead
#   of a homebrew executable (.nro). This is intended to be used for sysmodules.
#   NACP building is skipped as well.
#---------------------------------------------------------------------------------
TARGET		:=	PKSE
BUILD		:=	build
SOURCES		:=	src src/Pokemon src/Encryption src/Enums src/UI src/UI/Panels src/UI/Dialogs src/UI/Modals src/Trainer src/Names src/Utils src/Save src/Legality src/Conversion nanovg memecrypto
DATA		:=	data
INCLUDES	:=	include nanovg memecrypto
APP_TITLE   :=  PKSE
APP_AUTHOR  :=  Kiasta
# THE version, in two spellings. Both are set here and nothing downstream needs editing.
#
# APP_VERSION is the .nacp one -- the home menu and hbmenu read it. The name is not ours to choose:
# libnx's switch_rules passes $(APP_VERSION) straight to `nacptool --create`. Its display_version
# field is 16 bytes and nacptool truncates to fit WITHOUT complaining (exit 0, no warning), so this
# must stay at 15 characters or fewer.
#
# APP_VERSION_FULL is the one the app prints about itself: -DPKSE_VERSION below feeds it to
# Globals.h's VERSION_STRING, which has no length limit. Long pre-release tags belong here.
#
# Keep the short one an ABBREVIATION of the long one. The split exists so the .nacp can hold less of
# the version, not a different version.
#
# NOTE: no trailing comment on either assignment line. Make keeps trailing whitespace in a value, so
# "0.0.3 \t\t# ..." would have baked spaces into the .nacp version and the -D define.
APP_VERSION :=	1.2.2
APP_VERSION_FULL :=	1.2.2

# Mirrors what switch_rules does for APP_VERSION: an unset long form falls back to the short one
# rather than compiling in an empty version string.
ifeq ($(strip $(APP_VERSION_FULL)),)
APP_VERSION_FULL := $(APP_VERSION)
endif
#---------------------------------------------------------------------------------
# BUILD_TYPE: production (the default, and the only one a release is built as) or development.
#
#   make                          -> PKSE.nro      (objects in build/)
#   make BUILD_TYPE=development   -> PKSE-dev.nro  (objects in build-development/)
#   make all                      -> both
#
# The development build is the production build PLUS the on-console self test: it compiles
# DEVELOPMENT_SOURCES and defines PKSE_DEVELOPMENT_BUILD, which turns on the save picker's `-`
# binding (SELF_TEST_AVAILABLE in Globals.h). It also names itself, so it is never mistaken for a
# release: "PKSE (Development)" in the home menu, a red DEVELOPMENT at the right of every screen's
# title bar, and the version gains -dev in the .nacp and -development in the app's own title bar,
# logs and self-test report. Nothing else may differ between the two. In particular SD-card logging
# stays a runtime setting -- Settings -> "Enable Debug Logging", default off -- in BOTH builds, so a
# user who hits a bug on a release can still produce a log.
#
# Each build type has its own object directory and its own output file. An earlier `make ... prod`
# modifier (-DPKSE_PROD) shared both, so switching needed a 'make clean' that nobody remembered:
# objects did not depend on the define and the stale .nro was not relinked. Keep them separate.
#---------------------------------------------------------------------------------
BUILD_TYPE ?= production
DEVELOPMENT_SOURCES := src/Test
DEVELOPMENT_DEFINES := -DPKSE_DEVELOPMENT_BUILD
DEVELOPMENT_TARGET := $(TARGET)-dev
DEVELOPMENT_BUILD := $(BUILD)-development
PRODUCTION_TARGET := $(TARGET)
PRODUCTION_BUILD := $(BUILD)
BUILD_TYPE_DEFINES :=

ifeq ($(BUILD_TYPE),development)
TARGET  := $(DEVELOPMENT_TARGET)
BUILD   := $(DEVELOPMENT_BUILD)
SOURCES += $(DEVELOPMENT_SOURCES)
BUILD_TYPE_DEFINES := $(DEVELOPMENT_DEFINES)
APP_TITLE := $(APP_TITLE) (Development)
APP_VERSION := $(APP_VERSION)-dev
APP_VERSION_FULL := $(APP_VERSION_FULL)-development
else ifneq ($(BUILD_TYPE),production)
$(error BUILD_TYPE must be production or development, not "$(BUILD_TYPE)")
endif

# nacptool truncates a version over 15 characters without a word, so refuse to build one instead.
ifneq ($(shell test $$(printf '%s' '$(APP_VERSION)' | wc -c) -le 15 && echo fits),fits)
$(error APP_VERSION "$(APP_VERSION)" is longer than 15 characters; nacptool would truncate it)
endif
ROMFS		:=	romfs
ICON		:=  assets/icon.jpg

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# NanoVG: disable its stb_image (we feed RGBA buffers via nvgCreateImageRGBA, and SpriteManager
# already owns the STB_IMAGE_IMPLEMENTATION) — avoids duplicate symbols. Keeps fontstash for text.
DEFINES	:=	-DNVG_NO_STB -DPKSE_VERSION='"$(APP_VERSION_FULL)"'
DEFINES	+=	$(BUILD_TYPE_DEFINES)

# SDL2 provides the window, GL context and input ONLY -- rendering is NanoVG on GL,
# PNG decoding is stb_image and text is NanoVG's own font atlas, so SDL2_image and
# SDL2_ttf are no longer linked. Resolve the exact include paths and static link chain
# from devkitPro's pkg-config so we don't hand-maintain the (long, order-sensitive)
# dependency list.
#---------------------------------------------------------------------------------
SDL_PKGCONFIG	:=	$(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
SDL_CFLAGS	:=	$(shell $(SDL_PKGCONFIG) --cflags sdl2)
SDL_LIBS	:=	$(shell $(SDL_PKGCONFIG) --libs --static sdl2)

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ $(SDL_CFLAGS)

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

CXXFLAGS	+=	-std=c++20

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# SDL_LIBS already pulls in -lnx -lz -lm and the EGL/mesa/freetype/png/jpeg/webp
# chain; we only need to add the project's own extra libs (lz4).
# NanoVG renders through OpenGL 4.3 core loaded by switch-glad. -lglad must come BEFORE the
# EGL/mesa chain (which SDL_LIBS ends with: -lEGL -lglapi -ldrm_nouveau -lnx) so the static
# linker resolves glad's eglGetProcAddress reference.
LIBS	:= -lglad $(SDL_LIBS) -llz4 -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

# Default target when you just run 'make'. Only builds.
default: $(BUILD)

# 'make all' builds BOTH: the production build, then the development one (each in its own object
# directory, so neither disturbs the other). 'make BUILD_TYPE=development' still builds the
# development one alone, and so does 'make all BUILD_TYPE=development'. It used to fetch the romfs
# assets first; it does not any more.
ifeq ($(BUILD_TYPE),production)
all: $(BUILD)
	@$(MAKE) --no-print-directory BUILD_TYPE=development
else
all: $(BUILD)
endif

#---------------------------------------------------------------------------------
# romfs assets: REQUIRED, and NOT fetched here
#
# NOTHING IN THIS MAKEFILE DOWNLOADS ANYTHING. Fetching is five scripts under tools/, run by hand
# once per checkout -- romfs/ is gitignored, so a fresh one has none of it:
#
#     python tools/gen_fonts.py         the three SIL OFL UI fonts
#     python tools/gen_typeicons.py     the 19 type icons (18 + Stellar, which is Tera-only)
#     python tools/gen_marks.py         the origin markings
#     python tools/gen_ribbons.py       the ribbon and mark images
#     python tools/gen_hdsprites.py     the 3260 Pokemon sprites (needs Pillow, ~148 MB)
#
# A BUILD HAS NO BUSINESS REACHING THE NETWORK. It makes the toolchain depend on GitHub being up,
# it can half-succeed and leave a partial file behind, and it hides which upstream snapshot the
# .nro was actually built from.
#
# So the build REFUSES TO START instead of quietly fetching (`check-assets`, below -- named
# so it cannot collide with the assets/ DIRECTORY, which would let Make call it up to date). All five are mandatory: without the
# fonts nothing on screen has text at all, and the rest are blank art with nothing anywhere saying
# why -- a missing asset otherwise links into a perfectly healthy .nro. One representative file
# per group is enough, because each script verifies its own set and is safe to re-run.
#---------------------------------------------------------------------------------
ASSET_FONTS   := romfs/fonts/Nunito.ttf
ASSET_TYPES   := romfs/sprites/types/0.png
ASSET_MARKS   := romfs/sprites/marks/vc.png
ASSET_RIBBONS := romfs/sprites/ribbons/ribbonchampionkalos.png
ASSET_SPRITES := romfs/sprites/pokemon_hd/1.png

check-assets:
	@missing=""; \
	[ -s "$(CURDIR)/$(ASSET_FONTS)" ]   || missing="$$missing\n  UI fonts .......... python tools/gen_fonts.py"; \
	[ -s "$(CURDIR)/$(ASSET_TYPES)" ]   || missing="$$missing\n  type icons ........ python tools/gen_typeicons.py"; \
	[ -s "$(CURDIR)/$(ASSET_MARKS)" ]   || missing="$$missing\n  origin markings ... python tools/gen_marks.py"; \
	[ -s "$(CURDIR)/$(ASSET_RIBBONS)" ] || missing="$$missing\n  ribbon images ..... python tools/gen_ribbons.py"; \
	[ -s "$(CURDIR)/$(ASSET_SPRITES)" ] || missing="$$missing\n  Pokemon sprites ... python tools/gen_hdsprites.py"; \
	if [ -n "$$missing" ]; then \
		printf 'error: romfs is missing assets the .nro must contain, and this build does not\n'; \
		printf '       download anything. Fetch them once, then build again:%b\n' "$$missing"; \
		exit 1; \
	fi

#---------------------------------------------------------------------------------
.PHONY: $(BUILD) clean all check-assets

$(BUILD): check-assets
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile all

#---------------------------------------------------------------------------------
# Cleans BOTH build types, whichever BUILD_TYPE it is run with.
clean:
	@printf "clean ...\n"
ifeq ($(strip $(APP_JSON)),)
	@rm -fr $(foreach name,$(PRODUCTION_TARGET) $(DEVELOPMENT_TARGET),$(name).nro $(name).nacp $(name).elf $(name).lst) \
		$(PRODUCTION_BUILD) $(DEVELOPMENT_BUILD)
else
	@rm -fr $(foreach name,$(PRODUCTION_TARGET) $(DEVELOPMENT_TARGET),$(name).nsp $(name).nso $(name).npdm $(name).elf $(name).lst) \
		$(PRODUCTION_BUILD) $(DEVELOPMENT_BUILD)
endif

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(APP_JSON)),)

all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

else

all	:	$(OUTPUT).nsp

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso	:	$(OUTPUT).elf

endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@printf $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
