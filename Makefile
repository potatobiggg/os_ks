CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -O0
LDFLAGS =

SRCDIR  = src
OBJDIR  = obj
TARGET  = vfs

# Helper: standard echo for non-Windows shells
ifeq ($(OS),Windows_NT)
	RM = del /Q
	RMDIR = rmdir /S /Q
	MKDIR = mkdir
	EXE = .exe
	SEP = \\
else
	RM = rm -f
	RMDIR = rm -rf
	MKDIR = mkdir -p
	EXE =
	SEP = /
endif

SRCS   = $(wildcard $(SRCDIR)/*.c)
OBJS   = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

.PHONY: all clean run

all: $(TARGET)$(EXE)

$(TARGET)$(EXE): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@if not exist "$(OBJDIR)" $(MKDIR) $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-$(RMDIR) $(OBJDIR)
	-$(RM) $(TARGET)$(EXE)
	-$(RM) filesystem.dat

run: $(TARGET)$(EXE)
	./$(TARGET)$(EXE) filesystem.dat

debug: CFLAGS += -DDEBUG -fsanitize=address
debug: all
