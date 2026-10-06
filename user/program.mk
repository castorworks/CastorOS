# 用户程序的公共构建规则。使用方式（见 user/init/Makefile）：
#
#   TARGET      = 程序名（生成 build/$(ARCH)/$(TARGET).elf 和去掉调试信息的 .stripped.elf）
#   SOURCES     = C++ 源文件
#   ASM_SOURCES = 汇编源文件（可选），ASM_DEFINES 是给它们的 -D 选项
#   INCLUDES    = 额外的 -I 选项（可选）
#   MODULES     = 汇编里 .incbin 进来的文件（可选）
#   include ../program.mk

include $(dir $(lastword $(MAKEFILE_LIST)))arch.mk

LDSCRIPT = ../linker/user_$(ARCH).ld
# max-page-size: aarch64 的链接器默认按 64KB 对齐段在文件里的位置，每个程序白占 60 多 KB

CXXFLAGS = $(USER_CXXFLAGS) -I../lib/include $(INCLUDES)

BUILD_DIR = build/$(ARCH)
LIB_DIR = ../lib
LIBRARY = $(LIB_DIR)/build/$(ARCH)/libuser.a

OBJECTS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(SOURCES)) \
          $(patsubst %.S, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
ELF = $(BUILD_DIR)/$(TARGET).elf
# 去掉符号和调试信息的副本：嵌进别的映像（init、启动映像、内核）时用它，调试时用 $(ELF)
STRIPPED = $(BUILD_DIR)/$(TARGET).stripped.elf

.PHONY: all clean clean-all lib

all: lib $(ELF)

lib:
	@$(MAKE) --no-print-directory -C $(LIB_DIR) ARCH=$(ARCH)

$(LIBRARY): lib

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.S $(MODULES)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH_CFLAGS) $(ASM_DEFINES) -c $< -o $@

$(ELF): $(OBJECTS) $(LIBRARY)
	@mkdir -p $(dir $@)
	$(LD) -T $(LDSCRIPT) -nostdlib -z max-page-size=0x1000 -u _start -o $@ $(OBJECTS) $(LIBRARY)
	@$(CROSS)strip -o $(STRIPPED) $@
	@echo "[OK] $(TARGET) built for $(ARCH): $(ELF)"

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)

clean-all:
	rm -rf build
