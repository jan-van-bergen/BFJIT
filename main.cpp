#include <cstdint>
#include <cstdio>
#include <cassert>

#include <Windows.h>

static uint8_t * code_buffer_base;
static uint8_t * code_buffer_head;

static uint32_t get_code_offset() {
	return code_buffer_head - code_buffer_base;
}

static void emit(uint8_t byte) {
	*code_buffer_head++ = byte;
}

static void emit16(uint32_t word) {
	emit((word)      & 0xff);
	emit((word >> 8) & 0xff);
}

static void emit32(uint32_t dword) {
	emit((dword)       & 0xff);
	emit((dword >> 8)  & 0xff);
	emit((dword >> 16) & 0xff);
	emit((dword >> 24) & 0xff);
}

static void emit64(uint64_t qword) {
	emit((qword)       & 0xff);
	emit((qword >> 8)  & 0xff);
	emit((qword >> 16) & 0xff);
	emit((qword >> 24) & 0xff);
	emit((qword >> 32) & 0xff);
	emit((qword >> 40) & 0xff);
	emit((qword >> 48) & 0xff);
	emit((qword >> 56) & 0xff);
}

static uint8_t * data_buffer[30000];

static void compile(char const * source) {
	struct Branch {
		uint32_t offset_src;
		uint32_t offset_dst;
		uint8_t * sentinel;
	};
	Branch branch_stack[256] = { };
	size_t branch_stack_size = 0;

	emit(0x48); emit(0xbe); emit64((uintptr_t)data_buffer); // mov rsi, data_buffer
	emit(0x49); emit(0xbc); emit64((uintptr_t)putchar);     // mov r12, putchar
	emit(0x49); emit(0xbd); emit64((uintptr_t)getchar);     // mov r13, getchar

	emit(0x48); emit(0x83); emit(0xec); emit(0x20); // sub rsp, 32 ; shadow space

	auto cur = source;
	while (*cur) {
retry:
		switch (*cur) {
			// Fold consecutive inc/decs of pointer
			case '>':
			case '<': {
				auto accum = 0;
				for (;;) {
					if (*cur == '>') {
						accum++;
					} else if (*cur == '<') {
						accum--;
					} else break;
					cur++;
				}
				if (accum == 1) {
					emit(0x48); emit(0xff); emit(0xc6); // inc rsi
				} else if (accum == -1) {
					emit(0x48); emit(0xff); emit(0xce); // dec rsi
				} else if (accum != 0) {
					emit(0x48); emit(0x81); emit(0xc6); emit32(accum); // add rsi, accum
				}
				break;
			}

			// Fold consecutive inc/decs of pointee
			case '+':
			case '-': {
				auto accum = 0;
				for (;;) {
					if (*cur == '+') {
						accum++;
					} else if (*cur == '-') {
						accum--;
					} else break;
					cur++;
				}
				if (accum == 1) {
					emit(0xfe); emit(0x06); // inc BYTE [rsi]
				} else if (accum == -1) {
					emit(0xfe); emit(0x0e); // dec BYTE [rsi]
				} else if (accum != 0) {
					emit(0x80); emit(0x06); emit(accum); // add BYTE [rsi], accum
				}
				break;
			}

			// Output / input
			case '.': {
				emit(0x48); emit(0x8b); emit(0x0e); // mov rcx, BYTE [rsi]
				emit(0x41); emit(0xff); emit(0xd4); // call r12 ; putchar
				cur++;
				break;
			}
			case ',': {
				emit(0x41); emit(0xff); emit(0xd5); // call r13 ; getchar
				emit(0x88); emit(0x06);             // mov BYTE [rsi], al
				cur++;
				break;
			}

			// Loops
			case '[': {
				auto offset_src = get_code_offset();
				emit(0x8a); emit(0x06); // mov al, BYTE [rsi]
				emit(0x84); emit(0xc0); // test al, al
				emit(0x0f); emit(0x84); // jz
				auto offset_dst = get_code_offset();
				branch_stack[branch_stack_size++] = { offset_src, offset_dst, code_buffer_head };
				emit32(0);
				cur++;
				break;
			}
			case ']': {
				emit(0xe9); // jmp
				assert(branch_stack_size > 0);
				auto branch = branch_stack[--branch_stack_size];
				emit32(branch.offset_src - get_code_offset() - 4);
				// Replace sentinel jump target
				auto rel32 = get_code_offset() - branch.offset_dst - 4;
				memcpy(branch.sentinel, &rel32, sizeof(uint32_t));
				cur++;
				break;
			}

			default: cur++; break;
		}
	}
	assert(branch_stack_size == 0);
	// Exit
	emit(0x48); emit(0x83); emit(0xc4); emit(0x20); // add rsp, 32 ; shadow space
	emit(0xc3); // ret
}

struct Source_File {
	HANDLE handle;
	HANDLE mapping;
	char const * source;
};

static Source_File read_file(char const * filename) {
	Source_File file = { };
	file.handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (file.handle == INVALID_HANDLE_VALUE) {
		printf("ERROR: Unable to open file '%s'!\n", filename);
		exit(GetLastError());
	}
	file.mapping = CreateFileMappingA(file.handle, 0, PAGE_WRITECOPY, 0, 0, 0);
	if (file.handle == 0) {
		printf("ERROR: Unable to memory map file '%s'!\n", filename);
		exit(GetLastError());
	}
	file.source = (char const *)MapViewOfFileEx(file.mapping, FILE_MAP_COPY, 0, 0, 0, 0);
	return file;
}

static void close_file(Source_File file) {
	UnmapViewOfFile(file.source);
	CloseHandle(file.mapping);
	CloseHandle(file.handle);
}

int main(int arg_count, char const ** args) {
	char const * filename = nullptr;
	if (arg_count > 1) {
		filename = args[1];
	} else {
		filename = "examples/mandelbrot.bf";
	}
	auto file = read_file(filename);

	code_buffer_base = (uint8_t *)VirtualAllocEx(GetCurrentProcess(), 0, 1 << 16, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (code_buffer_base == nullptr) {
		puts("ERROR: Unable to allocate virtual memory!");
		exit(GetLastError());
	}
	code_buffer_head = code_buffer_base;

	compile(file.source);
	close_file(file);

	LARGE_INTEGER time_start; QueryPerformanceCounter(&time_start);

	// Execute code buffer
	auto executable_code = (void (*)())code_buffer_base;
	executable_code();
	
	LARGE_INTEGER time_end;  QueryPerformanceCounter(&time_end);
	LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency); 
	size_t delta = (time_end.QuadPart - time_start.QuadPart) * 1000000 / frequency.QuadPart;

	printf("Finished in %zu us.\nPress any key to continue...", delta);
	getchar();
}
