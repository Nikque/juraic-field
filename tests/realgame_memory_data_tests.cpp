#include "../play/memory/datareader14_win.h"
#include <cstdio>
using namespace play::realgame;
static int failures=0,checks=0;
static void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
int main(){
    ProcessMemory memory;std::wstring why;
    check(memory.open(GetCurrentProcessId(),why),"open own process read-only");
    check(memory.granted()==0x1010,"exact read/query rights");
    FieldDataReader14 data(memory);
    // Local test allocation only. No allocation, protection change or writes in another process.
    auto* pages=static_cast<uint8_t*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(pages!=nullptr,"local test pages");if(!pages)return 1;
    for(size_t i=0;i<8192;++i)pages[i]=uint8_t(i%251);
    const auto base=uint64_t(uintptr_t(pages));std::array<uint8_t,440> bytes{};
    check(data.checked(base,bytes.data(),bytes.size()) && bytes[439]==pages[439],"checked RW data read");
    const std::array<uint64_t,2> tops{base+64,base+1024+64};std::array<Field14Bytes,2> fields{};
    auto before=data.timing;
    check(data.pass(tops,fields) && fields[0][0]==pages[0] && fields[1][0]==pages[1024],"batch reads separate fields");
    check(data.timing.copies-before.copies==2 && data.timing.queries>before.queries,"batch checks current page permissions");
    before=data.timing;
    check(data.pass(tops,fields) && data.timing.queries>before.queries,"new pass rechecks permissions");
    DWORD old=0;check(VirtualProtect(pages,4096,PAGE_EXECUTE_READ,&old)!=0,"local executable page fixture");
    before=data.timing;
    check(!data.pass(tops,fields) && data.timing.copies==before.copies,"executable data refused before copying in new pass");
    check(VirtualProtect(pages,4096,PAGE_READONLY,&old)!=0,"local read-only page fixture");
    before=data.timing;
    check(!data.checked(base,bytes.data(),bytes.size()) && data.timing.copies==before.copies,"read-only page excluded");
    check(!data.pass(std::span<const uint64_t>(tops).first(1),fields),"size mismatch refused");
    check(!data.checked(UINT64_MAX-8,bytes.data(),bytes.size()),"overflow refused");
    VirtualFree(pages,0,MEM_RELEASE);memory.close();
    check(!data.checked(base,bytes.data(),bytes.size()),"closed process refused");
    std::printf("data reader: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
