MODULE := backends/platform/roomwizard

MODULE_OBJS := \
	roomwizard.o \
	roomwizard-graphics.o \
	roomwizard-events.o \
	../../mixer/oss/oss-mixer.o

# We don't use rules.mk but rather manually update OBJS and MODULE_DIRS.
MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))

# (O10) Enable NEON SIMD for the graphics blit hot path
$(MODULE)/roomwizard-graphics.o: CXXFLAGS += -mfpu=neon
