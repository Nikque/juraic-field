#include "../play/memory/pagequery14_win.h"
#include <cstdio>
using namespace play::realgame;
static int checks=0,failures=0;
static void check(bool b,const char* name){++checks;if(!b){++failures;std::printf("FAIL %s\n",name);}}
int main(){
    // Only this test's own allocations are protected or filled, never another process's pages.
    FieldProcess p;p.pid=GetCurrentProcessId();p.imageBase=uint64_t(GetModuleHandleW(nullptr));p.image.ok=true;p.image.machine=0x8664;
    p.image.timeStamp=0x66a8c684;p.image.imageSize=0x6fd000;
    FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);p.creationTime=uint64_t(c.dwHighDateTime)<<32|c.dwLowDateTime;
    SYSTEM_INFO sys{};GetSystemInfo(&sys);const size_t page=sys.dwPageSize;
    auto* bytes=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!bytes)return 2;std::memset(bytes,37,page*4);
    PageQueryReader14 read;std::wstring error;check(read.open(p,error),"open read/query handle");
    check(read.granted()==0x1410,"exact read/query rights, no write/operation/injection/termination rights");
    uint8_t out[440]{};check(read.checked(uint64_t(bytes+page-100),out,sizeof out),"cross-page data read");
    check(std::all_of(std::begin(out),std::end(out),[](auto x){return x==37;}),"copied expected data");
    for(DWORD protection:{PAGE_READONLY,PAGE_EXECUTE_READWRITE,PAGE_READWRITE|PAGE_GUARD}){
        DWORD old=0;check(VirtualProtect(bytes+page,page,protection,&old)!=FALSE,"fixture changes own page protection");
        const auto copied=read.timing.copies;
        check(!read.checked(uint64_t(bytes+page-100),out,sizeof out) && copied==read.timing.copies,"unsafe second page rejected before copying any range");
        MEMORY_BASIC_INFORMATION m{};VirtualQuery(bytes+page,&m,sizeof m);
        check(m.Protect==protection,"guard/protection remains unchanged by query");
        check(VirtualProtect(bytes+page,page,PAGE_READWRITE,&old)!=FALSE,"fixture restores own data page");
    }
    check(read.checked(uint64_t(bytes),out,sizeof out),"fresh protection query recovers RW page");
    const auto copies=read.timing.copies;
    check(!read.checked(UINT64_MAX-40,out,sizeof out) && read.timing.copies==copies,"overflow rejected without copy");
    check(!read.checked(uint64_t(bytes),nullptr,440),"null destination rejected");
    check(!read.checked(uint64_t(bytes),out,0),"zero length rejected");
    check(!read.checked(uint64_t(bytes),out,65537),"oversized request rejected");
    for(DWORD flags:{PAGE_READWRITE|PAGE_NOCACHE,PAGE_READWRITE|PAGE_WRITECOMBINE}){
        auto* local=VirtualAlloc(nullptr,page,MEM_RESERVE|MEM_COMMIT,flags);
        check(local!=nullptr,"own uncached/write-combined fixture allocated");
        if(local){std::memset(local,37,page);const auto count=read.timing.copies;
            check(!read.checked(uint64_t(local),out,1) && read.timing.copies==count,"uncached/write-combined page rejected before copy");
            VirtualFree(local,0,MEM_RELEASE);}
    }
    // A nonresident untouched committed page cannot be certified; it must not be faulted in by copying.
    auto* untouched=VirtualAlloc(nullptr,page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    const auto before=read.timing.copies;check(!read.checked(uint64_t(untouched),out,1) && read.timing.copies==before,"nonresident page rejected before access");
    VirtualFree(untouched,0,MEM_RELEASE);
    const auto identity=p;p.creationTime++;PageQueryReader14 other;
    check(!other.open(p,error),"wrong process creation identity rejected");
    p=identity;p.imageBase++;check(!other.open(p,error),"wrong module base identity rejected");
    p=identity;p.image.timeStamp++;check(!other.open(p,error),"unsupported image identity rejected");
    read.close();check(!read.checked(uint64_t(bytes),out,1),"closed handle rejected");
    VirtualFree(bytes,0,MEM_RELEASE);
    std::printf("page query: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
