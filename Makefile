CFLAGS =  -flto -mno-ms-bitfields -Wall -O3
LDFLAGS = -flto -Wall -O3 -lm -lpthread

BINDIR:= bin
OBJDIR:= obj

DIR:= $(shell mkdir -p $(OBJDIR)/src/roxmain $(OBJDIR)/src/rolzmain $(OBJDIR)/src/ropmain $(BINDIR))
SRC:= $(shell echo src/*.c)
OBJ:= $(addprefix $(OBJDIR)/, $(addsuffix .o, $(basename $(SRC))))
DEP:= $(addprefix $(OBJDIR)/, $(addsuffix .d, $(basename $(SRC))))

CROX_SRC := $(shell echo src/roxmain/*.c)
CROX_OBJ:= $(addprefix $(OBJDIR)/, $(addsuffix .o, $(basename $(CROX_SRC))))
CROX_DEP:= $(addprefix $(OBJDIR)/, $(addsuffix .d, $(basename $(CROX_SRC))))
CROX_BIN:= $(BINDIR)/comprox

CROLZ_SRC := $(shell echo src/rolzmain/*.c)
CROLZ_OBJ:= $(addprefix $(OBJDIR)/, $(addsuffix .o, $(basename $(CROLZ_SRC))))
CROLZ_DEP:= $(addprefix $(OBJDIR)/, $(addsuffix .d, $(basename $(CROLZ_SRC))))
CROLZ_BIN:= $(BINDIR)/comprolz

CROP_SRC := $(shell echo src/ropmain/*.c)
CROP_OBJ:= $(addprefix $(OBJDIR)/, $(addsuffix .o, $(basename $(CROP_SRC))))
CROP_DEP:= $(addprefix $(OBJDIR)/, $(addsuffix .d, $(basename $(CROP_SRC))))
CROP_BIN:= $(BINDIR)/comprop

target: $(CROX_BIN) $(CROLZ_BIN) $(CROP_BIN)
.PHONY: target

define _LinkTarget
	@ echo -e " linking..."
	@ $(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
	@ echo -e " done."
endef
$(CROX_BIN):  $(OBJ) $(CROX_OBJ)  ; $(_LinkTarget)
$(CROLZ_BIN): $(OBJ) $(CROLZ_OBJ) ; $(_LinkTarget)
$(CROP_BIN):  $(OBJ) $(CROP_OBJ)  ; $(_LinkTarget)

-include $(DEP)
-include $(CROX_DEP)
-include $(CROLZ_DEP)
-include $(CROP_DEP)

$(OBJDIR)/%.d: %.c
	@ echo -n -e " generating makefile dependence of $<..."
	@ $(CC) $(CFLAGS) -MM $< | sed "s?\\(.*\\):?$(OBJDIR)/$(basename $<).o $(basename $<).d :?g" > $@
	@ echo -e " done."

$(OBJDIR)/%.o: %.c
	@ echo -n -e " compiling $<..."
	@ $(CC) $(CFLAGS) -c -o $@ $<
	@ echo -e " done."

clean:
	@ echo -n -e " cleaning..."
	@ rm -rf $(DEP) $(OBJ) $(BIN)
	@ rm -rf $(CROX_DEP)  $(CROX_OBJ)  $(CROX_BIN)
	@ rm -rf $(CROLZ_DEP) $(CROLZ_OBJ) $(CROLZ_BIN)
	@ rm -rf $(CROP_DEP)  $(CROP_OBJ)  $(CROP_BIN)
	@
	@ rmdir -p --ignore-fail-on-non-empty $(OBJDIR)/src/roxmain
	@ rmdir -p --ignore-fail-on-non-empty $(OBJDIR)/src/rolzmain
	@ rmdir -p --ignore-fail-on-non-empty $(OBJDIR)/src/ropmain
	@ rmdir -p --ignore-fail-on-non-empty $(BINDIR)
	@ echo -e " done."

.IGNORE: clean
.PHONY:  clean
